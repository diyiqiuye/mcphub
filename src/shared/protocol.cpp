#include "shared/protocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>

namespace mcphub {

QString stateKey(ToolState s) {
    switch (s) {
    case ToolState::Absent: return "absent";
    case ToolState::Planning: return "planning";
    case ToolState::Downloading: return "downloading";
    case ToolState::Installing: return "installing";
    case ToolState::Verifying: return "verifying";
    case ToolState::Ready: return "ready";
    case ToolState::Running: return "running";
    case ToolState::Degraded: return "degraded";
    case ToolState::Broken: return "broken";
    }
    return "unknown";
}

ToolState stateFromKey(const QString& key) {
    if (key == "planning") return ToolState::Planning;
    if (key == "downloading") return ToolState::Downloading;
    if (key == "installing") return ToolState::Installing;
    if (key == "verifying") return ToolState::Verifying;
    if (key == "ready") return ToolState::Ready;
    if (key == "running") return ToolState::Running;
    if (key == "degraded") return ToolState::Degraded;
    if (key == "broken") return ToolState::Broken;
    return ToolState::Absent;
}

QString stateGlyph(ToolState s) {
    switch (s) {
    case ToolState::Absent: return QStringLiteral("\u25cb");
    case ToolState::Planning: return QStringLiteral("\u25d0");
    case ToolState::Downloading: return QStringLiteral("\u25d0");
    case ToolState::Installing: return QStringLiteral("\u25d0");
    case ToolState::Verifying: return QStringLiteral("\u25d0");
    case ToolState::Ready: return QStringLiteral("\u25cf");
    case ToolState::Running: return QStringLiteral("\u25cf");
    case ToolState::Degraded: return QStringLiteral("\u25d1");
    case ToolState::Broken: return QStringLiteral("\u2715");
    }
    return QStringLiteral("?");
}

QJsonObject toJson(const ToolStatus& t) {
    QJsonObject o;
    o.insert("id", t.id);
    o.insert("name", t.name);
    o.insert("category", t.category);
    o.insert("version", t.version);
    o.insert("state", stateKey(t.state));
    o.insert("progress", t.progress);
    o.insert("message", t.message);
    o.insert("path", t.path);
    o.insert("external", t.external);
    o.insert("manual", t.manual);
    o.insert("note", t.note);
    o.insert("verified", t.verified);
    o.insert("verifyOk", t.verifyOk);
    o.insert("verifyText", t.verifyText);
    return o;
}

ToolStatus toolStatusFromJson(const QJsonObject& o) {
    ToolStatus t;
    t.id = o.value("id").toString();
    t.name = o.value("name").toString(t.id);
    t.category = o.value("category").toString();
    t.version = o.value("version").toString();
    t.state = stateFromKey(o.value("state").toString());
    t.progress = o.value("progress").toDouble();
    t.message = o.value("message").toString();
    t.path = o.value("path").toString();
    t.external = o.value("external").toBool();
    t.manual = o.value("manual").toBool();
    t.note = o.value("note").toString();
    t.verified = o.value("verified").toBool();
    t.verifyOk = o.value("verifyOk").toBool();
    t.verifyText = o.value("verifyText").toString();
    return t;
}

QString pipeName() {
    QString user = qEnvironmentVariable("USERNAME", "user").toLower();
    user.replace(QRegularExpression("[^a-z0-9_-]"), "");
    return QStringLiteral("mcphub-") + user;
}

QString expandPath(const QString& raw, const QString& root) {
    if (raw.isEmpty()) return QString();
    QString s = raw;

    static const QRegularExpression re(QStringLiteral("%([A-Za-z0-9_]+)%"));
    QString out;
    int last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += qEnvironmentVariable(m.captured(1).toLocal8Bit().constData());
        last = m.capturedEnd();
    }
    out += s.mid(last);
    if (out.startsWith('~')) out = QDir::homePath() + out.mid(1);
    QDir d(out);
    if (d.isAbsolute()) return QDir::cleanPath(out);
    return QDir::cleanPath(QDir(root).absoluteFilePath(out));
}

}
