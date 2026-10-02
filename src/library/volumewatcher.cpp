#include "library/volumewatcher.h"

#include <QFileInfo>
#include <QStorageInfo>

#include "moc_volumewatcher.cpp"

namespace {
constexpr int kPollIntervalMs = 2000;
constexpr int kSettleMs = 2500;
} // namespace

VolumeWatcher::VolumeWatcher(QObject* parent)
        : QObject(parent),
          m_lastRoots(rootPaths(externalVolumes())) {
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &VolumeWatcher::poll);
    m_pollTimer.start();

    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(kSettleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, &VolumeWatcher::volumesChanged);
}

// static
QList<VolumeWatcher::Volume> VolumeWatcher::externalVolumes() {
    QList<Volume> result;
    for (const QStorageInfo& info : QStorageInfo::mountedVolumes()) {
        if (!info.isValid() || !info.isReady() || info.isRoot()) {
            continue;
        }
        const QString root = info.rootPath();
        // Only places where removable media get mounted; never the system disk.
#if defined(Q_OS_MACOS)
        if (!root.startsWith(QStringLiteral("/Volumes/"))) {
            continue;
        }
#elif defined(Q_OS_LINUX)
        if (!root.startsWith(QStringLiteral("/media/")) &&
                !root.startsWith(QStringLiteral("/run/media/")) &&
                !root.startsWith(QStringLiteral("/mnt/"))) {
            continue;
        }
#else
        continue;
#endif
        Volume volume;
        volume.rootPath = root;
        volume.device = QString::fromUtf8(info.device());
        volume.name = QFileInfo(root).fileName();
        if (volume.name.isEmpty()) {
            volume.name = info.displayName();
        }
        result.append(volume);
    }
    return result;
}

// static
QStringList VolumeWatcher::rootPaths(const QList<Volume>& volumes) {
    QStringList roots;
    for (const Volume& volume : volumes) {
        roots.append(volume.rootPath);
    }
    return roots;
}

void VolumeWatcher::poll() {
    const QStringList roots = rootPaths(externalVolumes());
    if (roots == m_lastRoots) {
        return;
    }
    m_lastRoots = roots;
    // Restarting the timer coalesces several changes (e.g. a drive with
    // multiple partitions) into one notification.
    m_settleTimer.start();
}
