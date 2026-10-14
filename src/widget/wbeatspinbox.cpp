#include "widget/wbeatspinbox.h"

#include <algorithm>
#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>
#include <QSignalBlocker>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "effects/backends/effectmanifestparameter.h"
#include "effects/effectparameterslotbase.h"
#include "moc_wbeatspinbox.cpp"
#include "skin/legacy/skincontext.h"
#include "util/math.h"

namespace {
const QRegularExpression kBlockListRegex(QStringLiteral("[^0-9.,/ ]"));
constexpr int kDragStepPixels = 24;
constexpr int kArrowZonePixels = 30; // width of the tap area at each end of the box
} // namespace

WBeatSpinBox::WBeatSpinBox(QWidget* parent,
        const ConfigKey& configKey,
        int decimals,
        double minimum,
        double maximum)
        : QDoubleSpinBox(parent),
          WBaseWidget(this),
          m_valueKey(configKey),
          m_valueControl(configKey, this, ControlFlag::NoAssertIfMissing),
          m_scaleFactor(1.0) {
    // replace the original QLineEdit by one that supports font scaling.
    setLineEdit(new WBeatLineEdit(this));
    setDecimals(decimals);
    setMinimum(minimum);
    setMaximum(maximum);
    setKeyboardTracking(false);
    // Prevent this widget from getting focused with Tab
    // to avoid interfering with using the library via keyboard.
    setFocusPolicy(Qt::ClickFocus);
    // This is necessary to also ignore Shift+Tab (Qt::BacktabFocusReason).
    lineEdit()->setFocusPolicy(Qt::ClickFocus);

    setValue(m_valueControl.get());
    connect(this,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &WBeatSpinBox::slotSpinboxValueChanged);
    m_valueControl.connectValueChanged(this, &WBeatSpinBox::slotControlValueChanged);
}

void WBeatSpinBox::setEffectParameterSlot(EffectParameterSlotBasePointer pParameterSlot) {
    m_pParameterSlot = pParameterSlot;
    if (m_pParameterSlot) {
        connect(m_pParameterSlot.data(),
                &EffectParameterSlotBase::updated,
                this,
                &WBeatSpinBox::updateUnitMode);
    }
    updateUnitMode();
}

void WBeatSpinBox::updateUnitMode() {
    bool beatMode = true;
    // No effect loaded in the slot: the box shows "---" like the name above it, not a value that
    // belongs to nothing
    m_noEffect = m_pParameterSlot && !m_pParameterSlot->isLoaded();
    if (m_pParameterSlot) {
        const EffectManifestParameterPointer pManifest = m_pParameterSlot->getManifest();
        if (pManifest) {
            const auto hint = pManifest->unitsHint();
            // The unit hint is not always set (Flanger's speed, Auto Pan's period are beats too),
            // so the parameters known to be beat lengths are named here.
            const QString id = pManifest->id();
            beatMode = hint == EffectManifestParameter::UnitsHint::Beats ||
                    hint == EffectManifestParameter::UnitsHint::Beat ||
                    id == QStringLiteral("delay_time") ||
                    id == QStringLiteral("lfo_period") ||
                    id == QStringLiteral("speed") ||
                    id == QStringLiteral("period");
            if (!beatMode) {
                m_rangeMin = pManifest->getMinimum();
                m_rangeMax = pManifest->getMaximum();
                if (m_rangeMax <= m_rangeMin) {
                    m_rangeMax = m_rangeMin + 1.0;
                }
                m_unitString = pManifest->unitString();
                const auto scaler = pManifest->valueScaler();
                m_toggle = scaler == EffectManifestParameter::ValueScaler::Toggle;
                m_logScale = (scaler == EffectManifestParameter::ValueScaler::Logarithmic ||
                                     scaler == EffectManifestParameter::ValueScaler::LogarithmicInverse) &&
                        m_rangeMin > 0.0;
            }
        }
    }
    // A beat length that is linked to the effect's depth knob (Auto Pan's period is, inverted) would
    // just follow DEPTH, and the "default" would be whatever DEPTH happens to be (3.0697 for a
    // depth of 0.23). Unlink it, and give it its own default.
    if (beatMode && m_dragToChange && m_pParameterSlot && m_pParameterSlot->isLoaded()) {
        ControlProxy linkType(
                ConfigKey(m_valueKey.group, m_valueKey.item + QStringLiteral("_link_type")),
                this,
                ControlFlag::NoAssertIfMissing);
        if (linkType.valid() && linkType.get() != 0.0) {
            linkType.set(0.0);
            if (const auto pManifest = m_pParameterSlot->getManifest()) {
                m_valueControl.set(pManifest->getDefault());
            }
        }
    }
    const QSignalBlocker blocker(this);
    m_beatMode = beatMode;
    if (beatMode) {
        setDecimals(5);
        setMinimum(0.03125);
        setMaximum(512.00);
    } else {
        setDecimals(4);
        setMinimum(m_rangeMin);
        setMaximum(m_rangeMax);
    }
    setValue(m_valueControl.get());
    lineEdit()->setText(textFromValue(value()));
    if (m_hideUnlessBeats && parentWidget()) {
        parentWidget()->setVisible(m_beatMode || m_noEffect);
    }
}

void WBeatSpinBox::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    m_scaleFactor = context.getScaleFactor();
    qobject_cast<WBeatLineEdit*>(lineEdit())->setScaleFactor(m_scaleFactor);

    m_dragToChange = context.selectBool(node, QStringLiteral("DragToChange"), false);
    // <HideUnlessBeats>true</HideUnlessBeats>: the group this box sits in is hidden while the shown
    // effect has no beat-length parameter (with no effect loaded it stays, showing "---").
    m_hideUnlessBeats = context.selectBool(node, QStringLiteral("HideUnlessBeats"), false);
    if (m_dragToChange) {
        // The text field would take the finger itself: let the box see the touches instead.
        lineEdit()->setAttribute(Qt::WA_TransparentForMouseEvents);
        lineEdit()->setReadOnly(true);
    }
}

void WBeatSpinBox::paintEvent(QPaintEvent* pEvent) {
    QDoubleSpinBox::paintEvent(pEvent);
    if (!m_dragToChange) {
        return;
    }
    // A left and a right arrow at the ends of the box (they also say "drag me sideways").
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x8c, 0x8c, 0x94));
    const double h = 5.0 * m_scaleFactor; // half the arrow's height
    const double w = 6.0 * m_scaleFactor;
    const double cy = height() / 2.0;
    const double margin = 6.0 * m_scaleFactor;
    painter.drawPolygon(QPolygonF({QPointF(margin, cy), QPointF(margin + w, cy - h), QPointF(margin + w, cy + h)}));
    const double right = width() - margin;
    painter.drawPolygon(QPolygonF({QPointF(right, cy), QPointF(right - w, cy - h), QPointF(right - w, cy + h)}));
}

void WBeatSpinBox::mousePressEvent(QMouseEvent* pEvent) {
    if (m_dragToChange && pEvent->button() == Qt::LeftButton) {
        const int x = pEvent->position().toPoint().x();
        // A tap on an arrow steps once at once; the rest of the box is for dragging. (A drag that
        // starts on an arrow still drags, and the step is only taken on release if it did not.)
        m_arrowTapStep = x < kArrowZonePixels ? -1 : (x > width() - kArrowZonePixels ? 1 : 0);
        m_dragMoved = false;
        m_dragging = true;
        m_dragLastX = pEvent->position().toPoint().x();
        m_dragAccum = 0;
        pEvent->accept();
        return;
    }
    QDoubleSpinBox::mousePressEvent(pEvent);
}

void WBeatSpinBox::mouseMoveEvent(QMouseEvent* pEvent) {
    if (!m_dragging) {
        QDoubleSpinBox::mouseMoveEvent(pEvent);
        return;
    }
    const int x = pEvent->position().toPoint().x();
    if (std::abs(x - m_dragLastX) > 0) {
        m_dragMoved = m_dragMoved || std::abs(m_dragAccum + x - m_dragLastX) >= 6;
    }
    m_dragAccum += x - m_dragLastX; // right is positive
    m_dragLastX = x;
    while (m_dragAccum >= kDragStepPixels) {
        stepBy(1);
        m_dragAccum -= kDragStepPixels;
    }
    while (m_dragAccum <= -kDragStepPixels) {
        stepBy(-1);
        m_dragAccum += kDragStepPixels;
    }
    pEvent->accept();
}

void WBeatSpinBox::mouseReleaseEvent(QMouseEvent* pEvent) {
    if (m_dragging) {
        m_dragging = false;
        if (!m_dragMoved && m_arrowTapStep != 0) {
            stepBy(m_arrowTapStep); // a tap on an arrow
        }
        pEvent->accept();
        return;
    }
    QDoubleSpinBox::mouseReleaseEvent(pEvent);
}

namespace {
// The values a drag steps through: each power of two and the one half way to the next (a "dotted"
// value, 1.5 times), so 4, 6, 8, 12, 16... instead of 4, 8, 16: doubling was too big a jump.
const QList<double>& dragSteps() {
    static const QList<double> steps = [] {
        QList<double> list;
        for (double base = 1.0 / 16.0; base <= 256.0; base *= 2.0) {
            list << base << base * 1.5;
        }
        return list;
    }();
    return steps;
}
} // namespace

void WBeatSpinBox::stepBy(int steps) {
    if (m_noEffect) {
        return;
    }
    if (m_dragToChange && !m_beatMode) {
        const double current = m_valueControl.get();
        const double span = m_rangeMax - m_rangeMin;
        double target;
        if (m_toggle) {
            target = current > 0.5 ? m_rangeMin : m_rangeMax;
        } else if (m_logScale) {
            target = current * std::pow(1.12, steps); // a cutoff in Hz: by ratios
        } else {
            double stepSize = span * 0.04;
            if (span >= 20.0) {
                stepSize = std::max(1.0, std::round(stepSize)); // whole BPM and the like
            }
            target = current + steps * stepSize;
            if (span >= 20.0) {
                target = std::round(target);
            }
        }
        target = std::clamp(target, m_rangeMin, m_rangeMax);
        m_valueControl.set(target);
        const QSignalBlocker blocker(this);
        setValue(m_valueControl.get());
        return;
    }
    if (m_dragToChange) {
        // Move along the list from the value the control has now (not the text: it may be a
        // moment behind), then show what the control ended up with.
        const QList<double>& list = dragSteps();
        const double current = m_valueControl.get();
        int nearest = 0;
        for (int i = 1; i < list.size(); ++i) {
            if (std::abs(list[i] - current) < std::abs(list[nearest] - current)) {
                nearest = i;
            }
        }
        const int target = std::clamp(nearest + steps, 0, static_cast<int>(list.size()) - 1);
        m_valueControl.set(list[target]);
        // A ControlProxy does not report a change it made itself, so the box would keep the old
        // number: set it ourselves (without sending the value back).
        const QSignalBlocker blocker(this);
        setValue(m_valueControl.get());
        return;
    }
    double oldValue = m_valueControl.get();
    double newValue;
    QString temp = text();
    int cursorPos = lineEdit()->cursorPosition();
    if (validate(temp, cursorPos) == QValidator::Acceptable) {
        newValue = valueFromText(temp) * pow(2, steps);
    } else {
        // here we have an unacceptable edit, going back to the old value first
        newValue = oldValue;
    }
    // Do not call QDoubleSpinBox::setValue directly in case
    // the new value of the ControlObject needs to be confirmed.
    m_valueControl.set(newValue);
    selectAll();
}

void WBeatSpinBox::slotSpinboxValueChanged(double newValue) {
    // Do not call QDoubleSpinBox::setValue directly in case
    // the new value of the ControlObject needs to be confirmed.
    m_valueControl.set(newValue);
}

void WBeatSpinBox::slotControlValueChanged(double newValue) {
    if (value() != newValue) {
        setValue(newValue);
    }
}

QString WBeatSpinBox::fractionString(int numerator, int denominator) const {
    return QString("%1/%2").arg(numerator).arg(denominator);
}

QString WBeatSpinBox::textFromValue(double value) const {
    if (m_noEffect) {
        return kNoEffectString;
    }
    if (!m_beatMode) {
        if (m_toggle) {
            return value > 0.5 ? tr("On") : tr("Off");
        }
        const double span = m_rangeMax - m_rangeMin;
        if (m_unitString.isEmpty() && span <= 2.5 && m_rangeMin >= -1.0 && m_rangeMax <= 1.0) {
            // an amount from 0 to 1 (or -1 to 1): as a percentage
            const int percent = qRound(value * 100.0);
            return (m_rangeMin < 0.0 && percent > 0 ? QStringLiteral("+") : QString()) +
                    QString::number(percent) + QStringLiteral("%");
        }
        // a real quantity (BPM, Hz, dB, ...): the number, with its unit
        const double a = std::abs(value);
        QString number;
        if (m_unitString == QStringLiteral("Hz") && a >= 1000.0) {
            number = QString::number(value / 1000.0, 'f', 1) + QStringLiteral(" k");
        } else {
            number = QString::number(value, 'f', a >= 100.0 ? 0 : (a >= 10.0 ? 1 : 2));
            if (number.contains(QChar('.'))) {
                while (number.endsWith(QChar('0'))) {
                    number.chop(1);
                }
                if (number.endsWith(QChar('.'))) {
                    number.chop(1);
                }
            }
        }
        return m_unitString.isEmpty() ? number : number + QChar(' ') + m_unitString;
    }
    double dWholePart, dFracPart;
    dFracPart = modf(value, &dWholePart);

    QString sFracPart;
    if (dFracPart == 0.5) {
        sFracPart = fractionString(1, 2);
    } else if (dFracPart == 0.25) {
        sFracPart = fractionString(1, 4);
    } else if (dFracPart == 0.75) {
        sFracPart = fractionString(3, 4);
    } else if (dFracPart == 0.33333) {
        sFracPart = fractionString(1, 3);
    } else if (dFracPart == 0.66667) {
        sFracPart = fractionString(2, 3);
    } else if (dFracPart == 0.125) {
        sFracPart = fractionString(1, 8);
    } else if (dFracPart == 0.375) {
        sFracPart = fractionString(3, 8);
    } else if (dFracPart == 0.625) {
        sFracPart = fractionString(5, 8);
    } else if (dFracPart == 0.875) {
        sFracPart = fractionString(7, 8);
    } else if (dFracPart == 0.0625) {
        sFracPart = fractionString(1, 16);
    } else if (dFracPart == 0.1875) {
        sFracPart = fractionString(3, 16);
    } else if (dFracPart == 0.3125) {
        sFracPart = fractionString(5, 16);
    } else if (dFracPart == 0.4375) {
        sFracPart = fractionString(7, 16);
    } else if (dFracPart == 0.5625) {
        sFracPart = fractionString(9, 16);
    } else if (dFracPart == 0.6875) {
        sFracPart = fractionString(11, 16);
    } else if (dFracPart == 0.8125) {
        sFracPart = fractionString(13, 16);
    } else if (dFracPart == 0.9375) {
        sFracPart = fractionString(15, 16);
    } else if (dFracPart == 0.03125) {
        sFracPart = fractionString(1, 32);
    } else if (dFracPart == 0.09375) {
        sFracPart = fractionString(3, 32);
    } else if (dFracPart == 0.15625) {
        sFracPart = fractionString(5, 32);
    } else if (dFracPart == 0.21875) {
        sFracPart = fractionString(7, 32);
    } else if (dFracPart == 0.28125) {
        sFracPart = fractionString(9, 32);
    } else if (dFracPart == 0.34375) {
        sFracPart = fractionString(11, 32);
    } else if (dFracPart == 0.40625) {
        sFracPart = fractionString(13, 32);
    } else if (dFracPart == 0.46875) {
        sFracPart = fractionString(15, 32);
    } else if (dFracPart == 0.53125) {
        sFracPart = fractionString(17, 32);
    } else if (dFracPart == 0.59375) {
        sFracPart = fractionString(19, 32);
    } else if (dFracPart == 0.65625) {
        sFracPart = fractionString(21, 32);
    } else if (dFracPart == 0.71875) {
        sFracPart = fractionString(23, 32);
    } else if (dFracPart == 0.78125) {
        sFracPart = fractionString(25, 32);
    } else if (dFracPart == 0.84375) {
        sFracPart = fractionString(27, 32);
    } else if (dFracPart == 0.90625) {
        sFracPart = fractionString(29, 32);
    } else if (dFracPart == 0.96875) {
        sFracPart = fractionString(31, 32);
    } else {
        return locale().toString(value, 'g', 5);
    }

    if (dWholePart > 0) {
        return locale().toString(dWholePart, 'f', 0) + " " + sFracPart;
    }
    return sFracPart;
}

double WBeatSpinBox::valueFromText(const QString& text) const {
    if (text.count(" ") > 1 || text.count("/") > 1) {
        return value();
    }

    bool conversionWorked = false;
    double dValue;
    dValue = locale().toDouble(text, &conversionWorked);
    if (conversionWorked) {
        return dValue;
    }

    QString sIntPart, sFracPart, sNumerator, sDenominator;

    if (text.contains(" ")) {
        QStringList numberParts = text.split(" ");
        sIntPart = numberParts.at(0);
        sFracPart = numberParts.at(1);
    } else if (text.contains("/")) {
        sFracPart = text;
    }

    QStringList splitFraction = sFracPart.split("/");
    sNumerator = splitFraction.at(0);
    sDenominator = splitFraction.at(1);

    return locale().toDouble(sIntPart)
            + locale().toDouble(sNumerator) / locale().toDouble(sDenominator);
}

QValidator::State WBeatSpinBox::validate(QString& input, int& pos) const {
    Q_UNUSED(pos);
    QRegularExpressionMatch blockListMatch = kBlockListRegex.match(input);
    if (blockListMatch.hasMatch()) {
        return QValidator::Invalid;
    }
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }

    if (input.count(" ") > 1 || input.count("/") > 1) {
        return QValidator::Invalid;
    }

    bool conversionWorked = false;
    locale().toDouble(input, &conversionWorked);
    if (conversionWorked) {
        return QValidator::Acceptable;
    }

    QString sIntPart, sFracPart, sNumerator, sDenominator;

    if (input.contains(" ")) {
        // Whole number + fraction, for example "1 1/2"
        QStringList numberParts = input.split(" ");
        sIntPart = numberParts.at(0);
        sFracPart = numberParts.at(1);
        // Whole number + trailing space
        if (sFracPart.isEmpty()) {
            return QValidator::Intermediate;
        }
    } else if (input.contains("/")) {
        // Fraction without whole number, for example "1/2"
        sFracPart = input;
    }

    if (!sIntPart.isEmpty()) {
        conversionWorked = false;
        sIntPart.toInt(&conversionWorked);
        if (!conversionWorked) {
            return QValidator::Invalid;
        }
    }

    if (!sFracPart.contains("/")) {
        return QValidator::Intermediate;
    }
    QStringList fracParts = sFracPart.split("/");
    sNumerator = fracParts.at(0);
    if (sNumerator.isEmpty()) {
        // when deleting the numerator, for example "/32"
        return QValidator::Intermediate;
    }
    sDenominator = fracParts.at(1);

    conversionWorked = false;
    sNumerator.toInt(&conversionWorked);
    if (!conversionWorked) {
        return QValidator::Invalid;
    }

    if (sDenominator.isEmpty()) {
        return QValidator::Intermediate;
    }
    conversionWorked = false;
    sDenominator.toInt(&conversionWorked);
    if (!conversionWorked) {
        return QValidator::Invalid;
    }

    return QValidator::Acceptable;
}

bool WBeatSpinBox::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::FontChange) {
        const QFont& fonti = font();
        // Change the new font on the fly by casting away its constancy
        // using setFont() here, would results into a recursive loop
        // resetting the font to the original css values.
        // Only scale pixel size fonts, point size fonts are scaled by the OS
        // This font instance is only used for size measuring in
        // QAbstractSpinBox::minimumSizeHint() the lineEdit()->font() is used for
        // rendering
        if (fonti.pixelSize() > 0) {
            const_cast<QFont&>(fonti).setPixelSize(
                    static_cast<int>(fonti.pixelSize() * m_scaleFactor));
        }
    }
    return QDoubleSpinBox::event(pEvent);
}

void WBeatSpinBox::keyPressEvent(QKeyEvent* pEvent) {
    // By default, Return & Enter keys apply the current value.
    // Additionally, move focus back to the previously focused library widget.
    if (pEvent->key() == Qt::Key_Return ||
            pEvent->key() == Qt::Key_Enter ||
            pEvent->key() == Qt::Key_Escape) {
        QDoubleSpinBox::keyPressEvent(pEvent);
        ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
        return;
    }
    QDoubleSpinBox::keyPressEvent(pEvent);
}

bool WBeatLineEdit::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::FontChange) {
        const QFont& fonti = font();
        // Change the new font on the fly by casting away its constancy
        // using setFont() here, would results into a recursive loop
        // resetting the font to the original css values.
        // Only scale pixel size fonts, point size fonts are scaled by the OS
        // This font instance is the one, used for rendering.
        if (fonti.pixelSize() > 0) {
            const_cast<QFont&>(fonti).setPixelSize(
                    static_cast<int>(fonti.pixelSize() * m_scaleFactor));
        }
    }
    return QLineEdit::event(pEvent);
}
