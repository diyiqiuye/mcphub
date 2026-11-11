#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mcphub {

class AdbProvider {
public:
    AdbProvider(QString adbExe, QString root);

    bool available() const;
    QString adbPath() const { return m_adb; }

    static QJsonArray toolDefs();

    QJsonObject call(const QString& name, const QJsonObject& args, bool* isError);

private:
    struct RunResult {
        int code = -1;
        QString out;
        QString err;
        bool timedOut = false;
        QByteArray raw;
    };

    RunResult run(const QString& program, const QStringList& args, int timeoutMs = 60000);
    QStringList serialPrefix(const QJsonObject& args) const;
    QString fastbootExe() const;
    QJsonObject textResult(const QString& text, bool isError = false) const;
    QJsonObject failText(const QString& text) const { return textResult(text, true); }

    QString m_adb;
    QString m_root;
};

}
