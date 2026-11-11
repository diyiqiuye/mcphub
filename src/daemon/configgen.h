#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace mcphub {

struct ClientTarget {
    QString key;
    QString label;
    QString file;
};

class ConfigGen {
public:
    ConfigGen(QString root, QString daemonPath);

    QVector<ClientTarget> clients() const;
    const ClientTarget* find(const QString& key) const;

    QJsonObject serverEntry() const;
    QJsonObject snippet(const QString& client) const;
    bool write(const QString& client, QString* backup, QString* error) const;

private:
    QString m_root;
    QString m_daemon;
};

}
