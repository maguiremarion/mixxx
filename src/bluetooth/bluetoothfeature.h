#pragma once

#include "library/libraryfeature.h"
#include "preferences/usersettings.h"

class TreeItemModel;

// Browse > Bluetooth (only when MIXXX_BLUETOOTH=1, i.e. on the Pi): pair a phone, who is connected.
class BluetoothFeature final : public LibraryFeature {
    Q_OBJECT
  public:
    BluetoothFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~BluetoothFeature() override = default;

    QVariant title() override;
    void bindLibraryWidget(WLibrary* pLibraryWidget, KeyboardEventFilter* pKeyboard) override;
    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;

  private:
    TreeItemModel* m_pSidebarModel;
};
