#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QPoint>

class QLabel;
class QSizeGrip;
class QTimer;
class QToolButton;
class DaemonClient;
class LogConsole;
class StatusPanel;
class TitleBar;
class TopologyView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void buildUi();
    void poll();
    void applySnapshot(const QJsonObject& snapshot);
    void copyConfig();
    void runDoctor();
    void openRoot();
    void spawnDaemon();
    void showToolMenu(const QString& id, const QPoint& globalPos);
    void restoreUi();
    void saveUi();

    TitleBar* m_title = nullptr;
    StatusPanel* m_status = nullptr;
    TopologyView* m_topo = nullptr;
    LogConsole* m_log = nullptr;
    DaemonClient* m_client = nullptr;
    QTimer* m_poll = nullptr;
    QSizeGrip* m_grip = nullptr;
    QLabel* m_logHint = nullptr;
    QToolButton* m_pauseBtn = nullptr;
    QJsonObject m_lastSnapshot;
    bool m_spawnTried = false;
};
