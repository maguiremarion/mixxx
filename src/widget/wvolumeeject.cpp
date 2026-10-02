#include "widget/wvolumeeject.h"

#include <QHBoxLayout>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>

#include "mixer/basetrackplayer.h"
#include "mixer/playermanager.h"
#include "moc_wvolumeeject.cpp"
#include "track/track.h"

namespace {

constexpr int kPollIntervalMs = 2000;
constexpr int kMaxNameLength = 12;
constexpr int kMessageDurationMs = 2500;

QString shortName(const QString& name) {
    QString result = name.toUpper();
    if (result.size() > kMaxNameLength) {
        result = result.left(kMaxNameLength - 1) + QChar(0x2026); // ellipsis
    }
    return result;
}

#ifndef Q_OS_MACOS
// "/dev/sda1" -> "/dev/sda", "/dev/mmcblk0p1" -> "/dev/mmcblk0"
QString wholeDiskDevice(const QString& partition) {
    static const QRegularExpression kPartitioned(
            QStringLiteral("^(/dev/(?:nvme\\d+n\\d+|mmcblk\\d+))p\\d+$"));
    static const QRegularExpression kNumbered(QStringLiteral("^(/dev/[a-z]+)\\d+$"));
    auto match = kPartitioned.match(partition);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    match = kNumbered.match(partition);
    return match.hasMatch() ? match.captured(1) : partition;
}
#endif

} // namespace

WVolumeEject::WVolumeEject(QWidget* parent, PlayerManager* pPlayerManager)
        : WWidget(parent),
          m_pPlayerManager(pPlayerManager),
          m_pLayout(new QHBoxLayout(this)) {
    m_pLayout->setContentsMargins(0, 0, 0, 0);
    m_pLayout->setSpacing(6);
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &WVolumeEject::refresh);
    m_pollTimer.start();
}

void WVolumeEject::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    Q_UNUSED(context);
}

void WVolumeEject::showEvent(QShowEvent* pEvent) {
    WWidget::showEvent(pEvent);
    refresh();
}

void WVolumeEject::refresh() {
    if (!isVisible()) {
        return;
    }
    const QList<Volume> volumes = VolumeWatcher::externalVolumes();
    QStringList roots;
    for (const Volume& volume : volumes) {
        roots.append(volume.rootPath);
    }
    if (roots == m_shownRoots) {
        return;
    }
    m_shownRoots = roots;

    while (QLayoutItem* pItem = m_pLayout->takeAt(0)) {
        delete pItem->widget();
        delete pItem;
    }
    m_pLayout->setContentsMargins(volumes.isEmpty() ? 0 : 8, 0, 0, 0);
    for (const Volume& volume : volumes) {
        auto* pButton = new QPushButton(
                QString(QChar(0x23CF)) + QChar(' ') + shortName(volume.name), // eject sign
                this);
        pButton->setObjectName(QStringLiteral("VolumeEjectBtn"));
        pButton->setToolTip(tr("Eject %1").arg(volume.name));
        pButton->setFocusPolicy(Qt::NoFocus);
        pButton->setProperty("label", pButton->text());
        connect(pButton, &QPushButton::clicked, this, [this, pButton, volume]() {
            eject(pButton, volume);
        });
        m_pLayout->addWidget(pButton);
    }
    updateGeometry();
}

void WVolumeEject::flash(QPushButton* pButton, const QString& text) {
    pButton->setEnabled(true);
    pButton->setText(text);
    QPointer<QPushButton> pGuard(pButton);
    QTimer::singleShot(kMessageDurationMs, this, [pGuard]() {
        if (pGuard) {
            pGuard->setText(pGuard->property("label").toString());
        }
    });
}

void WVolumeEject::eject(QPushButton* pButton, const Volume& volume) {
    // Never pull a drive out from under a loaded track.
    const QString prefix = volume.rootPath.endsWith(QChar('/'))
            ? volume.rootPath
            : volume.rootPath + QChar('/');
    for (int deck = 0; deck < m_pPlayerManager->numberOfDecks(); ++deck) {
        BaseTrackPlayer* pPlayer =
                m_pPlayerManager->getPlayer(PlayerManager::groupForDeck(deck));
        if (!pPlayer) {
            continue;
        }
        const TrackPointer pTrack = pPlayer->getLoadedTrack();
        if (pTrack && pTrack->getLocation().startsWith(prefix)) {
            flash(pButton, tr("IN USE: DECK %1").arg(deck + 1));
            return;
        }
    }

    pButton->setEnabled(false);
    pButton->setText(tr("EJECTING..."));

    auto* pProcess = new QProcess(this);
    QPointer<QPushButton> pGuard(pButton);
    connect(pProcess,
            &QProcess::finished,
            this,
            [this, pProcess, pGuard](int exitCode, QProcess::ExitStatus status) {
                if (pGuard && (status != QProcess::NormalExit || exitCode != 0)) {
                    flash(pGuard, tr("BUSY"));
                }
                // On success the volume disappears and the next poll removes the button.
                pProcess->deleteLater();
            });
    connect(pProcess,
            &QProcess::errorOccurred,
            this,
            [this, pProcess, pGuard](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    if (pGuard) {
                        flash(pGuard, tr("CAN'T EJECT"));
                    }
                    pProcess->deleteLater();
                }
            });

#if defined(Q_OS_MACOS)
    pProcess->start(QStringLiteral("/bin/sh"),
            {QStringLiteral("-c"),
                    QStringLiteral("/usr/sbin/diskutil eject \"$1\" || "
                                   "/usr/sbin/diskutil unmount \"$1\""),
                    QStringLiteral("sh"),
                    volume.rootPath});
#else
    // Unmount, then power the whole drive off if udisks allows it.
    pProcess->start(QStringLiteral("/bin/sh"),
            {QStringLiteral("-c"),
                    QStringLiteral("udisksctl unmount -b \"$1\" && "
                                   "{ udisksctl power-off -b \"$2\" || true; }"),
                    QStringLiteral("sh"),
                    volume.device,
                    wholeDiskDevice(volume.device)});
#endif
}
