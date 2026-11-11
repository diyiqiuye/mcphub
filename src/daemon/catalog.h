#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "shared/protocol.h"

namespace mcphub {

struct CatalogSource {
    QString name;
    QString url;
    QString sha256;
};

struct CatalogEntry {
    QString id;
    QString name;
    QString category;
    QString version;
    QString license;
    int sizeMB = 0;
    QStringList requires;
    QStringList probe;

    QString installType;
    QString dest;
    QString installNote;
    QVector<CatalogSource> sources;

    QStringList verifyCmd;
    QMap<QString, QString> verifyEnv;
    QString verifyMatch;
    int verifyTimeoutMs = 20000;

    QString mcpMode;
    QString provider;
    QString mcpNamespace;
    QStringList mcpTools;
    QStringList mcpCommand;
    QMap<QString, QString> mcpEnv;
    int mcpRpcPort = 13337;
    int mcpTimeoutMs = 120000;

    QJsonObject raw;
    QJsonObject toJson() const;
};

class Catalog {
public:
    bool load(const QString& catalogDir, QString* error);
    const QVector<CatalogEntry>& entries() const { return m_entries; }
    const CatalogEntry* find(const QString& id) const;
    QString dir() const { return m_dir; }

private:
    QVector<CatalogEntry> m_entries;
    QString m_dir;
};

ToolStatus probeEntry(const CatalogEntry& e, const QString& root);
bool pathUnder(const QString& path, const QString& root);

}
