#include "widget/wkey.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QStylePainter>

#include "control/controlproxy.h"
#include "library/library_prefs.h"
#include "moc_wkey.cpp"
#include "preferences/keydetectionsettings.h"
#include "preferences/usersettings.h"
#include "skin/legacy/skincontext.h"
#include "track/keyutils.h"

#include <algorithm>
#include <memory>

namespace {

// Sets the key notation for the whole player: the labels, the Key column of every table (they ask KeyUtils at paint time)
// and the tables' key sort order (ColumnCache hears the [Library],key_notation control). It is also written to the settings
// file at once, not only when the player exits, so that a power cut does not lose the choice.
void applyKeyNotation(KeyUtils::KeyNotation notation, UserSettingsPointer pConfig, ControlProxy* pNotationControl) {
    QMap<mixxx::track::io::key::ChromaticKey, QString> names;
    for (int key = mixxx::track::io::key::C_MAJOR; key <= mixxx::track::io::key::B_MINOR; ++key) {
        const auto chromaticKey = static_cast<mixxx::track::io::key::ChromaticKey>(key);
        names[chromaticKey] = KeyUtils::keyToString(chromaticKey, notation);
    }
    KeyUtils::setNotation(names);
    pNotationControl->set(static_cast<double>(notation));
    if (pConfig) {
        KeyDetectionSettings(pConfig).setKeyNotation(notation == KeyUtils::KeyNotation::Lancelot
                        ? QStringLiteral(KEY_NOTATION_LANCELOT)
                        : QStringLiteral(KEY_NOTATION_TRADITIONAL));
        pConfig->save();
    }
    // The tables read the notation when they paint: have them paint again
    const auto widgets = QApplication::allWidgets();
    for (QWidget* pWidget : widgets) {
        if (auto* pView = qobject_cast<QAbstractItemView*>(pWidget)) {
            pView->viewport()->update();
        }
    }
}

} // namespace

WKey::WKey(const QString& group, UserSettingsPointer pConfig, QWidget* pParent)
        : WLabel(pParent),
          m_keyNotation(mixxx::library::prefs::kKeyNotationConfigKey, this),
          m_pConfig(pConfig),
          m_engineKeyDistance(group,
                  "visual_key_distance",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_engineKey(group,
                  "key",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_colorPaletteSettings(pConfig) {
    setValue();
    m_keyNotation.connectValueChanged(this, &WKey::keyNotationChanged);
    m_engineKeyDistance.connectValueChanged(this, &WKey::setCents);
}

void WKey::onConnectedControlChanged(double dParameter, double dValue) {
    Q_UNUSED(dParameter);
    Q_UNUSED(dValue);
    // Enums are not currently represented using parameter space so it doesn't
    // make sense to use the parameter here yet.
    setValue();
}

void WKey::setup(const QDomNode& node, const SkinContext& context) {
    WLabel::setup(node, context);
    m_displayCents = context.selectBool(node, "DisplayCents", false);
    m_displayKey = context.selectBool(node, "DisplayKey", true);
}

void WKey::setValue() {
    m_key = KeyUtils::keyFromNumericValue(m_engineKey.get());
    m_diff_cents = m_engineKeyDistance.get();
    if (m_key != mixxx::track::io::key::INVALID) {
        // Render this key with the user-provided notation.
        QString keyStr = "";
        if (m_displayKey) {
            keyStr = KeyUtils::keyToString(m_key);
        }
        if (m_displayCents) {
            int cents_to_display = static_cast<int>(m_diff_cents * 100);
            char sign = ' ';
            if (m_diff_cents < 0) {
                sign = '-';
            } else if (m_diff_cents > 0) {
                sign = '+';
            }
            keyStr.append(QString(" %1%2c").arg(sign).arg(qAbs(cents_to_display)));
        }
        setText(keyStr);
    } else {
        setText("");
    }
    update();
}

void WKey::mousePressEvent(QMouseEvent* pEvent) {
    // A tap flips the notation everywhere (labels and tables) between Camelot ("8A") and the standard names ("Am").
    if (pEvent->button() == Qt::LeftButton) {
        const bool camelot = KeyUtils::keyNotationFromNumericValue(m_keyNotation.get()) == KeyUtils::KeyNotation::Lancelot;
        applyKeyNotation(camelot ? KeyUtils::KeyNotation::Traditional : KeyUtils::KeyNotation::Lancelot,
                m_pConfig,
                &m_keyNotation);
        setValue(); // a ControlProxy does not signal the object that changed it
        pEvent->accept();
        return;
    }
    WLabel::mousePressEvent(pEvent);
}

void WKey::setCents() {
    setValue();
}

void WKey::keyNotationChanged(double dKeyNotationValue) {
    Q_UNUSED(dKeyNotationValue);
    // NOTE: dKeyNotationValue is the index of the key notation type, NOT the
    // key itself, so we intentionally set the old value again to update the UI.
    setValue();
}

void WKey::paintEvent(QPaintEvent* event) {
    if (m_key == mixxx::track::io::key::INVALID || !m_colorPaletteSettings.getKeyColorsEnabled()) {
        WLabel::paintEvent(event);
        return;
    }

    // The text, left-aligned like the other sidebar rows, then a small pale square in the key's
    // colour to its right ("♭ G♯m ■").
    ColorPalette keyColorPalette = m_colorPaletteSettings.getConfigKeyColorPalette();
    QColor squareColor = KeyUtils::keyToColor(m_key, keyColorPalette);
    // paler: 45 % of the way to white
    squareColor = QColor::fromRgbF(
            squareColor.redF() * 0.55 + 0.45,
            squareColor.greenF() * 0.55 + 0.45,
            squareColor.blueF() * 0.55 + 0.45);

    QStyleOption option;
    option.initFrom(this);
    QStylePainter painter(this);

    // Paint the stylesheet's background/border first: this custom painting never did.
    painter.drawPrimitive(QStyle::PE_Widget, option);

    const QFontMetrics fontMetrics = option.fontMetrics;
    const int squareSize = std::max(8, fontMetrics.height() / 2);
    const int gap = 8;
    const QString elidedText = fontMetrics.elidedText(
            text(), Qt::ElideRight, std::max(0, width() - squareSize - gap));
    const int textWidth = fontMetrics.horizontalAdvance(elidedText);
    const bool centered = (alignment() & Qt::AlignHCenter) != 0;
    const int startX = centered ? std::max(0, (width() - textWidth - gap - squareSize) / 2) : 0;

    painter.setPen(m_keyTextColor);
    painter.drawText(QRect(startX, 0, textWidth + 2, height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            elidedText);

    // The square sits on the text's baseline: its bottom is level with the bottom of the letters
    // (the key names have no descenders), and it grows upwards from there.
    const int baseline = (height() - (fontMetrics.ascent() + fontMetrics.descent())) / 2 +
            fontMetrics.ascent();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(squareColor);
    painter.drawRoundedRect(QRectF(startX + textWidth + gap,
                                    baseline - squareSize,
                                    squareSize,
                                    squareSize),
            squareSize * 0.12,
            squareSize * 0.12);
}
