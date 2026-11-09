#include "bluetooth/dlgbluetooth.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "bluetooth/bluetoothsession.h"
#include "moc_dlgbluetooth.cpp"

namespace {
// Empties a layout for good: its widgets, and the widgets of the layouts inside it (a row holds a layout of layouts)
void clearLayout(QLayout* pLayout) {
    while (QLayoutItem* pItem = pLayout->takeAt(0)) {
        if (QWidget* pWidget = pItem->widget()) {
            pWidget->hide();
            pWidget->deleteLater();
        }
        if (QLayout* pChild = pItem->layout()) {
            clearLayout(pChild);
        }
        delete pItem;
    }
}

QLabel* makeLabel(const QString& text, const char* objectName, QWidget* pParent) {
    auto* pLabel = new QLabel(text, pParent);
    pLabel->setObjectName(QLatin1String(objectName));
    pLabel->setWordWrap(true);
    return pLabel;
}
} // namespace

DlgBluetooth::DlgBluetooth(QWidget* pParent)
        : QWidget(pParent) {
    // The Auto DJ page's names, so the skin's style sheet dresses this page the same way
    setObjectName(QStringLiteral("DlgAutoDJ"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* pRoot = new QVBoxLayout(this);
    pRoot->setContentsMargins(10, 4, 12, 12);
    pRoot->setSpacing(10);
    pRoot->addWidget(makeLabel(tr("Bluetooth"), "AutoDjTitle", this));
    pRoot->addWidget(makeLabel(
            tr("Pair a phone to play its music through the deck's audio output. The decks pause while it plays; "
               "the deck's volume knob sets the level."),
            "AutoDjText",
            this));

    auto* pPairRow = new QHBoxLayout();
    pPairRow->setSpacing(12);
    m_pPairButton = new QPushButton(tr("PAIR A PHONE"), this);
    m_pPairButton->setObjectName(QStringLiteral("AutoDjStartBtn"));
    m_pPairButton->setFocusPolicy(Qt::NoFocus);
    m_pPairButton->setFixedHeight(48);
    m_pPairButton->setMinimumWidth(240);
    m_pCancelButton = new QPushButton(tr("CANCEL"), this);
    m_pCancelButton->setObjectName(QStringLiteral("AutoDjBarBtn"));
    m_pCancelButton->setFocusPolicy(Qt::NoFocus);
    m_pCancelButton->setFixedSize(120, 40);
    pPairRow->addWidget(m_pPairButton);
    pPairRow->addWidget(m_pCancelButton);
    pPairRow->addStretch(1);
    pRoot->addLayout(pPairRow);
    // Only while pairing is open: what to look for, and for how long
    m_pPairText = makeLabel(QString(), "AutoDjCaption", this);
    pRoot->addWidget(m_pPairText);

    pRoot->addSpacing(8);
    m_pPhonesCaption = makeLabel(tr("PHONES"), "AutoDjCaption", this);
    pRoot->addWidget(m_pPhonesCaption);
    m_pDeviceRows = new QVBoxLayout();
    m_pDeviceRows->setSpacing(6);
    pRoot->addLayout(m_pDeviceRows);
    pRoot->addStretch(1);

    BluetoothSession* pSession = BluetoothSession::instance();
    if (pSession) {
        connect(m_pPairButton, &QPushButton::clicked, pSession, &BluetoothSession::pair);
        connect(m_pCancelButton, &QPushButton::clicked, pSession, &BluetoothSession::cancelPair);
        connect(pSession, &BluetoothSession::changed, this, &DlgBluetooth::refresh);
    }
    refresh();
}

void DlgBluetooth::onShow() {
    refresh();
}

void DlgBluetooth::refresh() {
    BluetoothSession* pSession = BluetoothSession::instance();
    if (!pSession || !pSession->helperAvailable()) {
        m_pPairButton->setEnabled(false);
        m_pCancelButton->hide();
        m_pPairText->setText(tr("Bluetooth is not available right now."));
        m_pPhonesCaption->hide();
        return;
    }
    const int left = pSession->pairSecondsLeft();
    const bool pairing = left > 0;
    m_pPairButton->setEnabled(!pairing);
    m_pPairButton->setText(pairing ? tr("SEARCHING...") : tr("PAIR A PHONE"));
    m_pCancelButton->setVisible(pairing);
    m_pPairText->setVisible(pairing);
    m_pPairText->setText(tr("Open Bluetooth settings on your phone and pick \"RVD(ix)\"   %1:%2")
                    .arg(left / 60)
                    .arg(left % 60, 2, 10, QLatin1Char('0')));

    // The phones, one row each: its name, "CONNECTED" in green when it is, and one button (DISCONNECT for the
    // phone that is connected, FORGET for the others)
    clearLayout(m_pDeviceRows);
    const auto devices = pSession->devices();
    bool any = false;
    for (const BluetoothSession::Device& device : devices) {
        if (!device.paired) {
            continue;
        }
        any = true;
        // One row per phone: its name with CONNECTED beside it (level with it), a line of detail under that, and
        // one button at the right (DISCONNECT for the phone that is connected, FORGET for the others)
        auto* pRow = new QHBoxLayout();
        pRow->setSpacing(10);
        auto* pText = new QVBoxLayout();
        pText->setSpacing(2);
        auto* pNameLine = new QHBoxLayout();
        pNameLine->setSpacing(12);
        auto* pName = makeLabel(device.name, "AutoDjText", this);
        pName->setWordWrap(false);
        pNameLine->addWidget(pName, 0, Qt::AlignVCenter);
        if (device.connected) {
            auto* pState = makeLabel(tr("CONNECTED"), "AutoDjLive", this);
            pState->setWordWrap(false);
            pNameLine->addWidget(pState, 0, Qt::AlignVCenter);
        }
        pNameLine->addStretch(1);
        pText->addLayout(pNameLine);
        QString detail = tr("Not connected");
        if (device.connected) {
            if (pSession->playing() && !pSession->title().isEmpty()) {
                detail = pSession->artist().isEmpty()
                        ? tr("Playing: %1").arg(pSession->title())
                        : tr("Playing: %1 - %2").arg(pSession->title(), pSession->artist());
            } else if (pSession->playing()) {
                detail = tr("Playing");
            } else {
                detail = pSession->title().isEmpty() ? tr("Ready") : tr("Paused: %1").arg(pSession->title());
            }
        }
        auto* pDetail = makeLabel(detail, "AutoDjCaption", this);
        pDetail->setWordWrap(false);
        pText->addWidget(pDetail);
        pRow->addLayout(pText, 1);
        const QString address = device.address;
        auto* pButton = new QPushButton(device.connected ? tr("DISCONNECT") : tr("FORGET"), this);
        pButton->setObjectName(device.connected ? QStringLiteral("AutoDjBarBtn") : QStringLiteral("AutoDjEndBtn"));
        pButton->setFocusPolicy(Qt::NoFocus);
        pButton->setFixedSize(140, 40);
        if (device.connected) {
            connect(pButton, &QPushButton::clicked, pSession, &BluetoothSession::disconnectDevice);
        } else {
            connect(pButton, &QPushButton::clicked, pSession, [pSession, address]() {
                pSession->forget(address);
            });
        }
        pRow->addWidget(pButton, 0, Qt::AlignVCenter);
        m_pDeviceRows->addLayout(pRow);
    }
    m_pPhonesCaption->setVisible(any);
}
