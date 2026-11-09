#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

class QTimer;

// The Pi as a Bluetooth speaker: what rvdix-bt.py (pi/pi-files in the skin repo) says about the phones and
// what plays, read from $XDG_RUNTIME_DIR/rvdix-bt.json, and the commands sent back to it over its socket.
// Only exists when MIXXX_BLUETOOTH=1 (set by the Pi's supervisor script): instance() is nullptr elsewhere.
class BluetoothSession : public QObject {
    Q_OBJECT
  public:
    struct Device {
        QString address;
        QString name;
        bool paired = false;
        bool connected = false;
    };

    static bool enabled();
    static BluetoothSession* instance();

    bool helperAvailable() const {
        return m_available;
    }
    bool connected() const {
        return m_connected;
    }
    QString deviceName() const {
        return m_deviceName;
    }
    QString status() const {
        return m_status;
    }
    bool playing() const {
        return m_status == QLatin1String("playing");
    }
    QString title() const {
        return m_title;
    }
    QString artist() const {
        return m_artist;
    }
    QString album() const {
        return m_album;
    }
    // The picture of the track that is on now (sent by the phone over Bluetooth), or empty
    QString artPath() const {
        return m_artPath;
    }
    // How far into the track the phone is, worked out from what the helper wrote and when (BlueZ's position
    // moves by itself while it plays; the file is only rewritten when something changes)
    qint64 positionMs() const;
    qint64 durationMs() const {
        return m_durationMs;
    }
    int pairSecondsLeft() const {
        return m_pairSecondsLeft;
    }
    QList<Device> devices() const {
        return m_devices;
    }

  public slots:
    void pair();
    void cancelPair();
    void forget(const QString& address);
    void disconnectDevice();
    void play();
    void pause();
    void next();
    void previous();
    // Start the system's default output (the phone's stream stays asleep if the card was taken from the player after it began)
    void wakeOutput();

  signals:
    void changed();
    void connectedChanged(bool connected);
    void playingChanged(bool playing);

  private:
    explicit BluetoothSession(QObject* pParent);
    void poll();
    void send(const QString& command);

    QString m_statePath;
    QString m_socketPath;
    QTimer* m_pPollTimer;
    QByteArray m_raw;

    bool m_available = false;
    bool m_connected = false;
    QString m_deviceName;
    QString m_status;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_artPath;
    int m_pairSecondsLeft = 0;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    qint64 m_stateTimeMs = 0;
    QList<Device> m_devices;
};
