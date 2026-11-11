#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace mcphub {

class UpstreamProxy {
public:
    UpstreamProxy() = default;
    ~UpstreamProxy();

    bool start(const QString& program, const QStringList& args, const QProcessEnvironment& env, QString* err);
    bool request(const QString& method, const QJsonObject& params, QJsonObject* result, QJsonObject* error,
                 int timeoutMs);
    void stop();

    QStringList toolNames() const { return m_toolNames; }
    QJsonArray toolDefs() const { return m_toolDefs; }
    bool alive() const { return m_proc.state() == QProcess::Running; }

private:
    bool sendLine(const QJsonObject& msg);
    bool readMessage(QJsonObject* msg, int timeoutMs);

    QProcess m_proc;
    QByteArray m_buf;
    int m_nextId = 1;
    QStringList m_toolNames;
    QJsonArray m_toolDefs;
};

}
