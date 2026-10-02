#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

/// Knows which mounted volumes are external (USB sticks, SD cards, external
/// drives, never the system disk) and tells you when that set changes.
///
/// The set is read from QStorageInfo and only counts places where removable
/// media get mounted: /Volumes on macOS, /media, /run/media and /mnt on Linux.
class VolumeWatcher : public QObject {
    Q_OBJECT
  public:
    struct Volume {
        QString rootPath;
        QString device;
        QString name;
    };

    explicit VolumeWatcher(QObject* parent = nullptr);

    static QList<Volume> externalVolumes();

  signals:
    /// A volume was mounted or removed and the list then stayed unchanged for
    /// a short settle time (a freshly mounted drive isn't readable instantly).
    /// Not emitted for the volumes that were already there at start-up.
    void volumesChanged();

  private slots:
    void poll();

  private:
    static QStringList rootPaths(const QList<Volume>& volumes);

    QTimer m_pollTimer;
    QTimer m_settleTimer;
    QStringList m_lastRoots;
};
