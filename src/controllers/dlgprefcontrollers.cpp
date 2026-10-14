#include "controllers/dlgprefcontrollers.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include "control/controlproxy.h"
#include "controllers/controller.h"
#include "controllers/controllermanager.h"
#include "controllers/defs_controllers.h"
#include "controllers/dlgprefcontroller.h"
#include "defs_urls.h"
#include "moc_dlgprefcontrollers.cpp"
#include "preferences/dialog/dlgpreferences.h"
#include "util/desktophelper.h"
#include "util/string.h"

namespace {
const QString kAppGroup = QStringLiteral("[App]");
} // namespace

DlgPrefControllers::DlgPrefControllers(DlgPreferences* pPreferences,
        UserSettingsPointer pConfig,
        std::shared_ptr<ControllerManager> pControllerManager,
        QTreeWidgetItem* pControllersRootItem)
        : DlgPreferencePage(pPreferences),
          m_pDlgPreferences(pPreferences),
          m_pConfig(pConfig),
          m_pControllerManager(pControllerManager),
          m_pControllersRootItem(pControllersRootItem),
          m_pNumDecks(make_parented<ControlProxy>(
                  kAppGroup, QStringLiteral("num_decks"), this)),
          m_pNumSamplers(make_parented<ControlProxy>(
                  kAppGroup, QStringLiteral("num_samplers"), this)) {
    setupUi(this);
    // Create text color for the cue mode link "?" to the manual
    createLinkColor();
    setupControllerWidgets();

    connect(btnOpenUserMappings, &QPushButton::clicked, this, [this]() {
        QString mappingsPath = userMappingsPath(m_pConfig);
        openLocalFile(mappingsPath);
    });

    // Connections
    connect(m_pControllerManager.get(),
            &ControllerManager::devicesChanged,
            this,
            &DlgPrefControllers::rescanControllers);

#ifdef __PORTMIDI__
    checkBox_midithrough->setChecked(m_pConfig->getValue(kMidiThroughCfgKey, false));
    connect(checkBox_midithrough,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefControllers::slotMidiThroughChanged);
    txt_midithrough->setTextFormat(Qt::RichText);
    //: text enclosed in <b> is bold, <br/> is a linebreak
    //: %1 is the placehodler for 'MIDI Through Port'
    txt_midithrough->setText(tr(
            "%1 is a virtual controller that allows to use e.g. the 'MIDI for light' "
            "mapping.<br/>"
            "You need to restart Mixxx in order to enable it.<br/>"
            "<b>Note:</b> mappings meant for physical controllers can cause issues and "
            "even render the Mixxx GUI unresponsive when being loaded to %1.")
                    .arg(kMidiThroughPortPrefix));
#else
    line_midithrough->hide();
    checkBox_midithrough->hide();
    txt_midithrough->hide();
#endif

    // Setting the description text here instead of in the ui file allows to paste
    // a formatted link (text color is a more readable blend of text color and original link color).
    txtMappingsOverview->setText(tr(
            "Mixxx uses \"mappings\" to connect messages from your controller to "
            "controls in Mixxx. If you do not see a mapping for your controller "
            "in the \"Load Mapping\" menu when you click on your controller on the "
            "left sidebar, you may be able to download one online from the %1. "
            "Place the XML (.xml) and Javascript (.js) file(s) in the \"User Mapping "
            "Folder\" then click the Reload button next to the mapping selector to "
            "reload all available mappings. If you download a mapping in a ZIP file, "
            "extract the XML and Javascript file(s) from the ZIP file to your "
            "\"User Mapping Folder\" then click the Reload button.")
                    .arg(coloredLinkString(
                            m_pLinkColor,
                            QStringLiteral("Mixxx Controller Forums"),
                            MIXXX_CONTROLLER_FORUMS_URL)));

    txtHardwareCompatibility->setText(coloredLinkString(
            m_pLinkColor,
            tr("Mixxx DJ Hardware Guide"),
            MIXXX_WIKI_HARDWARE_COMPATIBILITY_URL));

    txtControllerForums->setText(coloredLinkString(
            m_pLinkColor,
            QStringLiteral("Mixxx Controller Forums"),
            MIXXX_CONTROLLER_FORUMS_URL));

    txtControllerMappingFormat->setText(coloredLinkString(
            m_pLinkColor,
            tr("MIDI Mapping File Format"),
            MIXXX_WIKI_CONTROLLER_MAPPING_FORMAT_URL));

    txtControllerScripting->setText(coloredLinkString(
            m_pLinkColor,
            tr("MIDI Scripting with Javascript"),
            MIXXX_WIKI_MIDI_SCRIPTING_URL));

    if (DlgPreferences::kioskMode()) {
        applyKioskLayout();
    }
}

DlgPrefControllers::~DlgPrefControllers() {
    destroyControllerWidgets();
}

/// The kiosk's overview page: what a controller and a mapping are, and the mapping files. Plain
/// wording, no MIDI-through port, no links to the web; sizes follow dialogs.qss.
void DlgPrefControllers::applyKioskLayout() {
    auto makeText = [](const QString& text, const char* objectName, QWidget* pParent) {
        auto* pLabel = new QLabel(text, pParent);
        pLabel->setObjectName(QLatin1String(objectName));
        pLabel->setWordWrap(true);
        return pLabel;
    };

    auto* pContent = new QWidget(this);
    auto* pLayout = new QVBoxLayout(pContent);
    pLayout->setContentsMargins(28, 24, 28, 20);
    pLayout->setSpacing(0);

    pLayout->addWidget(makeText(tr("Controllers"), "PageHeading", pContent));
    pLayout->addSpacing(6);
    txtControllersOverview->setText(tr(
            "Plug a DJ controller into a USB port and it appears under \"Controllers\" in "
            "the list on the left. Select it there to switch it on and choose its mapping."));
    txtControllersOverview->setObjectName(QStringLiteral("PageText"));
    pLayout->addWidget(txtControllersOverview);
    pLayout->addSpacing(22);

    // Shown only when no controller was found (setupControllerWidgets()).
    auto* pNoneCard = new QFrame(pContent);
    pNoneCard->setObjectName(QStringLiteral("PrefCard"));
    auto* pNoneLayout = new QVBoxLayout(pNoneCard);
    pNoneLayout->setContentsMargins(20, 16, 20, 16);
    txtNoControllersAvailable->setText(tr(
            "No controller detected. Plug one in, then restart the app."));
    txtNoControllersAvailable->setObjectName(QStringLiteral("PageNotice"));
    txtNoControllersAvailable->setStyleSheet(QString());
    txtNoControllersAvailable->setFont(QFont());
    pNoneLayout->addWidget(txtNoControllersAvailable);
    m_pNoControllersCard = pNoneCard;
    pLayout->addWidget(pNoneCard);
    pLayout->addSpacing(14);

    auto* pFilesCard = new QFrame(pContent);
    pFilesCard->setObjectName(QStringLiteral("PrefCard"));
    auto* pFilesLayout = new QVBoxLayout(pFilesCard);
    pFilesLayout->setContentsMargins(20, 16, 20, 16);
    pFilesLayout->setSpacing(12);
    pFilesLayout->addWidget(makeText(tr("MAPPING FILES"), "SectionLabel", pFilesCard));
    txtMappingsOverview->setText(tr(
            "A mapping tells the player what each button, knob and fader on a controller "
            "does. To add one, put its .xml and .js files in the mapping files folder, then "
            "press the reload button next to the Mapping list on the controller's page."));
    txtMappingsOverview->setObjectName(QStringLiteral("PageText"));
    pFilesLayout->addWidget(txtMappingsOverview);
    btnOpenUserMappings->setText(tr("See available mapping files"));
    btnOpenUserMappings->setFixedHeight(44);
    pFilesLayout->addWidget(btnOpenUserMappings, 0, Qt::AlignLeft);
    pLayout->addWidget(pFilesCard);
    pLayout->addStretch(1);

    // Everything else of the stock page (MIDI-through port, "how to make a mapping", web
    // links) is left out.
    groupBox_overview->hide();
    groupBox_mappings->hide();
    groupBox_3->hide();
    gridLayout_3->removeItem(verticalSpacer);
    gridLayout_3->setContentsMargins(0, 0, 0, 0);
    gridLayout_3->addWidget(pContent, 3, 0);
    gridLayout_3->setRowStretch(3, 1);
}

void DlgPrefControllers::openLocalFile(const QString& file) {
    mixxx::DesktopHelper::openUrl(QUrl::fromLocalFile(file));
}

void DlgPrefControllers::slotUpdate() {
}

void DlgPrefControllers::slotCancel() {
    for (DlgPrefController* pControllerDlg : std::as_const(m_controllerPages)) {
        pControllerDlg->slotCancel();
    }
}

void DlgPrefControllers::slotApply() {
    for (DlgPrefController* pControllerDlg : std::as_const(m_controllerPages)) {
        pControllerDlg->slotApply();
    }
}

void DlgPrefControllers::slotResetToDefaults() {
    for (DlgPrefController* pControllerDlg : std::as_const(m_controllerPages)) {
        pControllerDlg->slotResetToDefaults();
    }
}

QUrl DlgPrefControllers::helpUrl() const {
    return QUrl(MIXXX_MANUAL_CONTROLLERS_URL);
}

bool DlgPrefControllers::handleTreeItemClick(QTreeWidgetItem* clickedItem) {
    int controllerIndex = m_controllerTreeItems.indexOf(clickedItem);
    if (controllerIndex >= 0) {
        DlgPrefController* pControllerDlg = m_controllerPages.value(controllerIndex);
        if (pControllerDlg) {
            const QString pageTitle = m_pControllersRootItem->text(0) + " - " +
                    clickedItem->text(0);
            m_pDlgPreferences->switchToPage(pageTitle, pControllerDlg);
        }
        return true;
    } else if (clickedItem == m_pControllersRootItem) {
        // Switch to the root page and expand the controllers tree item.
        m_pDlgPreferences->expandTreeItem(clickedItem);
        const QString pageTitle = clickedItem->text(0);
        m_pDlgPreferences->switchToPage(pageTitle, this);
        return true;
    }
    return false;
}

void DlgPrefControllers::rescanControllers() {
    destroyControllerWidgets();
    setupControllerWidgets();
}

void DlgPrefControllers::destroyControllerWidgets() {
    // NOTE: this assumes that the list of controllers does not change during the lifetime of Mixxx.
    // This is currently true, but once we support hotplug, we will need better lifecycle management
    // to keep this dialog and the controllermanager consistent.
    QList<Controller*> controllerList =
            m_pControllerManager->getControllerList(false, true);
    for (auto* pController : std::as_const(controllerList)) {
        pController->disconnect(this);
    }
    while (!m_controllerPages.isEmpty()) {
        DlgPrefController* pControllerDlg = m_controllerPages.takeLast();

        // this triggers the deletion of pControllerDlg, it no longer may be used
        m_pDlgPreferences->removePageWidget(pControllerDlg);
    }

    m_controllerTreeItems.clear();
    while (m_pControllersRootItem->childCount() > 0) {
        QTreeWidgetItem* pControllerTreeItem = m_pControllersRootItem->takeChild(0);
        delete pControllerTreeItem;
    }
}

void DlgPrefControllers::setupControllerWidgets() {
    // For each controller, create a dialog and put a little link to it in the
    // treepane on the left.
    QList<Controller*> controllerList =
            m_pControllerManager->getControllerList(false, true);
    if (DlgPreferences::kioskMode()) {
        // Only MIDI controllers are offered. A DJ controller often shows up twice (its MIDI port
        // and a second, HID, interface with its serial number in the name) and the second one
        // has no mapping and does nothing.
        controllerList.removeIf([](Controller* pController) {
            return pController->getDataRepresentationProtocol() != DataRepresentationProtocol::MIDI;
        });
    }
    if (controllerList.isEmpty()) {
        // If no controllers are available, show the "No controllers available" message.
        txtNoControllersAvailable->setVisible(true);
        if (m_pNoControllersCard) {
            m_pNoControllersCard->setVisible(true);
        }
        return;
    }
    txtNoControllersAvailable->setVisible(false);
    if (m_pNoControllersCard) {
        m_pNoControllersCard->setVisible(false);
    }

    std::sort(controllerList.begin(), controllerList.end(), controllerCompare);

    for (auto* pController : std::as_const(controllerList)) {
        auto pControllerDlg = make_parented<DlgPrefController>(
                this, pController, m_pControllerManager, m_pConfig);
        connect(pControllerDlg,
                &DlgPrefController::mappingStarted,
                m_pDlgPreferences,
                &DlgPreferences::hide);
        connect(pControllerDlg,
                &DlgPrefController::mappingEnded,
                m_pDlgPreferences,
                &DlgPreferences::show);
        // Recreate the control picker menus when decks or samplers are added
        m_pNumDecks->connectValueChanged(pControllerDlg.get(),
                &DlgPrefController::slotRecreateControlPickerMenu);
        m_pNumSamplers->connectValueChanged(pControllerDlg.get(),
                &DlgPrefController::slotRecreateControlPickerMenu);

        m_controllerPages.append(pControllerDlg);

        connect(pController,
                &Controller::openChanged,
                this,
                [this, pDlg = pControllerDlg.get()](bool bOpen) {
                    slotHighlightDevice(pDlg, bOpen);
                });

        QTreeWidgetItem* pControllerTreeItem = new QTreeWidgetItem(
                QTreeWidgetItem::Type);

        const QString treeImage = [protocol = pController->getDataRepresentationProtocol()] {
            switch (protocol) {
            case DataRepresentationProtocol::USB_BULK_TRANSFER:
                return QStringLiteral("ic_preferences_bulk.svg");
            case DataRepresentationProtocol::HID:
                return QStringLiteral("ic_preferences_hid.svg");
            case DataRepresentationProtocol::MIDI:
                return QStringLiteral("ic_preferences_midi.svg");
            default:
                return QStringLiteral("ic_preferences_controllers.svg");
            }
        }();

        m_pDlgPreferences->addPageWidget(
                DlgPreferences::PreferencesPage(pControllerDlg, pControllerTreeItem),
                pController->getName(),
                treeImage);

        m_pControllersRootItem->addChild(pControllerTreeItem);
        m_controllerTreeItems.append(pControllerTreeItem);

        // If controller is open make controller label bold
        QFont temp = pControllerTreeItem->font(0);
        temp.setBold(pController->isOpen());
        pControllerTreeItem->setFont(0, temp);
    }

    if (DlgPreferences::kioskMode()) {
        // A controller was found: show it in the list without a tap on "Controllers" first.
        m_pDlgPreferences->expandTreeItem(m_pControllersRootItem);
    }
}

void DlgPrefControllers::slotHighlightDevice(DlgPrefController* pControllerDlg, bool enabled) {
    int controllerPageIndex = m_controllerPages.indexOf(pControllerDlg);
    if (controllerPageIndex < 0) {
        return;
    }

    QTreeWidgetItem* pControllerTreeItem =
            m_controllerTreeItems.at(controllerPageIndex);
    if (!pControllerTreeItem) {
        return;
    }

    QFont temp = pControllerTreeItem->font(0);
    temp.setBold(enabled);
    pControllerTreeItem->setFont(0, temp);
}

#ifdef __PORTMIDI__
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefControllers::slotMidiThroughChanged(Qt::CheckState state) {
    m_pConfig->setValue(kMidiThroughCfgKey, state != Qt::Unchecked);
}
#else
void DlgPrefControllers::slotMidiThroughChanged(bool checked) {
    m_pConfig->setValue(kMidiThroughCfgKey, checked);
}
#endif
#endif
