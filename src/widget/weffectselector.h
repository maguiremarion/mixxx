#pragma once

#include <QColor>
#include <QComboBox>
#include <QTimer>

#include "effects/defs.h"
#include "widget/wbasewidget.h"

class EffectsManager;
class QListWidget;
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
    bool eventFilter(QObject* pObj, QEvent* pEvent) override;

  protected:
    void paintEvent(QPaintEvent* pEvent) override;

  signals:
    void presetListVisibleChanged(bool visible);

  private slots:
    void slotEffectUpdated();
    void slotEffectSelected(int newIndex);
    void slotPresetListShowRequest(bool show);
    void slotPresetPickRequest(int direction);
    void commitPick();
    void populate();
    bool event(QEvent* pEvent) override;

  private:
    EffectsManager* m_pEffectsManager;
    VisibleEffectsListPointer m_pVisibleEffectsList;
    EffectSlotPointer m_pEffectSlot;
    QColor m_textColor;

    // The effect list (see weffectselector.cpp), also stepped from a controller (FX SELECT): the effect is loaded when no press
    // has come for kPickCommitMs.
    QListWidget* m_pList = nullptr;
    QTimer m_pickTimer;
    bool m_controllerDriven = false;
    int m_indexBefore = -1;
    bool isListOpen() const;
    void openList(int highlightRow);
    void closeList(bool cancel);
};
