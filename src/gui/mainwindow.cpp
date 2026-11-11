#include "gui/mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QProcess>
#include <QResizeEvent>
#include <QSettings>
#include <QSizeGrip>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "gui/daemonclient.h"
#include "gui/logconsole.h"
#include "gui/statuspanel.h"
#include "gui/theme.h"
#include "gui/titlebar.h"
#include "gui/topologyview.h"
#include "shared/protocol.h"

using namespace mcphub;

namespace {
QString uiSettingsPath() {
    const QString dir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../state"));
    QDir().mkpath(dir);
    return QDir(dir).absoluteFilePath(QStringLiteral("ui.ini"));
}
QString oneLine(const QString& s, int max = 200) {
    QString t = s;
    t.replace(QLatin1Char('\r'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    t = t.simplified();
    return t.size() > max ? t.left(max) + QStringLiteral("…") : t;
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("MCP Hub"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(960, 600);
    buildUi();

    connect(m_status, &StatusPanel::toolMenuRequested, this, &MainWindow::showToolMenu);
    connect(m_topo, &TopologyView::toolMenuRequested, this, &MainWindow::showToolMenu);

    m_client = new DaemonClient(this);
    connect(m_client, &DaemonClient::onlineChanged, this, [this](bool online) {
        m_title->setOnline(online);
        m_status->setOnline(online);
        m_topo->setOnline(online);
        m_log->appendLine(online ? QStringLiteral("ok") : QStringLiteral("warn"), QStringLiteral("ipc"),
                          online ? QStringLiteral("connected to mcphubd") : QStringLiteral("daemon offline, retrying…"));
        if (online) {
            m_client->request(method::LogTail, QJsonObject{{"lines", 300}},
                              [this](const QJsonObject& r, const QJsonObject&) {
                                  const auto lines = r.value("lines").toArray();
                                  for (const auto& v : lines) m_log->appendEntry(v.toObject());
                              });
            poll();
        }
    });
    connect(m_client, &DaemonClient::eventArrived, this, [this](const QString& name, const QJsonObject& p) {
        if (name == QString::fromLatin1(event::Log)) {
            m_log->appendEntry(p);
        } else if (name == QString::fromLatin1(event::Status)) {
            applySnapshot(p);
        } else if (name == QString::fromLatin1(event::Hello)) {
            m_log->appendLine(QStringLiteral("debug"), QStringLiteral("ipc"),
                              QStringLiteral("handshake v%1").arg(p.value("version").toString()));
        }
    });

    m_poll = new QTimer(this);
    m_poll->setInterval(1500);
    connect(m_poll, &QTimer::timeout, this, &MainWindow::poll);
    m_poll->start();

    m_log->appendLine(QStringLiteral("debug"), QStringLiteral("ui"),
                      QStringLiteral("mcphub ui %1 · %2").arg(QString::fromLatin1(Version),
                                                                  QDir::toNativeSeparators(QCoreApplication::applicationDirPath())));
    m_log->appendLine(QStringLiteral("debug"), QStringLiteral("ui"),
                      QStringLiteral("提示：右键左侧工具行或拓扑节点 → 登记已有安装 / 校验 / 打开目录"));

    restoreUi();
    m_client->start();
    QTimer::singleShot(1400, this, [this] {
        if (!m_client->online()) spawnDaemon();
    });
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_title = new TitleBar(central);
    connect(m_title, &TitleBar::minimizeRequested, this, &MainWindow::showMinimized);
    connect(m_title, &TitleBar::maximizeRequested, this, [this] {
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    connect(m_title, &TitleBar::closeRequested, this, &MainWindow::close);
    root->addWidget(m_title);

    auto* hsplit = new QSplitter(Qt::Horizontal, central);
    hsplit->setChildrenCollapsible(false);
    hsplit->setHandleWidth(1);

    m_status = new StatusPanel(hsplit);
    hsplit->addWidget(m_status);

    auto* vsplit = new QSplitter(Qt::Vertical, hsplit);
    vsplit->setChildrenCollapsible(false);
    vsplit->setHandleWidth(1);

    m_topo = new TopologyView(vsplit);
    vsplit->addWidget(m_topo);

    auto* logHost = new QWidget(vsplit);
    auto* logLay = new QVBoxLayout(logHost);
    logLay->setContentsMargins(0, 0, 0, 0);
    logLay->setSpacing(0);

    auto* logHead = new QWidget(logHost);
    logHead->setFixedHeight(24);
    auto* headLay = new QHBoxLayout(logHead);
    headLay->setContentsMargins(8, 0, 6, 0);
    headLay->setSpacing(8);

    auto* live = new QLabel(QStringLiteral("\u25cf LIVE LOG"), logHead);
    live->setFont(theme::mono(9, QFont::DemiBold));
    live->setStyleSheet(QStringLiteral("color:#2DD4BF; letter-spacing:1px;"));

    m_logHint = new QLabel(QStringLiteral("tail \u00b7 stdout \u00b7 events"), logHead);
    m_logHint->setFont(theme::mono(9));
    m_logHint->setStyleSheet(QStringLiteral("color:#3F3F46;"));

    auto makeAction = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton(logHead);
        b->setText(text);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFont(theme::mono(10));
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(QStringLiteral(
            "QToolButton{border:none;background:transparent;color:#71717A;padding:0 6px;}"
            "QToolButton:hover{color:#2DD4BF;}"));
        return b;
    };
    auto* btnDoctor = makeAction(QStringLiteral("doctor"), QStringLiteral("自检：拉起一次 gateway，确认工具数与 DLL/路径问题"));
    auto* btnCfg = makeAction(QStringLiteral("copy mcp"), QStringLiteral("复制 MCP 接入配置 JSON（通用 mcpServers 结构）"));
    auto* btnRoot = makeAction(QStringLiteral("open root"), QStringLiteral("在资源管理器中打开工具根目录"));
    m_pauseBtn = makeAction(QStringLiteral("pause"), QStringLiteral("暂停日志滚动（继续时补上积压行）"));
    auto* btnClear = makeAction(QStringLiteral("clear"), QStringLiteral("清空日志视图"));

    connect(btnDoctor, &QToolButton::clicked, this, &MainWindow::runDoctor);
    connect(btnCfg, &QToolButton::clicked, this, &MainWindow::copyConfig);
    connect(btnRoot, &QToolButton::clicked, this, &MainWindow::openRoot);
    connect(m_pauseBtn, &QToolButton::clicked, this, [this] {
        const bool paused = !m_log->paused();
        m_log->setPaused(paused);
        m_pauseBtn->setText(paused ? QStringLiteral("resume") : QStringLiteral("pause"));
        m_pauseBtn->setStyleSheet(paused
                                      ? QStringLiteral("QToolButton{border:none;background:transparent;color:#F59E0B;padding:0 6px;}")
                                      : QStringLiteral("QToolButton{border:none;background:transparent;color:#71717A;padding:0 6px;}"));
        m_logHint->setText(paused ? QStringLiteral("paused \u00b7 buffering") : QStringLiteral("tail \u00b7 stdout \u00b7 events"));
    });
    connect(btnClear, &QToolButton::clicked, this, [this] { m_log->clear(); });

    headLay->addWidget(live);
    headLay->addWidget(m_logHint);
    headLay->addStretch(1);
    headLay->addWidget(btnDoctor);
    headLay->addWidget(btnCfg);
    headLay->addWidget(btnRoot);
    headLay->addWidget(m_pauseBtn);
    headLay->addWidget(btnClear);

    m_log = new LogConsole(logHost);

    logLay->addWidget(logHead);
    logLay->addWidget(m_log, 1);
    vsplit->addWidget(logHost);
    vsplit->setSizes({420, 300});

    hsplit->addWidget(vsplit);
    hsplit->setStretchFactor(0, 0);
    hsplit->setStretchFactor(1, 1);
    hsplit->setSizes({330, 950});

    root->addWidget(hsplit, 1);
    setCentralWidget(central);

    m_grip = new QSizeGrip(this);
    m_grip->setFixedSize(14, 14);
}

void MainWindow::poll() {
    if (!m_client->online()) return;
    m_client->request(method::StatusGet, {}, [this](const QJsonObject& r, const QJsonObject& e) {
        if (!e.isEmpty()) {
            m_log->appendLine(QStringLiteral("error"), QStringLiteral("ipc"), e.value("message").toString());
            return;
        }
        applySnapshot(r);
    });
}

void MainWindow::applySnapshot(const QJsonObject& snapshot) {
    m_lastSnapshot = snapshot;
    m_status->setSnapshot(snapshot);
    m_topo->setSnapshot(snapshot);
    const auto hub = snapshot.value("hub").toObject();
    m_title->setSummary(QStringLiteral("%1/%2 tools \u00b7 %3 client(s)")
                            .arg(hub.value("ready").toInt())
                            .arg(hub.value("total").toInt())
                            .arg(hub.value("clients").toInt()));
}

void MainWindow::copyConfig() {
    m_client->request(method::ConfigSnippet, QJsonObject{{"client", "standard"}},
                      [this](const QJsonObject& r, const QJsonObject& e) {
                          if (!e.isEmpty()) {
                              m_log->appendLine(QStringLiteral("error"), QStringLiteral("config"),
                                                e.value("message").toString());
                              return;
                          }
                          const QByteArray json =
                              QJsonDocument(r.value("config").toObject()).toJson(QJsonDocument::Indented);
                          QApplication::clipboard()->setText(QString::fromUtf8(json));
                          m_log->appendLine(QStringLiteral("ok"), QStringLiteral("config"),
                                            QStringLiteral("接入配置 JSON 已复制"));
                      });
}

void MainWindow::runDoctor() {
    const QString exe = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("mcphubd.exe"));
    if (!QFileInfo::exists(exe)) {
        m_log->appendLine(QStringLiteral("error"), QStringLiteral("doctor"),
                          QStringLiteral("找不到 %1").arg(QDir::toNativeSeparators(exe)));
        return;
    }
    m_log->appendLine(QStringLiteral("debug"), QStringLiteral("doctor"), QStringLiteral("自检中…"));
    auto* proc = new QProcess(this);
    proc->setProgram(exe);
    proc->setArguments({QStringLiteral("--doctor")});
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int, QProcess::ExitStatus) {
                const QString out = QString::fromLocal8Bit(proc->readAllStandardOutput());
                const QString err = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
                const auto doc = QJsonDocument::fromJson(out.toUtf8());
                if (doc.isObject()) {
                    const auto o = doc.object();
                    if (o.value("ok").toBool()) {
                        m_log->appendLine(QStringLiteral("ok"), QStringLiteral("doctor"),
                                          QStringLiteral("自检通过：gateway %1 个工具").arg(o.value("tools").toInt()));
                    } else {
                        QString hint = o.value("hint").toString();
                        if (hint.isEmpty()) hint = o.value("error").toString();
                        m_log->appendLine(QStringLiteral("error"), QStringLiteral("doctor"),
                                          QStringLiteral("自检失败：%1").arg(hint));
                        const QString stderrText = o.value("stderr").toString();
                        if (!stderrText.isEmpty())
                            m_log->appendLine(QStringLiteral("debug"), QStringLiteral("doctor"), stderrText.left(200));
                    }
                } else {
                    m_log->appendLine(QStringLiteral("error"), QStringLiteral("doctor"),
                                      QStringLiteral("自检无输出%1").arg(err.isEmpty() ? QString() : QStringLiteral("：") + err.left(160)));
                }
                proc->deleteLater();
            });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        m_log->appendLine(QStringLiteral("error"), QStringLiteral("doctor"),
                          QStringLiteral("自检失败：%1").arg(proc->errorString()));
        proc->deleteLater();
    });
    proc->start();
    QTimer::singleShot(15000, proc, [proc] {
        if (proc->state() != QProcess::NotRunning) proc->kill();
    });
}

void MainWindow::openRoot() {
    m_client->request(method::FsOpen, QJsonObject{{"target", "root"}},
                      [this](const QJsonObject& r, const QJsonObject& e) {
                          if (!e.isEmpty()) {
                              m_log->appendLine(QStringLiteral("error"), QStringLiteral("fs"), e.value("message").toString());
                              return;
                          }
                          m_log->appendLine(QStringLiteral("debug"), QStringLiteral("fs"), r.value("path").toString());
                      });
}

void MainWindow::showToolMenu(const QString& id, const QPoint& globalPos) {
    QJsonObject tool;
    for (const auto& v : m_lastSnapshot.value("tools").toArray()) {
        const auto o = v.toObject();
        if (o.value("id").toString() == id) {
            tool = o;
            break;
        }
    }
    if (tool.isEmpty()) return;

    const QString name = tool.value("name").toString(id);
    const QString path = tool.value("path").toString();
    const bool hasPath = !path.isEmpty();
    const bool verified = tool.value("verified").toBool();
    const bool verifyOk = tool.value("verifyOk").toBool();
    const bool manual = tool.value("manual").toBool();
    const QString note = tool.value("note").toString();
    const bool online = m_client->online();

    QMenu menu(this);
    auto* actSet = menu.addAction(
        manual && !hasPath ? QStringLiteral("登记安装目录…（该工具需自行下载）")
                           : (hasPath ? QStringLiteral("更换登记路径…") : QStringLiteral("登记已有安装…")));
    actSet->setEnabled(online);
    auto* actVerify = menu.addAction(!verified ? QStringLiteral("校验安装")
                                             : (verifyOk ? QStringLiteral("重新校验（上次通过）")
                                                         : QStringLiteral("重新校验（上次失败）")));
    actVerify->setEnabled(online && hasPath);
    auto* actOpen = menu.addAction(QStringLiteral("在资源管理器中打开"));
    actOpen->setEnabled(hasPath);
    auto* actCopy = menu.addAction(QStringLiteral("复制路径"));
    actCopy->setEnabled(hasPath);
    menu.addSeparator();
    auto* actForget = menu.addAction(QStringLiteral("清除登记"));
    actForget->setEnabled(online && hasPath);

    QAction* actIdaAuto = nullptr;
    bool idaAutoOn = false;
    if (id == QStringLiteral("ida")) {
        idaAutoOn = m_lastSnapshot.value("hub").toObject().value("idaAutostart").toBool();
        menu.addSeparator();
        actIdaAuto = menu.addAction(idaAutoOn
                                        ? QStringLiteral("停用 IDA 自动接入")
                                        : QStringLiteral("启用 IDA 自动接入（重启 IDA 生效）"));
        actIdaAuto->setEnabled(online);
    }
    QAction* actInstall = nullptr;
    if (manual) {
        auto* info = menu.addAction(note.isEmpty() ? QStringLiteral("需自行下载安装后登记目录") : note);
        info->setEnabled(false);
    } else {
        const QString state = tool.value("state").toString();
        const bool installing = (state == QStringLiteral("downloading") || state == QStringLiteral("installing") ||
                                 state == QStringLiteral("verifying") || state == QStringLiteral("planning"));
        actInstall = menu.addAction(installing ? QStringLiteral("安装中…")
                                               : QStringLiteral("下载安装（总包 %1 MB）").arg(126));
        actInstall->setEnabled(online && !hasPath && !installing);
    }

    auto* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == actSet) {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择 %1 的安装目录").arg(name),
            hasPath ? path : QDir::homePath());
        if (dir.isEmpty()) return;
        m_client->request(method::ToolSetPath, QJsonObject{{"id", id}, {"path", dir}},
                          [this, name](const QJsonObject& r, const QJsonObject& e) {
                              if (!e.isEmpty()) {
                                  m_log->appendLine(QStringLiteral("error"), QStringLiteral("tool"),
                                                    QStringLiteral("%1 登记失败：%2").arg(name, e.value("message").toString()));
                                  return;
                              }
                              m_log->appendLine(QStringLiteral("ok"), QStringLiteral("tool"),
                                                QStringLiteral("%1 已登记 → %2").arg(name, r.value("path").toString()));
                              poll();
                          }, 10000);
        return;
    }
    if (chosen == actVerify) {
        m_log->appendLine(QStringLiteral("debug"), QStringLiteral("verify"),
                          QStringLiteral("%1 校验中…").arg(name));
        m_client->request(method::ToolVerify, QJsonObject{{"id", id}},
                          [this, name](const QJsonObject& r, const QJsonObject& e) {
                              if (!e.isEmpty()) {
                                  m_log->appendLine(QStringLiteral("error"), QStringLiteral("verify"),
                                                    QStringLiteral("%1 校验失败：%2").arg(name, e.value("message").toString()));
                                  return;
                              }
                              const bool ok = r.value("ok").toBool();
                              m_log->appendLine(ok ? QStringLiteral("ok") : QStringLiteral("warn"), QStringLiteral("verify"),
                                                QStringLiteral("%1 %2 exit=%3 %4")
                                                    .arg(name, ok ? QStringLiteral("校验通过") : QStringLiteral("校验失败"))
                                                    .arg(r.value("code").toInt())
                                                    .arg(oneLine(r.value("output").toString(), 160)));
                              poll();
                          }, 65000);
        return;
    }
    if (chosen == actOpen) {
        m_client->request(method::FsOpen, QJsonObject{{"toolId", id}},
                          [this](const QJsonObject&, const QJsonObject& e) {
                              if (!e.isEmpty())
                                  m_log->appendLine(QStringLiteral("error"), QStringLiteral("fs"), e.value("message").toString());
                          });
        return;
    }
    if (chosen == actCopy) {
        QApplication::clipboard()->setText(path);
        m_log->appendLine(QStringLiteral("debug"), QStringLiteral("tool"),
                          QStringLiteral("已复制 %1 路径").arg(name));
        return;
    }
    if (actIdaAuto && chosen == actIdaAuto) {
        const QString action = idaAutoOn ? QStringLiteral("disable") : QStringLiteral("enable");
        m_client->request(method::IdaAutostart, QJsonObject{{"action", action}},
                          [this, action](const QJsonObject& r, const QJsonObject& e) {
                              if (!e.isEmpty()) {
                                  m_log->appendLine(QStringLiteral("error"), QStringLiteral("ida"),
                                                    e.value("message").toString());
                                  return;
                              }
                              m_log->appendLine(QStringLiteral("ok"), QStringLiteral("ida"),
                                                action == QStringLiteral("enable")
                                                    ? QStringLiteral("已安装自动接入插件，重启 IDA 后无需再按 Ctrl-Alt-M")
                                                    : QStringLiteral("已停用 IDA 自动接入"));
                              Q_UNUSED(r)
                              poll();
                          });
        return;
    }
    if (actInstall && chosen == actInstall) {
        m_log->appendLine(QStringLiteral("debug"), QStringLiteral("provision"),
                          QStringLiteral("%1 开始下载安装…").arg(name));
        m_client->request(method::ProvisionApply, QJsonObject{{"id", id}},
                          [this, name](const QJsonObject&, const QJsonObject& e) {
                              if (!e.isEmpty()) {
                                  m_log->appendLine(QStringLiteral("error"), QStringLiteral("provision"),
                                                    QStringLiteral("%1 无法安装：%2").arg(name, e.value("message").toString()));
                              }
                          },
                          10000);
        return;
    }
    if (chosen == actForget) {
        m_client->request(method::ToolForget, QJsonObject{{"id", id}},
                          [this, name](const QJsonObject&, const QJsonObject& e) {
                              if (!e.isEmpty()) {
                                  m_log->appendLine(QStringLiteral("error"), QStringLiteral("tool"), e.value("message").toString());
                                  return;
                              }
                              m_log->appendLine(QStringLiteral("ok"), QStringLiteral("tool"),
                                                QStringLiteral("%1 的手动登记已清除").arg(name));
                              poll();
                          });
    }
}

void MainWindow::spawnDaemon() {
    if (m_spawnTried) return;
    m_spawnTried = true;
    const QString daemon = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("mcphubd.exe"));
    if (!QFileInfo::exists(daemon)) {
        m_log->appendLine(QStringLiteral("error"), QStringLiteral("core"),
                          QStringLiteral("找不到 mcphubd.exe: %1").arg(QDir::toNativeSeparators(daemon)));
        return;
    }
    m_log->appendLine(QStringLiteral("debug"), QStringLiteral("core"),
                      QStringLiteral("daemon offline → 尝试拉起 %1").arg(QFileInfo(daemon).fileName()));
    QProcess::startDetached(daemon, {}, QCoreApplication::applicationDirPath());
}

void MainWindow::restoreUi() {
    QSettings s(uiSettingsPath(), QSettings::IniFormat);
    const QByteArray geo = s.value(QStringLiteral("geometry")).toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
}

void MainWindow::saveUi() {
    QSettings s(uiSettingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("geometry"), saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    saveUi();
    if (m_client) m_client->stop();
    QMainWindow::closeEvent(e);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST && !isMaximized()) {
        const QPoint gp = QCursor::pos();
        const QRect r = frameGeometry();
        const int b = 6;
        const bool left = gp.x() >= r.left() && gp.x() < r.left() + b;
        const bool right = gp.x() <= r.right() && gp.x() > r.right() - b;
        const bool top = gp.y() >= r.top() && gp.y() < r.top() + b;
        const bool bottom = gp.y() <= r.bottom() && gp.y() > r.bottom() - b;

        if (top && left) *result = HTTOPLEFT;
        else if (top && right) *result = HTTOPRIGHT;
        else if (bottom && left) *result = HTBOTTOMLEFT;
        else if (bottom && right) *result = HTBOTTOMRIGHT;
        else if (left) *result = HTLEFT;
        else if (right) *result = HTRIGHT;
        else if (top) *result = HTTOP;
        else if (bottom) *result = HTBOTTOM;
        else return QMainWindow::nativeEvent(eventType, message, result);
        return true;
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (m_grip) m_grip->move(width() - m_grip->width() - 1, height() - m_grip->height() - 1);
}
