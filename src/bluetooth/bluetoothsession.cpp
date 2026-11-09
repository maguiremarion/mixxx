#include "bluetooth/bluetoothsession.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

#include "moc_bluetoothsession.cpp"

namespace {
QString runtimeDir() {
    return qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/run/user/1000"));
}
} // namespace

// static
bool BluetoothSession::enabled() {
    static const bool on = qEnvironmentVariableIntValue("MIXXX_BLUETOOTH") > 0;
    return on;
}

// static
BluetoothSession* BluetoothSession::instance() {
    if (!enabled()) {
        return nullptr;
    }
    static BluetoothSession* pInstance = new BluetoothSession(QCoreApplication::instance());
    return pInstance;
}

BluetoothSession::BluetoothSession(QObject* pParent)
        : QObject(pParent),
          m_statePath(runtimeDir() + QStringLiteral("/rvdix-bt.json")),
          m_socketPath(runtimeDir() + QStringLiteral("/rvdix-bt.sock")),
          m_pPollTimer(new QTimer(this)) {
    m_pPollTimer->setInterval(300);
    connect(m_pPollTimer, &QTimer::timeout, this, &BluetoothSession::poll);
    m_pPollTimer->start();
    poll();
}

void BluetoothSession::poll() {
    QByteArray data;
    QFile file(m_statePath);
    if (file.open(QIODevice::ReadOnly)) {
        data = file.readAll();
    }
    if (data == m_raw) {
        return;
    }
    m_raw = data;

    const bool wasConnected = m_connected;
    const bool wasPlaying = playing();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    m_devices.clear();
    if (data.isEmpty() || error.error != QJsonParseError::NoError || !doc.isObject()) {
        m_available = false;
        m_connected = false;
        m_deviceName.clear();
        m_status.clear();
        m_title.clear();
        m_artist.clear();
        m_album.clear();
        m_artPath.clear();
        m_pairSecondsLeft = 0;
        m_positionMs = 0;
        m_durationMs = 0;
    } else {
        const QJsonObject root = doc.object();
        m_available = true;
        m_pairSecondsLeft = root.value(QLatin1String("adapter")).toObject().value(QLatin1String("pair_seconds_left")).toInt();
        const QJsonValue connectedValue = root.value(QLatin1String("connected"));
        m_connected = connectedValue.isObject();
        m_deviceName = m_connected ? connectedValue.toObject().value(QLatin1String("name")).toString() : QString();
        const QJsonValue playerValue = root.value(QLatin1String("player"));
        const QJsonObject player = playerValue.isObject() ? playerValue.toObject() : QJsonObject();
        m_status = player.value(QLatin1String("status")).toString();
        m_title = player.value(QLatin1String("title")).toString();
        m_artist = player.value(QLatin1String("artist")).toString();
        m_album = player.value(QLatin1String("album")).toString();
        m_artPath = root.value(QLatin1String("art")).toString();
        m_positionMs = static_cast<qint64>(player.value(QLatin1String("position")).toDouble());
        m_durationMs = static_cast<qint64>(player.value(QLatin1String("duration")).toDouble());
        m_stateTimeMs = static_cast<qint64>(root.value(QLatin1String("time")).toDouble() * 1000.0);
        const QJsonArray devices = root.value(QLatin1String("devices")).toArray();
        for (const QJsonValue& value : devices) {
            const QJsonObject device = value.toObject();
            Device entry;
            entry.address = device.value(QLatin1String("address")).toString();
            entry.name = device.value(QLatin1String("name")).toString();
            entry.paired = device.value(QLatin1String("paired")).toBool();
            entry.connected = device.value(QLatin1String("connected")).toBool();
            m_devices.append(entry);
        }
    }
    emit changed();
    if (m_connected != wasConnected) {
        emit connectedChanged(m_connected);
    }
    if (playing() != wasPlaying) {
        emit playingChanged(playing());
    }
}

void BluetoothSession::send(const QString& command) {
    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(500)) {
        qWarning() << "Bluetooth: the helper is not running:" << socket.errorString();
        return;
    }
    socket.write((command + QLatin1Char('\n')).toUtf8());
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
}

qint64 BluetoothSession::positionMs() const {
    qint64 position = m_positionMs;
    if (playing() && m_stateTimeMs > 0) {
        position += qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - m_stateTimeMs);
    }
    if (m_durationMs > 0) {
        position = qMin(position, m_durationMs);
    }
    return qMax<qint64>(0, position);
}

void BluetoothSession::pair() {
    send(QStringLiteral("pair"));
}

void BluetoothSession::cancelPair() {
    send(QStringLiteral("cancelpair"));
}

void BluetoothSession::forget(const QString& address) {
    send(QStringLiteral("forget ") + address);
}

void BluetoothSession::disconnectDevice() {
    send(QStringLiteral("disconnect"));
}

void BluetoothSession::play() {
    send(QStringLiteral("play"));
}

void BluetoothSession::pause() {
    send(QStringLiteral("pause"));
}

void BluetoothSession::next() {
    send(QStringLiteral("next"));
}

void BluetoothSession::wakeOutput() {
    send(QStringLiteral("wake"));
}

void BluetoothSession::previous() {
    send(QStringLiteral("previous"));
}
