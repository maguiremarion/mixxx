#pragma once

#include <QDoubleSpinBox>
#include <QLineEdit>

#include "control/controlproxy.h"
#include "effects/defs.h"
#include "widget/wbasewidget.h"

class QDomNode;
class SkinContext;

class WBeatSpinBox : public QDoubleSpinBox, public WBaseWidget {
    Q_OBJECT
  public:
    WBeatSpinBox(QWidget* parent, const ConfigKey& configKey, int decimals = 5,
            double minimum = 0.03125, double maximum = 512.00);

    void setup(const QDomNode& node, const SkinContext& context);

    /// For a box that shows an effect's parameter (the Beat FX value): the box only makes sense for
    /// parameters measured in beats (Echo time, Flanger period). Another effect's first parameter
    /// (Balance, a BPM, a cutoff...) is shown as the plain value (a percentage for 0..1 amounts, with
    /// its unit otherwise) and steps by a few percent of its range instead, not
    /// as an odd raw number such as 0.28938.
    void setEffectParameterSlot(EffectParameterSlotBasePointer pParameterSlot);

  private slots:
    void updateUnitMode();
    void slotSpinboxValueChanged(double newValue);
    void slotControlValueChanged(double newValue);

  private:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& input, int& pos) const override;

    void stepBy(int steps) override;
    QString fractionString(int numerator, int denominator) const;

    ControlProxy m_valueControl;
    EffectParameterSlotBasePointer m_pParameterSlot;
    bool m_beatMode = true;
    bool m_noEffect = false;
    bool m_hideUnlessBeats = false;
    bool m_toggle = false;
    bool m_logScale = false;
    QString m_unitString;
    double m_rangeMin = 0.0;
    double m_rangeMax = 1.0;

    // for font scaling
    bool event(QEvent* pEvent) override;
    void keyPressEvent(QKeyEvent* pEvent) override;
    double m_scaleFactor;

    // <DragToChange>true</DragToChange>: on a touch screen the box has no arrows to tap, so a
    // finger dragged right or left on it steps the value (right = double, left = halve), one step
    // per kDragStepPixels. A left and a right arrow are drawn at the box's ends; a tap on one steps
    // down or up.
    void mousePressEvent(QMouseEvent* pEvent) override;
    void mouseMoveEvent(QMouseEvent* pEvent) override;
    void mouseReleaseEvent(QMouseEvent* pEvent) override;
    void paintEvent(QPaintEvent* pEvent) override;
    bool m_dragToChange = false;
    bool m_dragging = false;
    bool m_dragMoved = false;
    int m_arrowTapStep = 0;
    int m_dragLastX = 0;
    int m_dragAccum = 0;
};

// This is an inherited class that supports font scaling
class WBeatLineEdit : public QLineEdit {
    Q_OBJECT
  public:
    explicit WBeatLineEdit(QWidget* parent=0)
        : QLineEdit(parent),
          m_scaleFactor(1.0) {
    }

    void setScaleFactor(double scaleFactor) {
        m_scaleFactor = scaleFactor;
    }

  private:
    bool event(QEvent* pEvent) override;
    double m_scaleFactor;
};
