#include "widget/wkey.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QStylePainter>

#include "control/controlpushbutton.h"
#include "library/library_prefs.h"
#include "moc_wkey.cpp"
#include "preferences/usersettings.h"
#include "skin/legacy/skincontext.h"
#include "track/keyutils.h"

#include <algorithm>
#include <memory>

namespace {

const ConfigKey kShowCamelotKey(QStringLiteral("[Skin]"), QStringLiteral("key_show_camelot"));

// Creates the shared "show keys as 1A" toggle the first time a key label is built. It must exist
// before the labels' proxies are made (a proxy to a missing control stays invalid).
ConfigKey ensureShowCamelotControl() {
    // Deliberately never deleted. A static std::unique_ptr here was destroyed during process
    // exit, long after Mixxx had torn down its control registry, and crashed (segfault on every
    // quit). The OS reclaims the memory when the process ends.
    static ControlPushButton* s_pShowCamelot = nullptr;
    if (!s_pShowCamelot) {
        s_pShowCamelot = new ControlPushButton(kShowCamelotKey, /*persist*/ true);
        s_pShowCamelot->setButtonMode(mixxx::control::ButtonMode::Toggle);
    }
    return kShowCamelotKey;
}

} // namespace

WKey::WKey(const QString& group, UserSettingsPointer pConfig, QWidget* pParent)
        : WLabel(pParent),
          m_keyNotation(mixxx::library::prefs::kKeyNotationConfigKey, this),
          m_showCamelot(ensureShowCamelotControl(), this),
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
    m_showCamelot.connectValueChanged(this, [this](double) { setValue(); });
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
            keyStr = m_showCamelot.get() > 0.0
                    ? KeyUtils::keyToString(m_key, KeyUtils::KeyNotation::Lancelot)
                    : KeyUtils::keyToString(m_key);
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
    // A tap flips every key label between e.g. "Gm" and "1A".
    if (pEvent->button() == Qt::LeftButton) {
        m_showCamelot.set(m_showCamelot.get() > 0.0 ? 0.0 : 1.0);
        // A ControlProxy doesn't signal the object that made the change, so this label has to
        // refresh itself (any other key label hears about it through the signal).
        setValue();
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

    painter.setPen(option.palette.color(foregroundRole()));
    painter.drawText(QRect(startX, 0, textWidth + 2, height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            elidedText);

    // Centre the square on the capital letters, not on the row: the text is centred as a whole
    // line (ascent + descent), so its capitals sit a little above the middle of the row.
    const int baseline = (height() - (fontMetrics.ascent() + fontMetrics.descent())) / 2 +
            fontMetrics.ascent();
    const int squareCenterY = baseline - static_cast<int>(fontMetrics.capHeight() / 2);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(squareColor);
    painter.drawRoundedRect(QRectF(startX + textWidth + gap,
                                    squareCenterY - squareSize / 2.0,
                                    squareSize,
                                    squareSize),
            squareSize * 0.3,
            squareSize * 0.3);
}
