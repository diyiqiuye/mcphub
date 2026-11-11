#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#include <functional>

class DaemonClient : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonObject& result, const QJsonObject& error)>;

    explicit DaemonClient(QObject* parent = nullptr);
    ~DaemonClient() override;

    void start();
    void stop();
    bool online() const { return m_online; }

    void request(const QString& method, const QJsonObject& params = {}, Reply cb = {}, int timeoutMs = 8000);

signals:
    void onlineChanged(bool online);
    void eventArrived(const QString& method, const QJsonObject& params);

private:
    void tryConnect();
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void setOnline(bool on);

    QLocalSocket m_socket;
    QTimer m_retry;
    QByteArray m_buffer;
    quint64 m_nextId = 1;
    QHash<quint64, Reply> m_pending;
    bool m_online = false;
    bool m_shutdown = false;
};
