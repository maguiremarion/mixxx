#include "bluetooth/bluetoothfeature.h"

#include "bluetooth/dlgbluetooth.h"
#include "library/treeitemmodel.h"
#include "moc_bluetoothfeature.cpp"
#include "widget/wlibrary.h"

namespace {
const QString kViewName = QStringLiteral("Bluetooth");
} // namespace

BluetoothFeature::BluetoothFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("bluetooth")),
          m_pSidebarModel(new TreeItemModel(this)) {
}

QVariant BluetoothFeature::title() {
    return QVariant(tr("Bluetooth"));
}

TreeItemModel* BluetoothFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void BluetoothFeature::bindLibraryWidget(WLibrary* pLibraryWidget, KeyboardEventFilter*) {
    // The view is owned (and deleted) by the library widget
    pLibraryWidget->registerView(kViewName, new DlgBluetooth(pLibraryWidget));
}

void BluetoothFeature::activate() {
    emit switchToView(kViewName);
    emit disableSearch();
    emit enableCoverArtDisplay(false);
}
