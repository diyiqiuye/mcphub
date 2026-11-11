#include "daemon/adbprovider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include "shared/protocol.h"

namespace mcphub {
namespace {

QJsonObject prop(const QString& type, const QString& desc, bool required = false) {
    QJsonObject p;
    p.insert("type", type);
    p.insert("description", desc);
    if (required) p.insert("required", true);
    return p;
}

QJsonObject toolDef(const QString& name, const QString& desc, const QJsonObject& props,
                    const QStringList& required = {}) {
    QJsonObject schema;
    schema.insert("type", "object");
    schema.insert("properties", props);
    if (!required.isEmpty()) {
        QJsonArray req;
        for (const auto& r : required) req.append(r);
        schema.insert("required", req);
    }
    QJsonObject t;
    t.insert("name", name);
    t.insert("description", desc);
    t.insert("inputSchema", schema);
    return t;
}

QString firstLine(const QString& s, int max = 200) {
    const QString t = s.simplified();
    return t.size() > max ? t.left(max) + QStringLiteral("…") : t;
}

}

AdbProvider::AdbProvider(QString adbExe, QString root) : m_adb(std::move(adbExe)), m_root(std::move(root)) {}

bool AdbProvider::available() const {
    return !m_adb.isEmpty() && QFileInfo::exists(m_adb);
}

QString AdbProvider::fastbootExe() const {
    return QDir(QFileInfo(m_adb).absolutePath()).absoluteFilePath(QStringLiteral("fastboot.exe"));
}

QJsonArray AdbProvider::toolDefs() {
    QJsonArray arr;

    arr.append(toolDef("devices", "列出已连接设备（adb devices -l）", {}));

    {
        QJsonObject p;
        p.insert("serial", prop("string", "可选：设备序列号（多设备时必填）"));
        arr.append(toolDef("device_info", "读取设备型号/品牌/Android 版本/SDK/ABI", p));
    }
    {
        QJsonObject p;
        p.insert("command", prop("string", "要执行的 adb shell 命令（如 pm list packages、dumpsys battery）", true));
        p.insert("serial", prop("string", "可选：设备序列号"));
        p.insert("timeoutSec", prop("number", "超时秒数，默认 60"));
        arr.append(toolDef("shell", "在设备上执行 shell 命令并返回 stdout/stderr", p, {"command"}));
    }
    {
        QJsonObject p;
        p.insert("local", prop("string", "本地文件/目录路径", true));
        p.insert("remote", prop("string", "设备目标路径（如 /sdcard/Download/a.apk）", true));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("push", "推送文件到设备（adb push）", p, {"local", "remote"}));
    }
    {
        QJsonObject p;
        p.insert("remote", prop("string", "设备上的文件路径", true));
        p.insert("local", prop("string", "本地保存路径", true));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("pull", "从设备拉取文件（adb pull）", p, {"remote", "local"}));
    }
    {
        QJsonObject p;
        p.insert("apk", prop("string", "APK 文件路径", true));
        p.insert("reinstall", prop("boolean", "覆盖安装（-r），默认 true"));
        p.insert("downgrade", prop("boolean", "允许降级（-d）"));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("install", "安装 APK（adb install）", p, {"apk"}));
    }
    {
        QJsonObject p;
        p.insert("package", prop("string", "包名，如 com.example.app", true));
        p.insert("keepData", prop("boolean", "保留数据与缓存（-k）"));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("uninstall", "卸载应用（adb uninstall）", p, {"package"}));
    }
    {
        QJsonObject p;
        p.insert("thirdParty", prop("boolean", "只看第三方应用（-3）"));
        p.insert("filter", prop("string", "按包名子串过滤"));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("packages", "列出已安装应用包名（pm list packages）", p));
    }
    {
        QJsonObject p;
        p.insert("lines", prop("number", "尾部行数，默认 200（adb logcat -d -t N）"));
        p.insert("filter", prop("string", "logcat 过滤表达式，如 ActivityManager:I *:S"));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("logcat", "抓取一次日志缓冲（非阻塞 dump）", p));
    }
    {
        QJsonObject p;
        p.insert("local", prop("string", "本地 PNG 保存路径", true));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("screencap", "截图并保存为 PNG（exec-out screencap -p）", p, {"local"}));
    }
    {
        QJsonObject p;
        p.insert("local", prop("string", "本地端口，如 tcp:8080", true));
        p.insert("remote", prop("string", "设备端口，如 tcp:8080", true));
        p.insert("remove", prop("boolean", "移除该转发"));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("forward", "端口转发：PC → 设备（adb forward）", p, {"local", "remote"}));
    }
    {
        QJsonObject p;
        p.insert("local", prop("string", "设备端口，如 tcp:8080", true));
        p.insert("remote", prop("string", "PC 端口，如 tcp:8080", true));
        p.insert("remove", prop("boolean", "移除该反向转发"));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("reverse", "反向端口转发：设备 → PC（adb reverse）", p, {"local", "remote"}));
    }
    {
        QJsonObject p;
        p.insert("mode", prop("string", "system | bootloader | recovery | fastboot | sideload，默认 system"));
        p.insert("serial", prop("string", "可选：设备序列号"));
        arr.append(toolDef("reboot", "重启设备到指定模式", p));
    }
    {
        QJsonObject p;
        p.insert("action", prop("string", "connect | disconnect | pair | tcpip", true));
        p.insert("host", prop("string", "设备 IP（connect/disconnect/pair 需要）"));
        p.insert("port", prop("number", "端口，默认 5555（tcpip 也可指定）"));
        p.insert("code", prop("string", "配对码（pair 需要）"));
        arr.append(toolDef("wireless", "无线调试：配对 / 连接 / 断开 / 切 TCP 模式", p, {"action"}));
    }
    {
        QJsonObject p;
        p.insert("command", prop("string", "devices | getvar", true));
        p.insert("args", prop("string", "附加参数，如 getvar 的变量名"));
        arr.append(toolDef("fastboot", "fastboot 只读操作（devices / getvar）", p, {"command"}));
    }
    return arr;
}

QStringList AdbProvider::serialPrefix(const QJsonObject& args) const {
    const QString serial = args.value("serial").toString().trimmed();
    return serial.isEmpty() ? QStringList{} : QStringList{QStringLiteral("-s"), serial};
}

AdbProvider::RunResult AdbProvider::run(const QString& program, const QStringList& args, int timeoutMs) {
    RunResult r;
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start();
    if (!proc.waitForStarted(5000)) {
        r.err = proc.errorString();
        return r;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        r.timedOut = true;
        proc.kill();
        proc.waitForFinished(2000);
    }
    r.raw = proc.readAllStandardOutput();
    r.out = QString::fromLocal8Bit(r.raw);
    r.err = QString::fromLocal8Bit(proc.readAllStandardError());
    r.code = proc.exitCode();
    return r;
}

QJsonObject AdbProvider::textResult(const QString& text, bool isError) const {
    QJsonObject item;
    item.insert("type", "text");
    item.insert("text", text);
    QJsonObject res;
    res.insert("content", QJsonArray{item});
    res.insert("isError", isError);
    return res;
}

QJsonObject AdbProvider::call(const QString& name, const QJsonObject& args, bool* isError) {
    if (isError) *isError = false;
    auto markError = [&](const QJsonObject& r) {
        if (isError) *isError = true;
        return r;
    };
    if (!available()) return markError(failText(QStringLiteral("adb 不可用：未登记 platform-tools 路径")));

    const QStringList pre = serialPrefix(args);

    auto runAdb = [&](const QStringList& tail, int timeoutMs = 60000) {
        return run(m_adb, pre + tail, timeoutMs);
    };

    auto format = [](const RunResult& r) {
        QString s = r.out;
        if (!r.err.trimmed().isEmpty()) s += (s.isEmpty() ? QString() : QStringLiteral("\n")) + QStringLiteral("[stderr]\n") + r.err;
        if (r.timedOut) s += QStringLiteral("\n[timeout]");
        return s.trimmed();
    };

    if (name == QStringLiteral("devices")) {
        const auto r = runAdb({QStringLiteral("devices"), QStringLiteral("-l")}, 20000);
        return textResult(format(r), r.code != 0);
    }

    if (name == QStringLiteral("device_info")) {
        const auto r = runAdb({QStringLiteral("shell"), QStringLiteral("getprop")}, 20000);
        if (r.code != 0) return markError(failText(format(r)));
        static const QStringList keys = {
            QStringLiteral("ro.product.brand"), QStringLiteral("ro.product.model"),
            QStringLiteral("ro.product.manufacturer"), QStringLiteral("ro.build.version.release"),
            QStringLiteral("ro.build.version.sdk"), QStringLiteral("ro.product.cpu.abi"),
            QStringLiteral("ro.serialno"), QStringLiteral("ro.build.version.incremental")};
        QStringList out;
        for (const auto& line : r.out.split(QLatin1Char('\n'))) {
            const int lb = line.indexOf(QLatin1Char('['));
            const int rb = line.indexOf(QStringLiteral("]: ["));
            if (lb < 0 || rb < 0) continue;
            const QString key = line.mid(lb + 1, rb - lb - 1);
            if (!keys.contains(key)) continue;
            QString val = line.mid(rb + 4);
            if (val.endsWith(QLatin1Char(']'))) val.chop(1);
            out << QStringLiteral("%1: %2").arg(key, val);
        }
        if (out.isEmpty()) return markError(failText(QStringLiteral("没有取到设备属性（未连接设备？）")));
        return textResult(out.join(QLatin1Char('\n')), false);
    }

    if (name == QStringLiteral("shell")) {
        const QString cmd = args.value("command").toString();
        if (cmd.isEmpty()) return markError(failText(QStringLiteral("command 不能为空")));
        const int to = qBound(5, args.value("timeoutSec").toInt(60), 600);
        const auto r = runAdb({QStringLiteral("shell"), cmd}, to * 1000);
        return textResult(format(r), r.code != 0);
    }

    if (name == QStringLiteral("push") || name == QStringLiteral("pull")) {
        const QString a = args.value(name == QStringLiteral("push") ? "local" : "remote").toString();
        const QString b = args.value(name == QStringLiteral("push") ? "remote" : "local").toString();
        if (a.isEmpty() || b.isEmpty()) return markError(failText(QStringLiteral("路径参数为空")));
        const auto r = runAdb({name, QDir::toNativeSeparators(a), QDir::toNativeSeparators(b)}, 300000);
        return textResult(format(r), r.code != 0);
    }

    if (name == QStringLiteral("install")) {
        const QString apk = args.value("apk").toString();
        if (apk.isEmpty()) return markError(failText(QStringLiteral("apk 不能为空")));
        QStringList tail{QStringLiteral("install")};
        if (args.value("reinstall").toBool(true)) tail << QStringLiteral("-r");
        if (args.value("downgrade").toBool(false)) tail << QStringLiteral("-d");
        tail << QDir::toNativeSeparators(apk);
        const auto r = runAdb(tail, 300000);
        return textResult(format(r), r.code != 0 || !r.out.contains(QStringLiteral("Success")));
    }

    if (name == QStringLiteral("uninstall")) {
        const QString pkg = args.value("package").toString();
        if (pkg.isEmpty()) return markError(failText(QStringLiteral("package 不能为空")));
        QStringList tail{QStringLiteral("uninstall")};
        if (args.value("keepData").toBool(false)) tail << QStringLiteral("-k");
        tail << pkg;
        const auto r = runAdb(tail, 120000);
        return textResult(format(r), r.code != 0 || !r.out.contains(QStringLiteral("Success")));
    }

    if (name == QStringLiteral("packages")) {
        QStringList tail{QStringLiteral("shell"), QStringLiteral("pm"), QStringLiteral("list"), QStringLiteral("packages")};
        if (args.value("thirdParty").toBool(false)) tail << QStringLiteral("-3");
        const auto r = runAdb(tail, 60000);
        QStringList lines = r.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString filter = args.value("filter").toString();
        if (!filter.isEmpty()) {
            QStringList kept;
            for (const auto& l : lines)
                if (l.contains(filter, Qt::CaseInsensitive)) kept << l;
            lines = kept;
        }
        return textResult(lines.join(QLatin1Char('\n')) + (r.err.isEmpty() ? QString() : QStringLiteral("\n") + r.err),
                          r.code != 0);
    }

    if (name == QStringLiteral("logcat")) {
        const int n = qBound(1, args.value("lines").toInt(200), 5000);
        QStringList tail{QStringLiteral("logcat"), QStringLiteral("-d"), QStringLiteral("-t"), QString::number(n)};
        const QString filter = args.value("filter").toString().trimmed();
        if (!filter.isEmpty()) tail << filter;
        const auto r = runAdb(tail, 60000);
        return textResult(format(r), r.code != 0);
    }

    if (name == QStringLiteral("screencap")) {
        const QString local = args.value("local").toString();
        if (local.isEmpty()) return markError(failText(QStringLiteral("local 不能为空")));
        const auto r = runAdb({QStringLiteral("exec-out"), QStringLiteral("screencap"), QStringLiteral("-p")}, 60000);
        if (r.code != 0 || r.raw.isEmpty()) return markError(failText(format(r)));
        const QString path = QDir::toNativeSeparators(local);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return markError(failText(QStringLiteral("无法写入 %1").arg(path)));
        }
        f.write(r.raw);
        f.close();
        return textResult(QStringLiteral("saved %1 (%2 bytes)").arg(path).arg(r.raw.size()), false);
    }

    if (name == QStringLiteral("forward") || name == QStringLiteral("reverse")) {
        const QString local = args.value("local").toString();
        const QString remote = args.value("remote").toString();
        if (local.isEmpty() || remote.isEmpty()) return markError(failText(QStringLiteral("local/remote 不能为空")));
        QStringList tail{name};
        if (args.value("remove").toBool(false)) tail << QStringLiteral("--remove");
        tail << local << remote;
        const auto r = runAdb(tail, 30000);
        return textResult(format(r), r.code != 0);
    }

    if (name == QStringLiteral("reboot")) {
        const QString mode = args.value("mode").toString().trimmed();
        const QStringList allowed = {QString(), QStringLiteral("system"), QStringLiteral("bootloader"),
                                     QStringLiteral("recovery"), QStringLiteral("fastboot"), QStringLiteral("sideload")};
        if (!allowed.contains(mode)) return markError(failText(QStringLiteral("不支持的 mode: %1").arg(mode)));
        QStringList tail{QStringLiteral("reboot")};
        if (!mode.isEmpty() && mode != QStringLiteral("system")) tail << mode;
        const auto r = runAdb(tail, 30000);
        return textResult(format(r).isEmpty() ? QStringLiteral("reboot: %1").arg(mode.isEmpty() ? "system" : mode)
                                              : format(r),
                          r.code != 0);
    }

    if (name == QStringLiteral("wireless")) {
        const QString action = args.value("action").toString();
        const QString host = args.value("host").toString().trimmed();
        const int port = args.value("port").toInt(5555);
        const QString code = args.value("code").toString().trimmed();
        if (action == QStringLiteral("tcpip")) {
            const auto r = runAdb({QStringLiteral("tcpip"), QString::number(port)}, 30000);
            return textResult(format(r), r.code != 0);
        }
        if (host.isEmpty()) return markError(failText(QStringLiteral("%1 需要 host").arg(action)));
        const QString target = QStringLiteral("%1:%2").arg(host).arg(port);
        if (action == QStringLiteral("connect") || action == QStringLiteral("disconnect")) {
            const auto r = runAdb({action, target}, 30000);
            return textResult(format(r), r.code != 0);
        }
        if (action == QStringLiteral("pair")) {
            if (code.isEmpty()) return markError(failText(QStringLiteral("pair 需要 code")));
            const auto r = runAdb({QStringLiteral("pair"), target, code}, 30000);
            return textResult(format(r), r.code != 0);
        }
        return markError(failText(QStringLiteral("未知 action: %1").arg(action)));
    }

    if (name == QStringLiteral("fastboot")) {
        const QString fb = fastbootExe();
        if (!QFileInfo::exists(fb)) return markError(failText(QStringLiteral("fastboot.exe 不存在")));
        const QString cmd = args.value("command").toString();
        if (cmd != QStringLiteral("devices") && cmd != QStringLiteral("getvar")) {
            return markError(failText(QStringLiteral("仅支持 devices / getvar（只读）")));
        }
        QStringList tail{cmd};
        const QString extra = args.value("args").toString().trimmed();
        if (!extra.isEmpty()) tail << extra;
        const auto r = run(fb, tail, 30000);
        return textResult(format(r), r.code != 0);
    }

    return markError(failText(QStringLiteral("未知 adb 工具: %1").arg(name)));
}

}
