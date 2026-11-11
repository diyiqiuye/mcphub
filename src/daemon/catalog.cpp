#include "daemon/catalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace mcphub {

QJsonObject CatalogEntry::toJson() const {
    QJsonObject o;
    o.insert("id", id);
    o.insert("name", name);
    o.insert("category", category);
    o.insert("version", version);
    o.insert("license", license);
    o.insert("sizeMB", sizeMB);
    o.insert("requires", QJsonArray::fromStringList(requires));
    o.insert("probe", QJsonArray::fromStringList(probe));
    o.insert("installType", installType);
    o.insert("dest", dest);
    o.insert("note", installNote);
    QJsonArray src;
    for (const auto& s : sources) {
        QJsonObject so;
        so.insert("name", s.name);
        so.insert("url", s.url);
        so.insert("sha256", s.sha256);
        src.append(so);
    }
    o.insert("sources", src);
    o.insert("verifyMatch", verifyMatch);
    o.insert("mcpMode", mcpMode);
    o.insert("provider", provider);
    o.insert("namespace", mcpNamespace);
    o.insert("tools", QJsonArray::fromStringList(mcpTools));
    return o;
}

bool Catalog::load(const QString& catalogDir, QString* error) {
    m_dir = catalogDir;
    m_entries.clear();
    QDir dir(catalogDir);
    if (!dir.exists()) {
        if (error) *error = QStringLiteral("catalog 目录不存在: %1").arg(catalogDir);
        return false;
    }
    const auto files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const auto& fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("无法读取 %1").arg(fi.fileName());
            continue;
        }
        QJsonParseError pe{};
        const auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error) *error = QStringLiteral("%1 解析失败: %2").arg(fi.fileName(), pe.errorString());
            continue;
        }
        const auto o = doc.object();
        CatalogEntry e;
        e.raw = o;
        e.id = o.value("id").toString();
        if (e.id.isEmpty()) e.id = fi.completeBaseName();
        e.name = o.value("name").toString(e.id);
        e.category = o.value("category").toString();
        e.version = o.value("version").toString();
        e.license = o.value("license").toString();
        e.sizeMB = o.value("sizeMB").toInt();
        for (const auto& v : o.value("requires").toArray()) e.requires << v.toString();
        for (const auto& v : o.value("probe").toArray()) e.probe << v.toString();

        const auto inst = o.value("install").toObject();
        e.installType = inst.value("type").toString();
        e.dest = inst.value("dest").toString(QStringLiteral("tools/") + e.id);
        e.installNote = inst.value("note").toString();
        for (const auto& sv : inst.value("sources").toArray()) {
            const auto so = sv.toObject();
            CatalogSource s;
            s.name = so.value("name").toString();
            s.url = so.value("url").toString();
            s.sha256 = so.value("sha256").toString();
            if (!s.url.isEmpty()) e.sources.append(s);
        }

        const auto ver = o.value("verify").toObject();
        for (const auto& v : ver.value("cmd").toArray()) e.verifyCmd << v.toString();
        const auto venv = ver.value("env").toObject();
        for (auto it = venv.begin(); it != venv.end(); ++it) e.verifyEnv.insert(it.key(), it.value().toString());
        e.verifyMatch = ver.value("match").toString();
        e.verifyTimeoutMs = ver.value("timeoutMs").toInt(20000);

        const auto mcp = o.value("mcp").toObject();
        e.mcpMode = mcp.value("mode").toString();
        e.provider = mcp.value("provider").toString();
        e.mcpNamespace = mcp.value("namespace").toString(e.id);
        for (const auto& v : mcp.value("tools").toArray()) e.mcpTools << v.toString();
        for (const auto& v : mcp.value("command").toArray()) e.mcpCommand << v.toString();
        const auto menv = mcp.value("env").toObject();
        for (auto it = menv.begin(); it != menv.end(); ++it) e.mcpEnv.insert(it.key(), it.value().toString());
        e.mcpRpcPort = mcp.value("rpcPort").toInt(13337);
        e.mcpTimeoutMs = mcp.value("timeoutMs").toInt(120000);

        m_entries.append(e);
    }
    return true;
}

const CatalogEntry* Catalog::find(const QString& id) const {
    for (const auto& e : m_entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

bool pathUnder(const QString& path, const QString& root) {
    const QString r = QDir(root).canonicalPath();
    const QString p = QFileInfo(path).canonicalFilePath();
    if (r.isEmpty() || p.isEmpty()) return false;
    return p.startsWith(r, Qt::CaseInsensitive);
}

ToolStatus probeEntry(const CatalogEntry& e, const QString& root) {
    ToolStatus s;
    s.id = e.id;
    s.name = e.name;
    s.category = e.category;
    s.version = e.version;
    s.manual = (e.installType == QStringLiteral("manual"));
    s.note = e.installNote;

    QStringList probes = e.probe;
    if (!e.dest.isEmpty()) probes.prepend(e.dest);

    for (const auto& raw : probes) {
        const QString p = expandPath(raw, root);
        QFileInfo fi(p);
        if (!fi.exists()) continue;
        s.path = QDir::toNativeSeparators(fi.absoluteFilePath());
        s.external = !pathUnder(fi.absoluteFilePath(), root);
        s.state = ToolState::Ready;
        s.progress = 1.0;
        s.message = s.external ? QStringLiteral("外部安装") : QStringLiteral("就绪");
        return s;
    }
    s.state = ToolState::Absent;
    s.progress = 0.0;
    s.message = e.installType == QStringLiteral("manual") ? QStringLiteral("需手动指定路径")
                                                        : QStringLiteral("未安装");
    return s;
}

}
