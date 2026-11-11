#pragma once

#include <QString>
#include <QJsonObject>

namespace mcphub {

namespace method {
inline constexpr auto AppInfo = "app.info";
inline constexpr auto CatalogList = "catalog.list";
inline constexpr auto StatusGet = "status.get";
inline constexpr auto ProvisionPlan = "provision.plan";
inline constexpr auto ProvisionApply = "provision.apply";
inline constexpr auto ServiceStart = "service.start";
inline constexpr auto ServiceStop = "service.stop";
inline constexpr auto ConfigSnippet = "config.snippet";
inline constexpr auto ConfigWrite = "config.write";
inline constexpr auto LogTail = "log.tail";
inline constexpr auto FsOpen = "fs.open";
inline constexpr auto ToolSetPath = "tool.set_path";
inline constexpr auto ToolVerify = "tool.verify";
inline constexpr auto ToolForget = "tool.forget";
inline constexpr auto IdaAutostart = "ida.autostart";
}

namespace event {
inline constexpr auto Log = "event.log";
inline constexpr auto Status = "event.status";
inline constexpr auto Plan = "event.plan";
inline constexpr auto Done = "event.done";
inline constexpr auto Hello = "event.hello";
}

namespace errc {
inline constexpr int NoTool = 1001;
inline constexpr int Busy = 1002;
inline constexpr int Download = 1003;
inline constexpr int Verify = 1004;
inline constexpr int Perm = 1005;
inline constexpr int Ipc = 1006;
inline constexpr int NotImplemented = 1007;
}

enum class ToolState {
    Absent,
    Planning,
    Downloading,
    Installing,
    Verifying,
    Ready,
    Running,
    Degraded,
    Broken,
};

QString stateKey(ToolState s);
ToolState stateFromKey(const QString& key);
QString stateGlyph(ToolState s);

struct ToolStatus {
    QString id;
    QString name;
    QString category;
    QString version;
    ToolState state = ToolState::Absent;
    double progress = 0.0;
    QString message;
    QString path;
    bool external = false;
    bool manual = false;
    QString note;
    bool verified = false;
    bool verifyOk = false;
    QString verifyText;
    bool ready() const { return state == ToolState::Ready || state == ToolState::Running; }
};

QJsonObject toJson(const ToolStatus& t);
ToolStatus toolStatusFromJson(const QJsonObject& o);

QString pipeName();

QString expandPath(const QString& raw, const QString& root);

inline constexpr auto AppName = "MCP Hub";
inline constexpr auto Version = "0.1.6";
inline constexpr auto DaemonExe = "mcphubd.exe";
inline constexpr auto GuiExe = "mcphub.exe";

}
