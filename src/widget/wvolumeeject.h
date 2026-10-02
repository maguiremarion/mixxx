// WVolumeEject is a row of eject buttons, one per mounted external volume
// (USB sticks, SD cards, external drives). In skins it is the <VolumeEject> node.
//
// The set of volumes changes while Mixxx runs and a skin can't create widgets
// on demand, hence a real widget. It polls the mount table while visible and
// rebuilds its buttons when the list changes.

#pragma once

#include <QPointer>
#include <QStringList>
#include <QTimer>

#include "library/volumewatcher.h"
#include "widget/wwidget.h"

class PlayerManager;
class QDomNode;
class QHBoxLayout;
class QPushButton;
class SkinContext;

class WVolumeEject : public WWidget {
    Q_OBJECT
  public:
    WVolumeEject(QWidget* parent, PlayerManager* pPlayerManager);
    ~WVolumeEject() override = default;

    void setup(const QDomNode& node, const SkinContext& context);

  protected:
    void showEvent(QShowEvent* pEvent) override;

  private slots:
    void refresh();

  private:
    using Volume = VolumeWatcher::Volume;

    void eject(QPushButton* pButton, const Volume& volume);
    // Show a message on a button for a moment, then restore its label.
    void flash(QPushButton* pButton, const QString& text);

    PlayerManager* m_pPlayerManager;
    QHBoxLayout* m_pLayout;
    QTimer m_pollTimer;
    QStringList m_shownRoots;
};
