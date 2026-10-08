#pragma once

#include <QColor>
#include <QComboBox>

#include "effects/defs.h"
#include "widget/wbasewidget.h"

class EffectsManager;
class QDomNode;
class SkinContext;

class WEffectSelector : public QComboBox, public WBaseWidget {
    Q_OBJECT
  public:
    /// Colour of the effect name in the closed box (qproperty-textColor in the skin's qss).
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)
    QColor textColor() const {
        return m_textColor;
    }
    void setTextColor(const QColor& color) {
        m_textColor = color;
        update();
    }

    WEffectSelector(QWidget* pParent, EffectsManager* pEffectsManager);

    void setup(const QDomNode& node, const SkinContext& context);

    void showPopup() override;
    void hidePopup() override;

  protected:
    void paintEvent(QPaintEvent* pEvent) override;

  signals:
    void presetListVisibleChanged(bool visible);

  private slots:
    void slotEffectUpdated();
    void slotEffectSelected(int newIndex);
    void slotPresetListShowRequest(bool show);
    void populate();
    bool event(QEvent* pEvent) override;

  private:
    EffectsManager* m_pEffectsManager;
    VisibleEffectsListPointer m_pVisibleEffectsList;
    EffectSlotPointer m_pEffectSlot;
    QColor m_textColor;
};
