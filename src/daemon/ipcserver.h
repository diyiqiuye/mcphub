#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>

#include "daemon/catalog.h"
#include "daemon/provisioner.h"
#include "daemon/configgen.h"
#include "shared/protocol.h"

class QLocalServer;
class QLocalSocket;

namespace mcphub {

class IpcServer : public QObject {
    Q_OBJECT
public:
    IpcServer(QString root, QString catalogDir, QString catalogError, QObject* parent = nullptr);

    bool start(QString* error);
    void log(const QString& level, const QString& source, const QString& text);
    QJsonObject statusSnapshot() const;
    QVector<ToolStatus> tools() const;

private:
    void handle(QLocalSocket* s, const QJsonObject& req);
    void sendResult(QLocalSocket* s, const QJsonValue& id, const QJsonObject& result);
    void sendError(QLocalSocket* s, const QJsonValue& id, int code, const QString& message);
    void broadcast(const QString& methodName, const QJsonObject& params);
    void tickStatus();

    ToolStatus probeTool(const CatalogEntry& e) const;
    bool underRoot(const QString& path) const;
    void appendLogFile(const QJsonObject& entry) const;

    void loadOverrides();
    void saveOverrides() const;
    void runVerify(QLocalSocket* s, const QJsonValue& id, const CatalogEntry& e, const ToolStatus& st);
    QString resolveVerifyArg(const QString& arg, const QString& toolPath) const;
    QString jreJavaPath() const;
    QString jreRoot() const;

    QString m_root;
    QString m_catalogDir;
    QString m_catalogError;
    Catalog m_catalog;
    ConfigGen m_cfg;

    QLocalServer* m_server = nullptr;
    QSet<QLocalSocket*> m_clients;
    QHash<QLocalSocket*, QByteArray> m_buffers;

    Provisioner* m_prov = nullptr;
    QHash<QString, double> m_progress;
    qint64 m_lastProgressBroadcast = 0;

    QVector<QJsonObject> m_logRing;
    QHash<QString, QString> m_lastSig;
    QHash<QString, QString> m_overrides;
    QHash<QString, QJsonObject> m_verify;
    QTimer m_statusTimer;
    qint64 m_startedAt = 0;
};

}
