#include "daemon/ipcserver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace mcphub {

namespace {
QJsonObject logEntry(const QString& level, const QString& source, const QString& text) {
    QJsonObject o;
    o.insert("ts", QDateTime::currentMSecsSinceEpoch());
    o.insert("level", level);
    o.insert("source", source);
    o.insert("text", text);
    return o;
}

const char* kIdaAutostartPlugin = R"PY(
import os
import traceback
import importlib.util
import idaapi

LOG = os.path.join(os.environ.get("TEMP", "."), "mcphub-ida-autostart.log")

def _log(msg):
    try:
        with open(LOG, "a", encoding="utf-8") as fh:
            fh.write(str(msg) + "\n")
    except Exception:
        pass

class McphubIdaAutostart(idaapi.plugin_t):
    flags = idaapi.PLUGIN_KEEP
    comment = "MCPHub IDA MCP autostart"
    help = ""
    wanted_name = "MCPHub IDA Autostart"
    wanted_hotkey = ""

    def init(self):
        self._srv = None
        _log("init start")
        try:
            path = os.path.join(os.environ["APPDATA"], "Hex-Rays", "IDA Pro", "plugins", "mcp-plugin.py")
            _log("mcp-plugin path exists: %s" % os.path.exists(path))
            spec = importlib.util.spec_from_file_location("mcphub_ida_pro_mcp_plugin", path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            _log("mcp-plugin imported")
            self._srv = mod.Server()
            self._srv.start()
            _log("server.start() called")
        except Exception:
            _log("autostart failed:/n" + traceback.format_exc())
        return idaapi.PLUGIN_KEEP

    def run(self, arg):
        pass

    def term(self):
        try:
            if self._srv:
                self._srv.stop()
        except Exception:
            _log("term failed:/n" + traceback.format_exc())

def PLUGIN_ENTRY():
    return McphubIdaAutostart()
)PY";

}

IpcServer::IpcServer(QString root, QString catalogDir, QString catalogError, QObject* parent)
    : QObject(parent),
      m_root(std::move(root)),
      m_catalogDir(std::move(catalogDir)),
      m_catalogError(std::move(catalogError)),
      m_cfg(m_root, QCoreApplication::applicationFilePath()) {
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    QString err;
    if (!m_catalog.load(m_catalogDir, &err) && catalogError.isEmpty()) m_catalogError = err;

    connect(&m_statusTimer, &QTimer::timeout, this, &IpcServer::tickStatus);
    loadOverrides();

    m_prov = new Provisioner(m_root, &m_catalog,
                             [this](const QString& tid) {
                                 const auto* te = m_catalog.find(tid);
                                 return te && !probeTool(*te).path.isEmpty();
                             },
                             this);
    connect(m_prov, &Provisioner::log, this, [this](const QString& level, const QString& text) {
        log(level, QStringLiteral("provision"), text);
    });
    connect(m_prov, &Provisioner::progress, this, [this](const QString& id, const QString& phase,
                                                         qint64 recv, qint64 total) {
        const double p = total > 0 ? qBound(0.0, static_cast<double>(recv) / total, 1.0) : 0.0;
        m_progress.insert(id, phase == QStringLiteral("done") ? 1.0 : p);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastProgressBroadcast > 220) {
            m_lastProgressBroadcast = now;
            broadcast(event::Status, statusSnapshot());
        }
    });
    connect(m_prov, &Provisioner::finished, this, [this](const QString& id, bool ok, const QString& message) {
        m_progress.remove(id);
        log(ok ? "info" : "error", "provision",
            QStringLiteral("%1 %2").arg(id, ok ? QStringLiteral("安装完成") : QStringLiteral("失败：") + message));
        broadcast(event::Status, statusSnapshot());
        if (!ok) return;
        const auto* e = m_catalog.find(id);
        if (!e) return;
        const auto st = probeTool(*e);
        if (!e->verifyCmd.isEmpty()) runVerify(nullptr, QJsonValue(), *e, st);
    });
}

bool IpcServer::start(QString* error) {
    const QString name = pipeName();
    QLocalServer::removeServer(name);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(name)) {
        if (error) *error = QStringLiteral("命名管道监听失败: %1 (%2)").arg(name, m_server->errorString());
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (auto* s = m_server->nextPendingConnection()) {
            m_clients.insert(s);
            connect(s, &QLocalSocket::readyRead, this, [this, s] {
                m_buffers[s].append(s->readAll());
                auto& buf = m_buffers[s];
                int idx;
                while ((idx = buf.indexOf('\n')) >= 0) {
                    const QByteArray line = buf.left(idx);
                    buf.remove(0, idx + 1);
                    if (line.trimmed().isEmpty()) continue;
                    QJsonParseError pe{};
                    const auto doc = QJsonDocument::fromJson(line, &pe);
                    if (pe.error == QJsonParseError::NoError && doc.isObject()) handle(s, doc.object());
                }
            });
            connect(s, &QLocalSocket::disconnected, this, [this, s] {
                m_clients.remove(s);
                m_buffers.remove(s);
                s->deleteLater();
                log("info", "ipc", "client disconnected");
            });

            QJsonObject hello;
            hello.insert("version", QString::fromLatin1(Version));
            hello.insert("root", QDir::toNativeSeparators(m_root));
            hello.insert("catalogDir", QDir::toNativeSeparators(m_catalogDir));
            hello.insert("catalogError", m_catalogError);
            hello.insert("pipe", pipeName());
            hello.insert("tools", QJsonArray());
            s->write(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"method", event::Hello}, {"params", hello}})
                         .toJson(QJsonDocument::Compact) + "\n");
            s->flush();
            log("info", "ipc", "client connected");
        }
    });

    m_statusTimer.start(1500);
    tickStatus();
    log("info", "core", QStringLiteral("daemon online · pipe=%1 · catalog=%2").arg(pipeName(), QDir::toNativeSeparators(m_catalogDir)));
    return true;
}

QVector<ToolStatus> IpcServer::tools() const {
    QVector<ToolStatus> out;
    out.reserve(m_catalog.entries().size());
    for (const auto& e : m_catalog.entries()) out.append(probeTool(e));
    return out;
}

ToolStatus IpcServer::probeTool(const CatalogEntry& e) const {
    ToolStatus s = probeEntry(e, m_root);
    if (m_progress.contains(e.id)) {
        s.state = ToolState::Downloading;
        s.progress = m_progress.value(e.id);
        s.message = QStringLiteral("安装中");
        return s;
    }

    const QString ov = m_overrides.value(e.id);
    if (!ov.isEmpty()) {
        QFileInfo fi(expandPath(ov, m_root));
        s.path = QDir::toNativeSeparators(fi.absoluteFilePath());
        s.external = !underRoot(fi.absoluteFilePath());
        if (fi.exists()) {
            s.state = ToolState::Ready;
            s.progress = 1.0;
            s.message = QStringLiteral("手动登记");
        } else {
            s.state = ToolState::Broken;
            s.progress = 0.0;
            s.message = QStringLiteral("登记路径不存在");
        }
    }

    const auto v = m_verify.value(e.id);
    if (!v.isEmpty()) {
        s.verified = true;
        s.verifyOk = v.value("ok").toBool();
        s.verifyText = v.value("output").toString();
        if (!s.verifyOk && s.state == ToolState::Ready) s.state = ToolState::Degraded;
        if (s.verifyOk && s.state == ToolState::Degraded) s.state = ToolState::Ready;
    }
    return s;
}

bool IpcServer::underRoot(const QString& path) const {
    return pathUnder(path, m_root);
}

QJsonObject IpcServer::statusSnapshot() const {
    QJsonArray arr;
    int ready = 0, total = 0;
    for (const auto& t : tools()) {
        arr.append(toJson(t));
        ++total;
        if (t.ready()) ++ready;
    }
    QJsonObject hub;
    hub.insert("version", QString::fromLatin1(Version));
    hub.insert("root", QDir::toNativeSeparators(m_root));
    hub.insert("catalogDir", QDir::toNativeSeparators(m_catalogDir));
    hub.insert("catalogError", m_catalogError);
    hub.insert("catalogCount", m_catalog.entries().size());
    hub.insert("uptimeMs", QDateTime::currentMSecsSinceEpoch() - m_startedAt);
    hub.insert("clients", m_clients.size());
    hub.insert("ready", ready);
    hub.insert("total", total);
    hub.insert("gateway", QStringLiteral("stdio"));
    hub.insert("bundle", m_prov && m_prov->hasBundle());
    hub.insert("bundleFile", m_prov ? m_prov->bundleInfo().value("file").toString() : QString());
    hub.insert("idaAutostart",
               QFileInfo::exists(qEnvironmentVariable("APPDATA") +
                                 QStringLiteral("/Hex-Rays/IDA Pro/plugins/mcphub-ida-autostart.py")));
    hub.insert("pipe", pipeName());

    QJsonObject out;
    out.insert("tools", arr);
    out.insert("hub", hub);
    return out;
}

void IpcServer::sendResult(QLocalSocket* s, const QJsonValue& id, const QJsonObject& result) {
    QJsonObject resp;
    resp.insert("jsonrpc", "2.0");
    resp.insert("id", id);
    resp.insert("result", result);
    s->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + "\n");
    s->flush();
}

void IpcServer::sendError(QLocalSocket* s, const QJsonValue& id, int code, const QString& message) {
    QJsonObject err;
    err.insert("code", code);
    err.insert("message", message);
    QJsonObject resp;
    resp.insert("jsonrpc", "2.0");
    resp.insert("id", id);
    resp.insert("error", err);
    s->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + "\n");
    s->flush();
}

void IpcServer::broadcast(const QString& methodName, const QJsonObject& params) {
    if (m_clients.isEmpty()) return;
    QJsonObject msg;
    msg.insert("jsonrpc", "2.0");
    msg.insert("method", methodName);
    msg.insert("params", params);
    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    for (auto* s : m_clients) {
        if (s->state() == QLocalSocket::ConnectedState) {
            s->write(line);
            s->flush();
        }
    }
}

void IpcServer::handle(QLocalSocket* s, const QJsonObject& req) {
    const QJsonValue id = req.value("id");
    const QString m = req.value("method").toString();
    const QJsonObject p = req.value("params").toObject();

    if (m == method::StatusGet) {
        sendResult(s, id, statusSnapshot());
        return;
    }
    if (m == method::AppInfo) {
        QJsonObject o;
        o.insert("name", QString::fromLatin1(AppName));
        o.insert("version", QString::fromLatin1(Version));
        o.insert("root", QDir::toNativeSeparators(m_root));
        o.insert("catalogDir", QDir::toNativeSeparators(m_catalogDir));
        o.insert("daemon", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        o.insert("pipe", pipeName());
        sendResult(s, id, o);
        return;
    }
    if (m == method::CatalogList) {
        QJsonArray arr;
        for (const auto& e : m_catalog.entries()) arr.append(e.toJson());
        sendResult(s, id, QJsonObject{{"catalog", arr}, {"dir", QDir::toNativeSeparators(m_catalogDir)},
                                      {"error", m_catalogError}});
        return;
    }
    if (m == method::LogTail) {
        const int n = qBound(1, p.value("lines").toInt(200), 2000);
        QJsonArray arr;
        const int from = qMax(0, m_logRing.size() - n);
        for (int i = from; i < m_logRing.size(); ++i) arr.append(m_logRing.at(i));
        sendResult(s, id, QJsonObject{{"lines", arr}, {"total", m_logRing.size()}});
        return;
    }
    if (m == method::ProvisionPlan) {
        QJsonArray steps;
        int totalMB = 0;
        for (const auto& t : tools()) {
            const auto* e = m_catalog.find(t.id);
            if (!e || t.ready()) continue;
            QJsonObject step;
            step.insert("id", t.id);
            if (e->installType == QStringLiteral("manual")) {
                step.insert("action", "manual");
                step.insert("note", e->installNote);
            } else {
                step.insert("action", "download");
                step.insert("sizeMB", e->sizeMB);
                step.insert("sources", e->sources.size());
                totalMB += e->sizeMB;
            }
            steps.append(step);
        }
        sendResult(s, id, QJsonObject{{"steps", steps},
                                      {"totalMB", totalMB},
                                      {"bundle", m_prov && m_prov->hasBundle()},
                                      {"bundleBytes", m_prov ? m_prov->bundleInfo().value("bytes").toVariant().toLongLong() : 0}});
        return;
    }
    if (m == method::ConfigSnippet) {
        sendResult(s, id, m_cfg.snippet(p.value("client").toString(QStringLiteral("standard"))));
        return;
    }
    if (m == method::ConfigWrite) {
        QString backup, err;
        const QString client = p.value("client").toString(QStringLiteral("zcode"));
        if (!m_cfg.write(client, &backup, &err)) {
            sendError(s, id, errc::Perm, err);
            return;
        }
        log("info", "config", QStringLiteral("已写入 %1 配置%2").arg(client, backup.isEmpty() ? QString() : QStringLiteral("（已备份）")));
        sendResult(s, id, QJsonObject{{"ok", true}, {"client", client}, {"backup", backup}});
        return;
    }
    if (m == method::FsOpen) {
        const QString tid = p.value("toolId").toString();
        if (!tid.isEmpty()) {
            const auto* te = m_catalog.find(tid);
            if (!te) { sendError(s, id, errc::NoTool, QStringLiteral("未知工具: %1").arg(tid)); return; }
            const auto ts = probeTool(*te);
            if (ts.path.isEmpty() || !QFileInfo::exists(ts.path)) {
                sendError(s, id, errc::NoTool, QStringLiteral("%1 尚未安装/登记").arg(te->name));
                return;
            }
            QProcess::startDetached(QStringLiteral("explorer.exe"), {ts.path});
            sendResult(s, id, QJsonObject{{"ok", true}, {"path", ts.path}});
            return;
        }
        const QString target = expandPath(p.value("target").toString(), m_root);
        const QStringList allowed = {"", "tools", "logs", "catalog", "cache", "state"};
        QString resolved;
        for (const auto& a : allowed) {
            const QString base = QDir(m_root).absoluteFilePath(a);
            const QString norm = QDir::cleanPath(target);
            if (norm.compare(QDir::cleanPath(base), Qt::CaseInsensitive) == 0 ||
                norm.startsWith(QDir::cleanPath(base) + QDir::separator(), Qt::CaseInsensitive)) {
                resolved = norm;
                break;
            }
        }
        if (resolved.isEmpty()) {
            sendError(s, id, errc::Perm, QStringLiteral("路径不在白名单内"));
            return;
        }
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(resolved)});
        sendResult(s, id, QJsonObject{{"ok", true}, {"path", QDir::toNativeSeparators(resolved)}});
        return;
    }
    if (m == method::IdaAutostart) {
        const QString action = p.value("action").toString(QStringLiteral("status"));
        const QString dir = qEnvironmentVariable("APPDATA") +
                            QStringLiteral("/Hex-Rays/IDA Pro/plugins");
        const QString upstream = dir + QStringLiteral("/mcp-plugin.py");
        const QString file = dir + QStringLiteral("/mcphub-ida-autostart.py");
        auto status = [&] {
            return QJsonObject{{"enabled", QFileInfo::exists(file)},
                               {"upstream", QFileInfo::exists(upstream)},
                               {"path", QDir::toNativeSeparators(file)}};
        };
        if (action == QStringLiteral("status")) {
            sendResult(s, id, status());
            return;
        }
        if (!QFileInfo::exists(upstream)) {
            sendError(s, id, errc::NoTool,
                      QStringLiteral("未找到 mcp-plugin.py，请先安装 ida-pro-mcp 的 IDA 插件"));
            return;
        }
        if (action == QStringLiteral("disable")) {
            QFile::remove(file);
            log("info", "ida", QStringLiteral("已停用 IDA 自动接入"));
            broadcast(event::Status, statusSnapshot());
            sendResult(s, id, status());
            return;
        }
        QDir().mkpath(dir);
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            sendError(s, id, errc::Perm, QStringLiteral("无法写入 %1").arg(QDir::toNativeSeparators(file)));
            return;
        }
        f.write(kIdaAutostartPlugin);
        f.close();
        log("info", "ida", QStringLiteral("已写入自动接入插件，重启 IDA 后生效"));
        broadcast(event::Status, statusSnapshot());
        sendResult(s, id, status());
        return;
    }
    if (m == method::ToolSetPath || m == method::ToolForget) {
        const QString tid = p.value("id").toString();
        const auto* te = m_catalog.find(tid);
        if (!te) {
            sendError(s, id, errc::NoTool, QStringLiteral("未知工具: %1").arg(tid));
            return;
        }
        const QString raw = m == method::ToolForget ? QString() : p.value("path").toString().trimmed();
        if (raw.isEmpty()) {
            m_overrides.remove(tid);
            m_verify.remove(tid);
            saveOverrides();
            log("info", "tool", QStringLiteral("%1 已清除手动登记").arg(te->name));
            broadcast(event::Status, statusSnapshot());
            sendResult(s, id, QJsonObject{{"ok", true}, {"id", tid}, {"cleared", true}});
            return;
        }
        const QString abs = expandPath(raw, m_root);
        if (!QFileInfo::exists(abs)) {
            sendError(s, id, errc::Perm, QStringLiteral("路径不存在: %1").arg(QDir::toNativeSeparators(abs)));
            return;
        }
        m_overrides.insert(tid, abs);
        m_verify.remove(tid);
        saveOverrides();
        log("info", "tool", QStringLiteral("%1 登记路径 → %2").arg(te->name, QDir::toNativeSeparators(abs)));
        broadcast(event::Status, statusSnapshot());
        sendResult(s, id, QJsonObject{{"ok", true}, {"id", tid}, {"path", QDir::toNativeSeparators(abs)}});
        return;
    }
    if (m == method::ToolVerify) {
        const QString tid = p.value("id").toString();
        const auto* te = m_catalog.find(tid);
        if (!te) {
            sendError(s, id, errc::NoTool, QStringLiteral("未知工具: %1").arg(tid));
            return;
        }
        const auto ts = probeTool(*te);
        if (ts.path.isEmpty()) {
            sendError(s, id, errc::NoTool, QStringLiteral("%1 尚未安装/登记路径").arg(te->name));
            return;
        }
        runVerify(s, id, *te, ts);
        return;
    }
    if (m == method::ProvisionApply) {
        const QString tid = p.value("id").toString();
        const auto* te = m_catalog.find(tid);
        if (!te) {
            sendError(s, id, errc::NoTool, QStringLiteral("未知工具: %1").arg(tid));
            return;
        }
        if (te->installType == QStringLiteral("manual")) {
            sendError(s, id, errc::Perm, QStringLiteral("%1 需手动安装后登记目录").arg(te->name));
            return;
        }
        if (m_prov->busy(tid)) {
            sendError(s, id, errc::Busy, QStringLiteral("%1 正在安装").arg(te->name));
            return;
        }
        const auto st = probeTool(*te);
        if (st.ready()) {
            sendError(s, id, errc::Busy, QStringLiteral("%1 已就绪（%2），无需重复安装").arg(te->name, st.path));
            return;
        }
        m_prov->apply(tid);
        sendResult(s, id, QJsonObject{{"ok", true}, {"started", true}, {"id", tid}});
        return;
    }
    if (m == method::ServiceStart || m == method::ServiceStop) {
        sendError(s, id, errc::NotImplemented, QStringLiteral("服务编排将在 M3 实现（当前为 M1 骨架）"));
        return;
    }
    sendError(s, id, -32601, QStringLiteral("method not found: %1").arg(m));
}

QString IpcServer::jreRoot() const {
    if (const auto* jre = m_catalog.find(QStringLiteral("jre21"))) {
        const auto st = probeTool(*jre);
        if (!st.path.isEmpty()) return st.path;
    }
    return QString();
}

QString IpcServer::jreJavaPath() const {
    if (const auto* jre = m_catalog.find(QStringLiteral("jre21"))) {
        const auto st = probeTool(*jre);
        if (!st.path.isEmpty()) {
            const QString p = QDir(st.path).absoluteFilePath(QStringLiteral("bin/java.exe"));
            if (QFileInfo::exists(p)) return QDir::toNativeSeparators(p);
        }
    }
    return QStringLiteral("java");
}

QString IpcServer::resolveVerifyArg(const QString& arg, const QString& toolPath) const {
    QString a = arg;
    a.replace(QStringLiteral("{root}"), QDir::toNativeSeparators(toolPath));
    a.replace(QStringLiteral("{java}"), jreJavaPath());
    return QDir::toNativeSeparators(a);
}

void IpcServer::runVerify(QLocalSocket* s, const QJsonValue& id, const CatalogEntry& e, const ToolStatus& st) {
    Q_UNUSED(st)
    if (e.verifyCmd.isEmpty()) {
        sendError(s, id, errc::NotImplemented, QStringLiteral("%1 未声明校验命令").arg(e.name));
        return;
    }
    QStringList parts;
    for (const auto& a : e.verifyCmd) parts << resolveVerifyArg(a, st.path);
    QString program = parts.takeFirst();
    QStringList args = parts;
    if (program.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive) ||
        program.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)) {
        args.prepend(program);
        args.prepend(QStringLiteral("/c"));
        program = QStringLiteral("cmd.exe");
    }

    auto* proc = new QProcess(this);
    proc->setWorkingDirectory(st.path);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList pathParts;
    const QString jre = jreRoot();
    if (!jre.isEmpty()) {
        env.insert(QStringLiteral("JAVA_HOME"), jre);
        pathParts << (jre + QStringLiteral("/bin"));
    }
    pathParts << st.path;
    pathParts << env.value(QStringLiteral("PATH"));
    env.insert(QStringLiteral("PATH"), pathParts.join(QLatin1Char(';')));
    env.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
    for (auto it = e.verifyEnv.begin(); it != e.verifyEnv.end(); ++it) {
        env.insert(it.key(), resolveVerifyArg(it.value(), st.path));
    }
    proc->setProcessEnvironment(env);
    proc->setProperty("settled", false);
    auto* timer = new QTimer(proc);
    timer->setSingleShot(true);
    QPointer<QLocalSocket> guard(s);

    connect(timer, &QTimer::timeout, proc, [proc] { proc->kill(); });

    connect(proc, &QProcess::finished, this, [this, proc, timer, guard, id, e](int code, QProcess::ExitStatus status) {
        timer->stop();
        if (proc->property("settled").toBool()) return;
        proc->setProperty("settled", true);
        const QString out = QString::fromLocal8Bit(proc->readAll()).trimmed();
        proc->deleteLater();

        bool ok = (status == QProcess::NormalExit) && code == 0;
        if (ok && !e.verifyMatch.isEmpty()) ok = QRegularExpression(e.verifyMatch).match(out).hasMatch();

        QJsonObject rec;
        rec.insert("ok", ok);
        rec.insert("code", code);
        rec.insert("output", out.left(400));
        rec.insert("at", QDateTime::currentMSecsSinceEpoch());
        m_verify.insert(e.id, rec);
        log(ok ? "info" : "warn", "verify",
            QStringLiteral("%1 校验%2 (exit=%3) %4")
                .arg(e.name, ok ? QStringLiteral("通过") : QStringLiteral("失败"))
                .arg(code)
                .arg(out.left(120)));
        broadcast(event::Status, statusSnapshot());
        if (guard) sendResult(guard, id, QJsonObject{{"ok", ok}, {"code", code}, {"output", out.left(4000)}});
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc, timer, guard, id, e](QProcess::ProcessError) {
        if (proc->property("settled").toBool()) return;
        proc->setProperty("settled", true);
        timer->stop();
        const QString msg = proc->errorString();
        proc->deleteLater();
        QJsonObject rec;
        rec.insert("ok", false);
        rec.insert("code", -1);
        rec.insert("output", msg.left(400));
        rec.insert("at", QDateTime::currentMSecsSinceEpoch());
        m_verify.insert(e.id, rec);
        log("error", "verify", QStringLiteral("%1 校验无法执行: %2").arg(e.name, msg));
        broadcast(event::Status, statusSnapshot());
        if (guard) sendError(guard, id, errc::Verify, msg);
    });

    log("info", "verify", QStringLiteral("%1 校验开始: %2 %3").arg(e.name, program, args.join(QLatin1Char(' '))));
    proc->start(program, args);
    timer->start(qMax(5000, e.verifyTimeoutMs));
}

void IpcServer::loadOverrides() {
    m_overrides.clear();
    QFile f(QDir(m_root).absoluteFilePath(QStringLiteral("state/overrides.json")));
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const auto o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty()) {
            m_overrides.insert(it.key(), it.value().toString());
        }
    }
}

void IpcServer::saveOverrides() const {
    const QString dir = QDir(m_root).absoluteFilePath(QStringLiteral("state"));
    QDir().mkpath(dir);
    QJsonObject o;
    for (auto it = m_overrides.begin(); it != m_overrides.end(); ++it) o.insert(it.key(), it.value());
    QFile f(QDir(dir).absoluteFilePath(QStringLiteral("overrides.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void IpcServer::tickStatus() {
    const auto list = tools();
    bool changed = false;
    for (const auto& t : list) {
        const QString sig = stateKey(t.state) + "|" + QString::number(t.progress, 'f', 2) + "|" + t.path;
        if (m_lastSig.value(t.id) != sig) {
            if (m_lastSig.contains(t.id)) {
                log(t.ready() ? "info" : "warn", "state",
                    QStringLiteral("%1 %2 → %3")
                        .arg(t.name, stateKey(t.state),
                             t.path.isEmpty() ? t.message : QStringLiteral("@ ") + t.path));
            }
            m_lastSig.insert(t.id, sig);
            changed = true;
        }
    }
    if (changed && !m_clients.isEmpty()) {
        broadcast(event::Status, statusSnapshot());
    }
}

void IpcServer::log(const QString& level, const QString& source, const QString& text) {
    const auto entry = logEntry(level, source, text);
    m_logRing.append(entry);
    if (m_logRing.size() > 800) m_logRing.remove(0, m_logRing.size() - 800);
    broadcast(event::Log, entry);
    appendLogFile(entry);
}

void IpcServer::appendLogFile(const QJsonObject& entry) const {
    const QString dir = QDir(m_root).absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/mcphubd-") +
                         QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".log");
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    const QString ts = QDateTime::fromMSecsSinceEpoch(entry.value("ts").toVariant().toLongLong())
                           .toString(QStringLiteral("HH:mm:ss.zzz"));
    f.write(QStringLiteral("%1 [%2] [%3] %4\n")
                .arg(ts, entry.value("level").toString(), entry.value("source").toString(),
                     entry.value("text").toString())
                .toUtf8());
}

}
