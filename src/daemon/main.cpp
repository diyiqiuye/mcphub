#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextStream>

#include <cstdio>

#include "daemon/catalog.h"
#include "daemon/configgen.h"
#include "daemon/gateway.h"
#include "daemon/ipcserver.h"
#include "shared/protocol.h"

using namespace mcphub;

namespace {

QString resolveRoot() {
    const QString env = qEnvironmentVariable("MCPHUB_ROOT");
    if (!env.isEmpty()) return QDir::cleanPath(env);

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {appDir, QDir(appDir).absoluteFilePath(QStringLiteral("..")),
                                    QDir(appDir).absoluteFilePath(QStringLiteral("../.."))};
    for (const auto& c : candidates) {
        if (QFileInfo::exists(QDir(c).absoluteFilePath(QStringLiteral("catalog")))) {
            return QDir::cleanPath(c);
        }
    }
    return QDir::cleanPath(appDir);
}

QString resolveCatalog(const QString& root) {
    const QString env = qEnvironmentVariable("MCPHUB_CATALOG");
    if (!env.isEmpty()) return QDir::cleanPath(env);
    return QDir(root).absoluteFilePath(QStringLiteral("catalog"));
}

void printJson(const QJsonObject& o) {
    const QByteArray b = QJsonDocument(o).toJson(QJsonDocument::Indented);
    std::fwrite(b.constData(), 1, static_cast<size_t>(b.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

int runDoctor(const QString& root) {
    QProcess p;
    p.setProgram(QCoreApplication::applicationFilePath());
    p.setArguments({QStringLiteral("--gateway")});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("MCPHUB_ROOT"), root);
    p.setProcessEnvironment(env);
    p.start();
    if (!p.waitForStarted(5000)) {
        printJson(QJsonObject{{"ok", false},
                              {"error", QStringLiteral("无法启动 gateway: %1").arg(p.errorString())},
                              {"hint", QStringLiteral("多半是 Qt DLL 不在 exe 同目录或 PATH 里")}});
        return 1;
    }
    p.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n");
    p.write("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n");
    p.closeWriteChannel();

    QByteArray buf;
    int tools = -1;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000 && tools < 0) {
        if (!p.waitForReadyRead(400)) {
            if (p.state() == QProcess::NotRunning) break;
            continue;
        }
        buf += p.readAllStandardOutput();
        int idx;
        while ((idx = buf.indexOf('\n')) >= 0 && tools < 0) {
            const QByteArray line = buf.left(idx);
            buf.remove(0, idx + 1);
            const auto doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const auto o = doc.object();
            if (o.value("id").toInt() == 2) {
                tools = o.value("result").toObject().value("tools").toArray().size();
            }
        }
    }
    const bool stillRunning = p.state() != QProcess::NotRunning;
    const int code = stillRunning ? -1 : p.exitCode();
    if (stillRunning) p.kill();
    p.waitForFinished(1000);
    const QString stderrText = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();

    QJsonObject out;
    out.insert("ok", tools >= 0);
    out.insert("tools", tools);
    out.insert("exitCode", code);
    if (!stderrText.isEmpty()) out.insert("stderr", stderrText.left(400));
    if (tools < 0) {
        out.insert("error", QStringLiteral("gateway 没有返回 tools/list"));
        if (code == -1073741515) {
            out.insert("hint", QStringLiteral("0xC0000135：缺 Qt DLL。用 windeployqt 部署到 exe 同目录，或把 C:\\msys64\\ucrt64\\bin 加进 PATH"));
        }
    }
    printJson(out);
    return tools >= 0 ? 0 : 1;
}

int runCli(const QString& cmd, const QString& root, const QString& catalogDir, const QStringList& cliArgs) {
    Catalog catalog;
    QString err;
    catalog.load(catalogDir, &err);
    Q_UNUSED(err)

    if (cmd == QStringLiteral("plan")) {
        QJsonArray steps;
        int totalMB = 0;
        for (const auto& e : catalog.entries()) {
            const auto st = probeEntry(e, root);
            if (st.ready()) continue;
            QJsonObject step;
            step.insert("id", e.id);
            step.insert("action", e.installType == QStringLiteral("manual") ? "manual" : "download");
            step.insert("sizeMB", e.sizeMB);
            steps.append(step);
            if (e.installType != QStringLiteral("manual")) totalMB += e.sizeMB;
        }
        printJson(QJsonObject{{"steps", steps}, {"totalMB", totalMB}});
        return 0;
    }
    if (cmd == QStringLiteral("config")) {
        ConfigGen cfg(root, QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        const QString client = cliArgs.size() > 1 ? cliArgs.at(1) : QStringLiteral("standard");
        printJson(cfg.snippet(client).value("config").toObject());
        return 0;
    }

    QJsonArray tools;
    int ready = 0;
    for (const auto& e : catalog.entries()) {
        const auto st = probeEntry(e, root);
        tools.append(toJson(st));
        if (st.ready()) ++ready;
    }
    printJson(QJsonObject{{"tools", tools},
                          {"hub", QJsonObject{{"version", QString::fromLatin1(Version)},
                                              {"root", QDir::toNativeSeparators(root)},
                                              {"catalogDir", QDir::toNativeSeparators(catalogDir)},
                                              {"catalogCount", catalog.entries().size()},
                                              {"ready", ready},
                                              {"total", catalog.entries().size()},
                                              {"pipe", pipeName()}}}});
    return 0;
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mcphubd"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("mcphub"));

    QStringList args = app.arguments();
    args.removeFirst();

    const QString root = resolveRoot();
    const QString catalogDir = resolveCatalog(root);
    QString catalogError;
    {
        Catalog probe;
        probe.load(catalogDir, &catalogError);
    }

    if (args.contains(QStringLiteral("--version"))) {
        std::printf("mcphubd %s\nroot: %s\ncatalog: %s\n", qUtf8Printable(QString::fromLatin1(Version)),
                    qUtf8Printable(root), qUtf8Printable(catalogDir));
        return 0;
    }
    if (args.contains(QStringLiteral("--doctor"))) {
        return runDoctor(root);
    }
    if (args.contains(QStringLiteral("--gateway"))) {
        return runGateway(root, catalogDir, catalogError);
    }
    const int cliIdx = args.indexOf(QStringLiteral("--cli"));
    if (cliIdx >= 0) {
        const QString cmd = (cliIdx + 1 < args.size()) ? args.at(cliIdx + 1) : QStringLiteral("status");
        return runCli(cmd, root, catalogDir, args.mid(cliIdx + 1));
    }

    {
        QLocalSocket probeSocket;
        probeSocket.connectToServer(pipeName());
        if (probeSocket.waitForConnected(300)) {
            std::fprintf(stderr, "[mcphubd] already running on %s\n", qUtf8Printable(pipeName()));
            return 0;
        }
    }

    IpcServer server(root, catalogDir, catalogError);
    QString err;
    if (!server.start(&err)) {
        std::fprintf(stderr, "[mcphubd] %s\n", qUtf8Printable(err));
        return 1;
    }
    return app.exec();
}
