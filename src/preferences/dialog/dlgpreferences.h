#pragma once

#include <QDialog>
#include <QIcon>
#include <QDir>
#include <QEvent>
#include <QRect>
#include <QStringList>
#include <memory>

#include "control/controlpushbutton.h"
#include "preferences/constants.h"
#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgpreferencesdlg.h"
#include "preferences/settingsmanager.h"
#include "preferences/usersettings.h"

class ControllerManager;
class DlgPrefControllers;
class DlgPrefSound;
class EffectsManager;
class Library;
class SoundManager;
class VinylControlManager;

namespace mixxx {
class ScreensaverManager;
namespace skin {
class SkinLoader;
} // namespace skin
} // namespace mixxx

class DlgPreferences : public QDialog, public Ui::DlgPreferencesDlg {
    Q_OBJECT
  public:
    struct PreferencesPage {
        PreferencesPage() {
        }
        PreferencesPage(DlgPreferencePage* pDlg, QTreeWidgetItem* pTreeItem)
                : pDlg(pDlg),
                  pTreeItem(pTreeItem) {
        }

        DlgPreferencePage* pDlg;
        QTreeWidgetItem* pTreeItem;
        QString iconFile;
    };

    DlgPreferences(
            std::shared_ptr<mixxx::ScreensaverManager> pScreensaverManager,
            std::shared_ptr<mixxx::skin::SkinLoader> pSkinLoader,
            std::shared_ptr<SoundManager> pSoundManager,
            std::shared_ptr<ControllerManager> pControllerManager,
            std::shared_ptr<VinylControlManager> pVCManager,
            std::shared_ptr<EffectsManager> pEffectsManager,
            std::shared_ptr<SettingsManager> pSettingsManager,
            std::shared_ptr<Library> pLibrary,
            bool includeWaveformPreferences = true);
    virtual ~DlgPreferences();

    void addPageWidget(const PreferencesPage& page,
            const QString& pageTitle,
            const QString& iconFile);
    void removePageWidget(DlgPreferencePage* pWidget);
    void expandTreeItem(QTreeWidgetItem* pItem);
    void switchToPage(const QString& pageTitle, DlgPreferencePage* pPage);

  public slots:
    void changePage(QTreeWidgetItem* pCurrent, QTreeWidgetItem* pPrevious);
    void showSoundHardwarePage(
            std::optional<mixxx::preferences::SoundHardwareTab> tab =
                    std::nullopt);
    void showSoundHardwareInputPage();
    void slotButtonPressed(QAbstractButton* pButton);
    void handleButtonRole(QDialogButtonBox::ButtonRole role);
  signals:
    void closeDlg();
    void showDlg();

    // Emitted just after the user clicks Apply or OK.
    void applyPreferences();
    // Emitted if the user clicks Cancel
    void cancelPreferences();
    // Emitted if the user clicks Reset to Defaults.
    void resetToDefaults();

    void reloadUserInterface();
    void tooltipModeChanged(mixxx::preferences::Tooltips tooltipMode);
    void menuBarAutoHideChanged();

  public:
    /// On the kiosk (see kioskPreferencesEnabled()) the dialog opens fullscreen: a small window is
    /// hard to use on the touch screen.
    void setVisible(bool visible) override;

    /// True on the kiosk (see setVisible()): a short, user-facing set of pages with plain wording.
    static bool kioskMode();

    /// The icon at `path` painted solid white (the kiosk's pages are dark; some of Qt's own icon
    /// states and the stock icon files came out black on them).
    static QIcon whiteIcon(const QString& path, int size = 32);

  protected:
    bool eventFilter(QObject*, QEvent*) override;
    void changeEvent(QEvent* pEvent) override;
    void moveEvent(QMoveEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

  private:
    DlgPreferencePage* currentPage();
    void fixSliderStyle();
    QList<PreferencesPage> m_allPages;
    void onShow();
    void useKioskButtons();
    void onHide();
    QRect getDefaultGeometry();

    QAbstractButton* m_pApplyButton;
    QAbstractButton* m_pAcceptButton;

    QStringList m_geometry;
    UserSettingsPointer m_pConfig;
    std::unique_ptr<DlgPrefSound> m_pSoundDlg;
    PreferencesPage m_soundPage;
    DlgPrefControllers* m_pControllersDlg;

    QSize m_pageSizeHint;

    QDir m_iconsPath;
    bool pendingConfigValidOnAllPages();
};
