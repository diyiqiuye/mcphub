#include <QApplication>
#include <QGuiApplication>
#include <QTimer>

#include "gui/mainwindow.h"
#include "gui/theme.h"

int main(int argc, char** argv) {

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mcphub"));
    app.setApplicationDisplayName(QStringLiteral("MCP Hub"));
    app.setOrganizationName(QStringLiteral("mcphub"));

    app.setFont(theme::mono(12));
    app.setStyleSheet(theme::styleSheet());

    MainWindow w;
    w.resize(1300, 780);
    w.show();

    const QStringList args = app.arguments();
    const int shotIdx = args.indexOf(QStringLiteral("--shot"));
    if (shotIdx > 0 && shotIdx + 1 < args.size()) {
        const QString path = args.at(shotIdx + 1);
        QTimer::singleShot(4200, &app, [&w, path, &app] {
            w.grab().save(path);
            app.quit();
        });
    }
    return app.exec();
}
