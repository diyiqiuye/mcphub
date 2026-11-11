#include "daemon/upstreamproxy.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>

namespace mcphub {

UpstreamProxy::~UpstreamProxy() {
    stop();
}

void UpstreamProxy::stop() {
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.closeWriteChannel();
        if (!m_proc.waitForFinished(1500)) {
            m_proc.kill();
            m_proc.waitForFinished(1500);
        }
    }
}

bool UpstreamProxy::start(const QString& program, const QStringList& args, const QProcessEnvironment& env,
                          QString* err) {
    m_proc.setProgram(program);
    m_proc.setArguments(args);
    m_proc.setProcessEnvironment(env);
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    m_proc.start();
    if (!m_proc.waitForStarted(8000)) {
        if (err) *err = QStringLiteral("启动失败: %1").arg(m_proc.errorString());
        return false;
    }

    QJsonObject init;
    init.insert("protocolVersion", "2024-11-05");
    init.insert("capabilities", QJsonObject());
    init.insert("clientInfo", QJsonObject{{"name", "mcphub"}, {"version", "0.1.3"}});

    QJsonObject result, error;
    if (!request(QStringLiteral("initialize"), init, &result, &error, 20000)) {
        if (err) *err = error.value("message").toString(QStringLiteral("initialize 失败"));
        stop();
        return false;
    }

    QJsonObject note;
    note.insert("jsonrpc", "2.0");
    note.insert("method", "notifications/initialized");
    note.insert("params", QJsonObject());
    sendLine(note);

    QJsonObject listResult;
    if (!request(QStringLiteral("tools/list"), QJsonObject(), &listResult, &error, 25000)) {
        if (err) *err = error.value("message").toString(QStringLiteral("tools/list 失败"));
        stop();
        return false;
    }
    m_toolDefs = listResult.value("tools").toArray();
    m_toolNames.clear();
    for (const auto& v : m_toolDefs) {
        const QString n = v.toObject().value("name").toString();
        if (!n.isEmpty()) m_toolNames << n;
    }
    return true;
}

bool UpstreamProxy::sendLine(const QJsonObject& msg) {
    if (m_proc.state() != QProcess::Running) return false;
    m_proc.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n");
    return m_proc.waitForBytesWritten(3000);
}

bool UpstreamProxy::readMessage(QJsonObject* msg, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        int idx;
        while ((idx = m_buf.indexOf('\n')) >= 0) {
            const QByteArray line = m_buf.left(idx);
            m_buf.remove(0, idx + 1);
            if (line.trimmed().isEmpty()) continue;
            const auto doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            *msg = doc.object();
            return true;
        }
        if (m_proc.state() == QProcess::NotRunning) return false;
        const int left = timeoutMs - static_cast<int>(timer.elapsed());
        if (!m_proc.waitForReadyRead(qBound(1, left, 2000))) {
            if (m_proc.state() == QProcess::NotRunning) return false;
            continue;
        }
        m_buf += m_proc.readAllStandardOutput();
    }
    return false;
}

bool UpstreamProxy::request(const QString& method, const QJsonObject& params, QJsonObject* result,
                            QJsonObject* error, int timeoutMs) {
    if (m_proc.state() != QProcess::Running) {
        if (error) *error = QJsonObject{{"message", QStringLiteral("上游进程未运行")}};
        return false;
    }
    const int id = m_nextId++;
    QJsonObject req;
    req.insert("jsonrpc", "2.0");
    req.insert("id", id);
    req.insert("method", method);
    req.insert("params", params);
    if (!sendLine(req)) {
        if (error) *error = QJsonObject{{"message", QStringLiteral("写入上游失败")}};
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QJsonObject msg;
        if (!readMessage(&msg, timeoutMs - static_cast<int>(timer.elapsed()))) break;
        if (msg.value("id").toInt(-1) != id) continue;
        if (msg.contains("error")) {
            if (error) *error = msg.value("error").toObject();
            return false;
        }
        if (result) *result = msg.value("result").toObject();
        return true;
    }
    if (error) {
        *error = QJsonObject{{"message", QStringLiteral("上游响应超时（%1 ms）").arg(timeoutMs)},
                             {"code", -32000}};
    }
    return false;
}

}
