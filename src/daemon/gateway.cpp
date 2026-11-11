#include "daemon/gateway.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QProcessEnvironment>
#include <QThread>
#include <QTcpSocket>

#include <cstdio>
#include <iostream>
#include <string>

#include "daemon/adbprovider.h"
#include "daemon/upstreamproxy.h"
#include "daemon/catalog.h"
#include "daemon/configgen.h"
#include "shared/protocol.h"

namespace mcphub {
namespace {

constexpr auto kProtocolVersion = "2025-06-18";

void send(const QJsonObject& msg) {
    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}

QJsonObject result(const QJsonValue& id, const QJsonObject& r) {
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", r}};
}

QJsonObject error(const QJsonValue& id, int code, const QString& message) {
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject textResult(const QString& text, bool isError = false) {
    QJsonObject content;
    content.insert("type", "text");
    content.insert("text", text);
    return QJsonObject{{"content", QJsonArray{content}}, {"isError", isError}};
}

QJsonObject toolDef(const QString& name, const QString& desc, const QJsonObject& props = {}) {
    QJsonObject schema{{"type", "object"}, {"properties", props}};
    return QJsonObject{{"name", name}, {"description", desc}, {"inputSchema", schema}};
}

QJsonArray toolList() {
    QJsonArray arr;
    arr.append(toolDef("hub_status", "MCP Hub 全量状态：工具清单、安装状态、就绪数、守护进程信息"));
    arr.append(toolDef("hub_tools", "只列工具与其状态（精简）"));
    arr.append(toolDef("hub_install_plan", "缺什么装什么：返回待安装步骤与预计体积"));
    arr.append(toolDef("hub_config",
                       "生成 MCP 接入配置（JSON），默认通用 mcpServers；client 可选 zcode|cursor|claude-desktop|claude-code",
                       QJsonObject{{"client", QJsonObject{{"type", "string"}}}}));
    return arr;
}

QJsonObject snapshot(const Catalog& catalog, const QString& root, const QString& catalogDir,
                     const QString& catalogError) {
    QJsonArray tools;
    int ready = 0;
    for (const auto& e : catalog.entries()) {
        const auto st = probeEntry(e, root);
        tools.append(toJson(st));
        if (st.ready()) ++ready;
    }
    QJsonObject hub;
    hub.insert("version", QString::fromLatin1(Version));
    hub.insert("root", QDir::toNativeSeparators(root));
    hub.insert("catalogDir", QDir::toNativeSeparators(catalogDir));
    hub.insert("catalogError", catalogError);
    hub.insert("catalogCount", catalog.entries().size());
    hub.insert("ready", ready);
    hub.insert("total", catalog.entries().size());
    hub.insert("gateway", "stdio");
    hub.insert("pipe", pipeName());
    return QJsonObject{{"tools", tools}, {"hub", hub}};
}

QHash<QString, QString> loadOverrides(const QString& root) {
    QHash<QString, QString> out;
    QFile f(QDir(root).absoluteFilePath(QStringLiteral("state/overrides.json")));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return out;
    const auto o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty()) out.insert(it.key(), it.value().toString());
    }
    return out;
}

QString resolveToolExe(const Catalog& catalog, const QString& id, const QString& exeName,
                       const QString& root, const QHash<QString, QString>& ovr) {
    QString base = ovr.value(id);
    if (!base.isEmpty()) {
        base = expandPath(base, root);
    } else if (const auto* e = catalog.find(id)) {
        for (const auto& raw : e->probe) {
            const QString p = expandPath(raw, root);
            if (QFileInfo::exists(p)) {
                base = p;
                break;
            }
        }
    }
    if (base.isEmpty()) return QString();
    QFileInfo fi(base);
    if (fi.isFile()) return QDir::toNativeSeparators(base);
    const QString exe = QDir(base).absoluteFilePath(exeName);
    return QFileInfo::exists(exe) ? QDir::toNativeSeparators(exe) : QString();
}

void killTree(qint64 pid) {
    if (pid <= 0) return;
    QProcess t;
    t.setProgram(QStringLiteral("taskkill"));
    t.setArguments({QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T"), QStringLiteral("/F")});
    t.setProcessChannelMode(QProcess::MergedChannels);
    t.start();
    if (t.waitForStarted(3000)) {
        t.waitForFinished(8000);
        t.readAll();
    }
}

bool portOpen(const QString& host, int port, int timeoutMs);

bool waitPortClosed(int port, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (!portOpen(QStringLiteral("127.0.0.1"), port, 200)) return true;
        QThread::msleep(200);
    }
    return !portOpen(QStringLiteral("127.0.0.1"), port, 200);
}

QString resolveToolDir(const Catalog& catalog, const QString& id, const QString& root,
                       const QHash<QString, QString>& ovr) {
    QString base = ovr.value(id);
    if (!base.isEmpty()) {
        base = expandPath(base, root);
    } else if (const auto* e = catalog.find(id)) {
        for (const auto& raw : e->probe) {
            const QString p = expandPath(raw, root);
            if (QFileInfo::exists(p)) {
                base = p;
                break;
            }
        }
    }
    if (base.isEmpty()) return QString();
    const QFileInfo fi(base);
    return QDir::toNativeSeparators(fi.isFile() ? fi.absolutePath() : fi.absoluteFilePath());
}

bool portOpen(const QString& host, int port, int timeoutMs = 400) {
    QTcpSocket s;
    s.connectToHost(host, static_cast<quint16>(port));
    const bool ok = s.waitForConnected(timeoutMs);
    if (ok) s.disconnectFromHost();
    return ok;
}

QJsonObject plan(const Catalog& catalog, const QString& root) {
    QJsonArray steps;
    int totalMB = 0;
    for (const auto& e : catalog.entries()) {
        const auto st = probeEntry(e, root);
        if (st.ready()) continue;
        QJsonObject step;
        step.insert("id", e.id);
        if (e.installType == QStringLiteral("manual")) {
            step.insert("action", "manual");
            step.insert("note", e.installNote);
        } else {
            step.insert("action", "download");
            step.insert("sizeMB", e.sizeMB);
            step.insert("sources", e.sources.size());
            totalMB += e.sizeMB;
        }
        steps.append(step);
    }
    return QJsonObject{{"steps", steps}, {"totalMB", totalMB}};
}

}

int runGateway(const QString& root, const QString& catalogDir, const QString& catalogError) {
    Catalog catalog;
    QString err;
    if (!catalog.load(catalogDir, &err)) {
        std::fprintf(stderr, "[mcphub-gateway] catalog load failed: %s\n", err.toUtf8().constData());
    }
    ConfigGen cfg(root, QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));

    const auto ovr = loadOverrides(root);
    AdbProvider adb(resolveToolExe(catalog, QStringLiteral("adb"), QStringLiteral("adb.exe"), root, ovr), root);
    if (adb.available()) {
        std::fprintf(stderr, "[mcphub-gateway] adb tools enabled: %s\n", adb.adbPath().toLocal8Bit().constData());
    }

    UpstreamProxy idaUp;
    QJsonArray idaTools;
    QString idaErr;
    bool idaReady = false;
    int idaTimeout = 180000;
    qint64 idaPid = 0;
    const auto* idaEntry = catalog.find(QStringLiteral("ida"));
    const QString idaDir = resolveToolDir(catalog, QStringLiteral("ida"), root, ovr);
    if (idaEntry) idaTimeout = idaEntry->mcpTimeoutMs;

    QString idaTarget;
    {
        QFile f(QDir(root).absoluteFilePath(QStringLiteral("state/ida-target.json")));
        if (f.open(QIODevice::ReadOnly)) {
            const QString p = QJsonDocument::fromJson(f.readAll()).object().value("path").toString();
            if (!p.isEmpty() && QFileInfo::exists(p)) idaTarget = p;
        }
    }

    QHash<QString, QJsonObject> idaSchemas;
    int idaUpstreamToolCount = 0;
    {
        QFile f(QDir(root).absoluteFilePath(QStringLiteral("ida-tools.json")));
        if (f.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            for (const auto& v : doc.object().value("tools").toArray()) {
                QJsonObject raw = v.toObject();
                const QString upName = raw.value("name").toString();

                QJsonObject schema = raw.value("inputSchema").toObject();
                if (schema.isEmpty()) schema.insert("type", "object");
                QJsonObject props = schema.value("properties").toObject();
                if (!props.contains("path")) {
                    props.insert("path", QJsonObject{
                        {"type", "string"},
                        {"description", "目标路径：首次调用必填（会自动打开 IDA），之后本会话可省略"}});
                }
                schema.insert("properties", props);
                schema.insert("additionalProperties", true);
                raw.insert("inputSchema", schema);

                idaSchemas.insert(upName, raw);
                ++idaUpstreamToolCount;
                raw.insert(QStringLiteral("name"), QStringLiteral("ida_") + upName);
                raw.insert(QStringLiteral("description"),
                           QStringLiteral("[ida] ") + raw.value("description").toString());
                idaTools.append(raw);
            }
        }
    }
    if (idaEntry && !idaDir.isEmpty()) {
        QJsonObject openSchema;
        openSchema.insert("type", "object");
        openSchema.insert("properties",
                          QJsonObject{{"path", QJsonObject{{"type", "string"},
                                                          {"description", "要分析的二进制 / APK 路径"}}}});
        openSchema.insert("required", QJsonArray{"path"});
        idaTools.prepend(QJsonObject{{"name", "ida_open"},
                                     {"description", "[ida] 启动 IDA 打开目标（复制到可写缓存、等待插件端口、建立连接）"},
                                     {"inputSchema", openSchema}});
        idaTools.prepend(QJsonObject{{"name", "ida_close"},
                                     {"description", "[ida] 关闭由 mcphub 启动的 IDA 实例与上游连接"},
                                     {"inputSchema", QJsonObject{{"type", "object"}, {"properties", QJsonObject()}}}});

        QJsonObject analyzeProp;
        analyzeProp.insert("path", QJsonObject{{"type", "string"},
                                               {"description", "目标路径；首次调用必填，之后可省略（记住上次目标）"}});
        analyzeProp.insert("functions", QJsonObject{{"type", "integer"},
                                                    {"description", "返回函数条数，默认 50"}});
        analyzeProp.insert("strings", QJsonObject{{"type", "integer"},
                                                  {"description", "返回字符串条数，默认 0（不列）"}});
        idaTools.prepend(QJsonObject{
            {"name", "ida_analyze"},
            {"description", "[ida] 一步分析：自动打开 IDA（如需），返回元数据 + 入口点 + 函数列表。"
                            "不需要先调用 ida_open"},
            {"inputSchema", QJsonObject{{"type", "object"}, {"properties", analyzeProp}}}});

        QJsonObject decProp;
        decProp.insert("name", QJsonObject{{"type", "string"}, {"description", "函数名"}});
        decProp.insert("address", QJsonObject{{"type", "string"}, {"description", "函数地址（与 name 二选一）"}});
        decProp.insert("path", QJsonObject{{"type", "string"}, {"description", "目标路径；首次调用必填"}});
        idaTools.prepend(QJsonObject{
            {"name", "ida_decompile"},
            {"description", "[ida] 按函数名或地址反编译：内部自动打开 IDA、名字转地址，直接返回伪代码"},
            {"inputSchema", QJsonObject{{"type", "object"}, {"properties", decProp}}}});
        idaReady = false;
    }

    auto startIdaUpstream = [&]() -> bool {
        if (idaReady) return true;
        if (!idaEntry || idaDir.isEmpty()) {
            idaErr = QStringLiteral("IDA 未登记安装目录");
            return false;
        }
        if (!portOpen(QStringLiteral("127.0.0.1"), idaEntry->mcpRpcPort)) {
            idaErr = QStringLiteral("IDA 插件未监听 127.0.0.1:%1").arg(idaEntry->mcpRpcPort);
            return false;
        }
        QStringList cmd;
        for (const auto& raw : idaEntry->mcpCommand) {
            QString a = raw;
            a.replace(QStringLiteral("{root}"), QDir::toNativeSeparators(idaDir));
            a.replace(QStringLiteral("{rpcPort}"), QString::number(idaEntry->mcpRpcPort));
            a.replace(QStringLiteral("{target}"), qEnvironmentVariable("MCPHUB_IDA_TARGET"));
            cmd << a;
        }
        if (cmd.isEmpty() || !QFileInfo::exists(cmd.first())) {
            idaErr = QStringLiteral("上游命令不存在：%1").arg(cmd.value(0));
            return false;
        }
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (auto it = idaEntry->mcpEnv.begin(); it != idaEntry->mcpEnv.end(); ++it) {
            QString v = it.value();
            v.replace(QStringLiteral("{root}"), QDir::toNativeSeparators(idaDir));
            env.insert(it.key(), v);
        }
        if (!idaUp.start(cmd.first(), cmd.mid(1), env, &idaErr)) return false;
        idaReady = true;
        idaErr.clear();
        std::fprintf(stderr, "[mcphub-gateway] ida upstream ready (%d tools)\n", static_cast<int>(idaTools.size()) - 2);
        return true;
    };

    if (idaEntry && !idaDir.isEmpty() && portOpen(QStringLiteral("127.0.0.1"), idaEntry->mcpRpcPort)) {
        startIdaUpstream();
    }

    auto idaText = [](const QJsonObject& r) {
        const auto c = r.value("content").toArray();
        return c.isEmpty() ? QString() : c.first().toObject().value("text").toString();
    };

    auto idaCall = [&](const QString& upName, const QJsonObject& a,
                       QJsonObject* res, QJsonObject* errObj) -> bool {
        QJsonObject req{{"name", upName}, {"arguments", a}};
        return idaUp.request(QStringLiteral("tools/call"), req, res, errObj, idaTimeout);
    };

    auto compactIdaError = [&](const QString& upName, const QString& text) -> QString {
        if (!text.contains(QStringLiteral("validation error"))) return text.left(1500);
        QStringList missing;
        QRegularExpression re(QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*\\r?\\n\\s*Field required"));
        auto it = re.globalMatch(text);
        while (it.hasNext()) missing << it.next().captured(1);
        if (missing.isEmpty()) return text.left(1500);
        const auto sch = idaSchemas.value(upName).value("inputSchema").toObject();
        return QStringLiteral("%1 缺少必填参数：%2。参数定义：%3")
            .arg(upName, missing.join(QStringLiteral(", ")),
                 QString::fromUtf8(QJsonDocument(sch).toJson(QJsonDocument::Compact)));
    };

    auto normalizeIdaArgs = [&](const QString& upName, QJsonObject a) {
        const auto sch = idaSchemas.value(upName).value("inputSchema").toObject();
        for (const auto& rv : sch.value("required").toArray()) {
            const QString k = rv.toString();
            if (a.contains(k)) continue;
            if (k == QLatin1String("offset") || k == QLatin1String("first")) a.insert(k, 0);
            else if (k == QLatin1String("count") || k == QLatin1String("limit")) a.insert(k, 200);
            else if (k == QLatin1String("size")) a.insert(k, 16);
        }
        return a;
    };

    auto idaJson = [&](const QString& upName, const QJsonObject& a, QString* err) -> QJsonValue {
        QJsonObject res, eo;
        if (!idaCall(upName, a, &res, &eo)) {
            if (err) *err = eo.value("message").toString();
            return QJsonValue();
        }
        if (res.value("isError").toBool()) {
            if (err) *err = compactIdaError(upName, idaText(res));
            return QJsonValue();
        }
        const QString t = idaText(res);
        const auto d = QJsonDocument::fromJson(t.toUtf8());
        if (d.isObject()) return d.object();
        if (d.isArray()) return d.array();
        return t;
    };

    auto openIda = [&](const QString& path, QString* msg) -> bool {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            *msg = QStringLiteral("目标不存在：%1").arg(path);
            return false;
        }
        if (idaReady) {
            *msg = QStringLiteral("IDA 已连接（%1）").arg(idaTarget);
            return true;
        }
        if (!idaEntry || idaDir.isEmpty()) {
            *msg = QStringLiteral("IDA 未登记安装目录，先在 mcphub 里登记");
            return false;
        }
        const QFileInfo fi(path);
        const QByteArray key = (fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.size()) +
                                QLatin1Char('|') + QString::number(fi.lastModified().toMSecsSinceEpoch()))
                                   .toUtf8();
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()).left(16);
        const QString cacheDir = QDir(root).absoluteFilePath(QStringLiteral("cache/ida/") + sha);
        QDir().mkpath(cacheDir);
        const QString cached = QDir(cacheDir).absoluteFilePath(fi.fileName());
        if (!QFileInfo::exists(cached) && !QFile::copy(fi.absoluteFilePath(), cached)) {
            *msg = QStringLiteral("复制目标到缓存失败：%1").arg(QDir::toNativeSeparators(cached));
            return false;
        }
        const QString idaExe = QDir(idaDir).absoluteFilePath(QStringLiteral("ida.exe"));
        if (!QFileInfo::exists(idaExe)) {
            *msg = QStringLiteral("找不到 ida.exe：%1").arg(QDir::toNativeSeparators(idaExe));
            return false;
        }

        QString openTarget = QDir::toNativeSeparators(cached);
        const QString dbFile = cached + QStringLiteral(".i64");
        if (qEnvironmentVariableIsSet("MCPHUB_IDA_KEEP_DB") && QFileInfo(dbFile).size() > 0) {
            openTarget = QDir::toNativeSeparators(dbFile);
        } else {
            static const QStringList kDbSuffix = {
                QStringLiteral("i64"), QStringLiteral("idb"), QStringLiteral("id0"),
                QStringLiteral("id1"), QStringLiteral("id2"), QStringLiteral("id3"),
                QStringLiteral("nam"), QStringLiteral("til")};
            QDir cdir(cacheDir);
            const QString stem = fi.fileName() + QLatin1Char('.');
            for (const auto& fn : cdir.entryList(QDir::Files)) {
                if (!fn.startsWith(stem, Qt::CaseInsensitive)) continue;
                if (kDbSuffix.contains(fn.mid(stem.size()).toLower())) QFile::remove(cdir.absoluteFilePath(fn));
            }
        }

        qint64 pid = 0;

        if (!QProcess::startDetached(idaExe, {QStringLiteral("-A"), openTarget},
                                     QDir::toNativeSeparators(idaDir), &pid)) {
            *msg = QStringLiteral("无法启动 IDA");
            return false;
        }
        idaPid = pid;
        bool up = false;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 90000) {
            if (portOpen(QStringLiteral("127.0.0.1"), idaEntry->mcpRpcPort, 300)) {
                QThread::msleep(400);
                if (portOpen(QStringLiteral("127.0.0.1"), idaEntry->mcpRpcPort, 300)) {
                    up = true;
                    break;
                }
                continue;
            }
            QThread::msleep(500);
        }
        if (!up) {
            killTree(idaPid);
            idaPid = 0;
            *msg = QStringLiteral("IDA 启动超时（90s）：插件未监听 %1，已关闭本次拉起的 IDA").arg(idaEntry->mcpRpcPort);
            return false;
        }
        if (!startIdaUpstream()) {
            *msg = QStringLiteral("IDA 已启动但上游连接失败：%1").arg(idaErr);
            return false;
        }
        idaTarget = path;
        const QString stateDir = QDir(root).absoluteFilePath(QStringLiteral("state"));
        QDir().mkpath(stateDir);
        QFile tf(QDir(stateDir).absoluteFilePath(QStringLiteral("ida-target.json")));
        if (tf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            tf.write(QJsonDocument(QJsonObject{{"path", path}}).toJson(QJsonDocument::Compact));
        }
        *msg = QStringLiteral("IDA 已打开：%1（pid %2）").arg(QDir::toNativeSeparators(cached)).arg(idaPid);
        return true;
    };

    auto ensureIda = [&](const QString& pathHint, QString* err) -> bool {
        if (idaReady) return true;
        if (!idaEntry || idaDir.isEmpty()) {
            *err = QStringLiteral("IDA 未登记安装目录：右键 IDA PRO → 登记安装目录");
            return false;
        }
        if (portOpen(QStringLiteral("127.0.0.1"), idaEntry->mcpRpcPort)) {
            if (startIdaUpstream()) return true;
        }
        const QString path = pathHint.isEmpty() ? idaTarget : pathHint;
        if (path.isEmpty()) {
            *err = QStringLiteral("还没有目标：首次调用请带 path，例如 {\"path\": \"D:/sample/libnative.so\"}，"
                                  "之后本会话不再需要（也可直接用 ida_analyze）");
            return false;
        }
        return openIda(path, err);
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        QJsonParseError pe{};
        const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(line), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            send(error(QJsonValue(), -32700, QStringLiteral("parse error: %1").arg(pe.errorString())));
            continue;
        }
        const auto req = doc.object();
        const auto id = req.value("id");
        const QString m = req.value("method").toString();
        const auto params = req.value("params").toObject();

        if (!req.contains("id")) continue;

        if (m == QStringLiteral("initialize")) {
            QJsonObject caps;
            caps.insert("tools", QJsonObject{{"listChanged", false}});
            QJsonObject info;
            info.insert("name", "mcphub");
            info.insert("version", QString::fromLatin1(Version));
            QJsonObject r;
            r.insert("protocolVersion", QString::fromLatin1(kProtocolVersion));
            r.insert("capabilities", caps);
            r.insert("serverInfo", info);
            send(result(id, r));
            continue;
        }
        if (m == QStringLiteral("ping")) {
            send(result(id, QJsonObject{}));
            continue;
        }
        if (m == QStringLiteral("tools/list")) {
            QJsonArray tools = toolList();
            if (adb.available()) {
                for (const auto& v : AdbProvider::toolDefs()) {
                    QJsonObject t = v.toObject();
                    t.insert("name", QStringLiteral("adb_") + t.value("name").toString());
                    t.insert("description", QStringLiteral("[adb] ") + t.value("description").toString());
                    tools.append(t);
                }
            }
            for (const auto& v : idaTools) tools.append(v);
            send(result(id, QJsonObject{{"tools", tools}}));
            continue;
        }
        if (m == QStringLiteral("tools/call")) {
            const QString name = params.value("name").toString();
            const auto args = params.value("arguments").toObject();
            if (name == QStringLiteral("ida_open")) {
                QString msg;
                const bool ok = openIda(args.value("path").toString(), &msg);
                send(result(id, textResult(msg, !ok)));
                continue;
            }
            if (name == QStringLiteral("ida_close")) {
                idaUp.stop();
                idaReady = false;
                if (idaPid > 0) {
                    killTree(idaPid);
                    idaPid = 0;
                    waitPortClosed(idaEntry ? idaEntry->mcpRpcPort : 13337, 8000);
                }
                send(result(id, textResult(QStringLiteral("IDA 已关闭"))));
                continue;
            }
            if (name == QStringLiteral("ida_analyze")) {
                QString err;
                if (!ensureIda(args.value("path").toString(), &err)) {
                    send(result(id, textResult(err, true)));
                    continue;
                }
                const int fnCount = args.contains("functions") ? qBound(1, args.value("functions").toInt(), 2000) : 50;
                const int strCount = args.value("strings").toInt(0);
                QJsonObject out;
                out.insert("target", idaTarget);
                QString e1;
                const auto meta = idaJson(QStringLiteral("get_metadata"), {}, &e1);
                out.insert("metadata", meta.isUndefined() ? QJsonValue(e1) : meta);
                QString e2;
                const auto eps = idaJson(QStringLiteral("get_entry_points"), {}, &e2);
                if (!eps.isUndefined()) out.insert("entryPoints", eps);
                QString e3;
                const auto fns = idaJson(QStringLiteral("list_functions"),
                                         QJsonObject{{"offset", 0}, {"count", fnCount}}, &e3);
                if (!fns.isUndefined()) out.insert("functions", fns);
                if (strCount > 0) {
                    QString e4;
                    const auto strs = idaJson(QStringLiteral("list_strings"),
                                              QJsonObject{{"offset", 0}, {"count", strCount}}, &e4);
                    if (!strs.isUndefined()) out.insert("strings", strs);
                }
                send(result(id, textResult(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented)))));
                continue;
            }
            if (name == QStringLiteral("ida_decompile")) {
                QString err;
                if (!ensureIda(args.value("path").toString(), &err)) {
                    send(result(id, textResult(err, true)));
                    continue;
                }
                QString address = args.value("address").toString();
                const QString fname = args.value("name").toString();
                QJsonObject out;
                if (address.isEmpty() && !fname.isEmpty()) {
                    QString e;
                    const auto info = idaJson(QStringLiteral("get_function_by_name"),
                                              QJsonObject{{"name", fname}}, &e);
                    if (info.isObject()) address = info.toObject().value("address").toString();
                    if (address.isEmpty() && info.isString()) {
                        const auto m = QRegularExpression(QStringLiteral("0x[0-9a-fA-F]+"))
                                           .match(info.toString());
                        if (m.hasMatch()) address = m.captured(0);
                    }
                    if (address.isEmpty()) {
                        send(result(id, textResult(QStringLiteral("找不到函数 %1：%2").arg(fname, e), true)));
                        continue;
                    }
                    out.insert("name", fname);
                }
                if (address.isEmpty()) {
                    send(result(id, textResult(QStringLiteral("需要提供 name 或 address"), true)));
                    continue;
                }
                QString e;
                const auto code = idaJson(QStringLiteral("decompile_function"),
                                          QJsonObject{{"address", address}}, &e);
                if (code.isUndefined()) {
                    send(result(id, textResult(e, true)));
                    continue;
                }
                out.insert("address", address);
                out.insert("code", code);
                send(result(id, textResult(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented)))));
                continue;
            }
            if (name.startsWith(QStringLiteral("ida_"))) {
                const QString upName = name.mid(4);
                QJsonObject a = args;
                const QString pathHint = a.take(QStringLiteral("path")).toString();
                QString err;
                if (!ensureIda(pathHint, &err)) {
                    send(result(id, textResult(err, true)));
                    continue;
                }
                a = normalizeIdaArgs(upName, a);
                QJsonObject res, errObj;
                if (!idaCall(upName, a, &res, &errObj)) {
                    send(result(id, textResult(QStringLiteral("IDA 调用失败：%1").arg(errObj.value("message").toString()), true)));
                    continue;
                }
                if (res.value("isError").toBool()) {
                    send(result(id, textResult(compactIdaError(upName, idaText(res)), true)));
                    continue;
                }
                send(result(id, res));
                continue;
            }
            if (name.startsWith(QStringLiteral("adb_"))) {
                bool isError = false;
                const auto res = adb.call(name.mid(4), args, &isError);
                send(result(id, res));
                continue;
            }
            if (name == QStringLiteral("hub_status")) {
                auto snap = snapshot(catalog, root, catalogDir, catalogError);
                snap.insert("upstreams", QJsonObject{{"ida", QJsonObject{{"ready", idaReady},
                                                                       {"tools", idaUpstreamToolCount},
                                                                       {"target", idaTarget},
                                                                       {"error", idaErr}}}});
                send(result(id, textResult(QString::fromUtf8(QJsonDocument(snap).toJson(QJsonDocument::Indented)))));
            } else if (name == QStringLiteral("hub_tools")) {
                QJsonArray arr;
                for (const auto& e : catalog.entries()) {
                    const auto st = probeEntry(e, root);
                    QJsonObject o;
                    o.insert("id", st.id);
                    o.insert("state", stateKey(st.state));
                    o.insert("version", st.version);
                    o.insert("path", st.path);
                    arr.append(o);
                }
                send(result(id, textResult(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented)))));
            } else if (name == QStringLiteral("hub_install_plan")) {
                const auto p = plan(catalog, root);
                send(result(id, textResult(QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Indented)))));
            } else if (name == QStringLiteral("hub_config")) {
                const QString client = args.value("client").toString(QStringLiteral("standard"));
                const auto s = cfg.snippet(client);
                send(result(id, textResult(QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Indented)))));
            } else {
                send(result(id, textResult(QStringLiteral("未知工具: %1").arg(name), true)));
            }
            continue;
        }
        send(error(id, -32601, QStringLiteral("method not found: %1").arg(m)));
    }
    idaUp.stop();
    if (idaPid > 0 && !qEnvironmentVariableIsSet("MCPHUB_IDA_KEEP")) {
        killTree(idaPid);
    }
    return 0;
}

}
