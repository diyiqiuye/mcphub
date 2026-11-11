#include "gui/daemonclient.h"

#include <QJsonDocument>
#include <QJsonValue>

#include "shared/protocol.h"

DaemonClient::DaemonClient(QObject* parent) : QObject(parent) {
    m_retry.setInterval(1200);
    connect(&m_retry, &QTimer::timeout, this, &DaemonClient::tryConnect);
    connect(&m_socket, &QLocalSocket::connected, this, &DaemonClient::onConnected);
    connect(&m_socket, &QLocalSocket::disconnected, this, &DaemonClient::onDisconnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &DaemonClient::onReadyRead);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (m_socket.state() == QLocalSocket::UnconnectedState) setOnline(false);
    });
}

DaemonClient::~DaemonClient() {
    m_shutdown = true;
    m_retry.stop();
    m_socket.disconnect(this);
    m_socket.abort();
}

void DaemonClient::start() {
    m_retry.start();
    tryConnect();
}

void DaemonClient::stop() {
    m_retry.stop();
    m_socket.abort();
    setOnline(false);
}

void DaemonClient::tryConnect() {
    if (m_socket.state() != QLocalSocket::UnconnectedState) return;
    m_socket.connectToServer(mcphub::pipeName());
    if (m_socket.waitForConnected(120)) {

    }
}

void DaemonClient::onConnected() {
    m_retry.stop();
    m_buffer.clear();
    setOnline(true);
}

void DaemonClient::onDisconnected() {
    if (m_shutdown) return;
    m_buffer.clear();
    setOnline(false);
    if (m_pending.isEmpty()) return;
    const QJsonObject err{{"code", mcphub::errc::Ipc}, {"message", QStringLiteral("daemon disconnected")}};
    const auto callbacks = m_pending;
    m_pending.clear();
    for (auto it = callbacks.begin(); it != callbacks.end(); ++it) {
        if (it.value()) it.value()({}, err);
    }
}

void DaemonClient::setOnline(bool on) {
    if (m_shutdown || m_online == on) return;
    m_online = on;
    if (!on) m_retry.start();
    emit onlineChanged(on);
}

void DaemonClient::onReadyRead() {
    m_buffer.append(m_socket.readAll());
    int idx;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError pe{};
        const auto doc = QJsonDocument::fromJson(line, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const auto obj = doc.object();

        if (obj.contains("id") && (obj.contains("result") || obj.contains("error"))) {
            const quint64 id = static_cast<quint64>(obj.value("id").toDouble());
            auto cb = m_pending.take(id);
            if (cb) cb(obj.value("result").toObject(), obj.value("error").toObject());
            continue;
        }
        const QString method = obj.value("method").toString();
        if (!method.isEmpty()) emit eventArrived(method, obj.value("params").toObject());
    }
}

void DaemonClient::request(const QString& method, const QJsonObject& params, Reply cb, int timeoutMs) {
    if (!m_online) {
        if (cb) cb({}, QJsonObject{{"code", mcphub::errc::Ipc}, {"message", QStringLiteral("daemon offline")}});
        return;
    }
    const quint64 id = m_nextId++;
    QJsonObject req;
    req.insert("jsonrpc", "2.0");
    req.insert("id", static_cast<double>(id));
    req.insert("method", method);
    req.insert("params", params);
    m_socket.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
    m_socket.flush();

    if (cb) {
        m_pending.insert(id, cb);
        QTimer::singleShot(timeoutMs, this, [this, id] {
            auto pending = m_pending.take(id);
            if (pending) {
                pending({}, QJsonObject{{"code", mcphub::errc::Ipc},
                                        {"message", QStringLiteral("request timeout")}});
            }
        });
    }
}
