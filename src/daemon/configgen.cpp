#include "daemon/configgen.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>

namespace mcphub {

ConfigGen::ConfigGen(QString root, QString daemonPath)
    : m_root(std::move(root)), m_daemon(std::move(daemonPath)) {}

QVector<ClientTarget> ConfigGen::clients() const {

    const QString home = qEnvironmentVariable("MCPHUB_HOME", QDir::homePath());
    const QString appData = qEnvironmentVariable("APPDATA");
    return {
        {QStringLiteral("zcode"), QStringLiteral("ZCode"), home + "/.zcode/cli/config.json"},
        {QStringLiteral("cursor"), QStringLiteral("Cursor"), home + "/.cursor/mcp.json"},
        {QStringLiteral("claude-desktop"), QStringLiteral("Claude Desktop"),
         appData + "/Claude/claude_desktop_config.json"},
        {QStringLiteral("claude-code"), QStringLiteral("Claude Code"), home + "/.claude.json"},
    };
}

const ClientTarget* ConfigGen::find(const QString& key) const {
    static QVector<ClientTarget> cache;
    cache = clients();
    for (const auto& c : cache) {
        if (c.key == key) return &c;
    }
    return nullptr;
}

namespace {

QString derivedRoot(const QString& daemonPath) {
    const QString appDir = QFileInfo(daemonPath).absolutePath();
    const QStringList candidates = {appDir, QDir(appDir).absoluteFilePath(QStringLiteral("..")),
                                    QDir(appDir).absoluteFilePath(QStringLiteral("../.."))};
    for (const auto& c : candidates) {
        if (QFileInfo::exists(QDir(c).absoluteFilePath(QStringLiteral("catalog")))) {
            return QDir::cleanPath(c);
        }
    }
    return QDir::cleanPath(appDir);
}
}

QJsonObject ConfigGen::serverEntry() const {
    QJsonObject o;
    o.insert("command", QDir::toNativeSeparators(m_daemon));
    o.insert("args", QJsonArray{QStringLiteral("--gateway")});

    if (QDir::cleanPath(m_root).compare(derivedRoot(m_daemon), Qt::CaseInsensitive) != 0) {
        o.insert("env", QJsonObject{{"MCPHUB_ROOT", QDir::toNativeSeparators(m_root)}});
    }
    return o;
}

QJsonObject ConfigGen::snippet(const QString& client) const {

    if (client == QStringLiteral("standard") || client.isEmpty()) {
        QJsonObject servers;
        servers.insert("mcphub", serverEntry());
        QJsonObject out;
        out.insert("client", QStringLiteral("standard"));
        out.insert("label", QStringLiteral("Standard (mcpServers)"));
        out.insert("file", QString());
        out.insert("exists", false);
        out.insert("config", QJsonObject{{"mcpServers", servers}});
        return out;
    }

    const auto* target = find(client);
    QJsonObject out;
    if (!target) {
        out.insert("error", QStringLiteral("unknown client"));
        return out;
    }
    QJsonObject wrapper;
    if (client == QStringLiteral("zcode")) {
        QJsonObject servers;
        servers.insert("mcphub", serverEntry());
        QJsonObject mcp;
        mcp.insert("servers", servers);
        wrapper.insert("mcp", mcp);
    } else {
        QJsonObject servers;
        servers.insert("mcphub", serverEntry());
        wrapper.insert("mcpServers", servers);
    }
    out.insert("client", client);
    out.insert("label", target->label);
    out.insert("file", QDir::toNativeSeparators(target->file));
    out.insert("exists", QFileInfo::exists(target->file));
    out.insert("config", wrapper);
    return out;
}

bool ConfigGen::write(const QString& client, QString* backup, QString* error) const {
    const auto* target = find(client);
    if (!target) {
        if (error) *error = QStringLiteral("未知客户端: %1").arg(client);
        return false;
    }
    QJsonObject root;
    if (QFileInfo::exists(target->file)) {
        QFile in(target->file);
        if (!in.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("无法读取 %1").arg(target->file);
            return false;
        }
        QJsonParseError pe{};
        const auto doc = QJsonDocument::fromJson(in.readAll(), &pe);
        in.close();
        if (pe.error != QJsonParseError::NoError) {
            if (error) *error = QStringLiteral("现有配置不是合法 JSON（%1），已中止，避免破坏").arg(pe.errorString());
            return false;
        }
        if (doc.isObject()) root = doc.object();
        const QString bak = target->file + QStringLiteral(".bak-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        QFile::copy(target->file, bak);
        if (backup) *backup = QDir::toNativeSeparators(bak);
    }

    if (client == QStringLiteral("zcode")) {
        auto mcp = root.value("mcp").toObject();
        auto servers = mcp.value("servers").toObject();
        servers.insert("mcphub", serverEntry());
        mcp.insert("servers", servers);
        root.insert("mcp", mcp);
    } else {
        auto servers = root.value("mcpServers").toObject();
        servers.insert("mcphub", serverEntry());
        root.insert("mcpServers", servers);
    }

    QDir().mkpath(QFileInfo(target->file).absolutePath());
    QFile out(target->file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("无法写入 %1").arg(target->file);
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
    return true;
}

}
