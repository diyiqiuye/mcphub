#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace mcphub {

class Catalog;
struct CatalogEntry;

class Provisioner : public QObject {
    Q_OBJECT
public:
    using InstalledFn = std::function<bool(const QString& id)>;
    Provisioner(QString root, Catalog* catalog, InstalledFn installed, QObject* parent = nullptr);

    bool busy(const QString& id) const { return m_jobs.contains(id); }
    bool hasBundle() const { return !m_bundle.value("url").toString().isEmpty(); }
    QJsonObject bundleInfo() const { return m_bundle; }

    void apply(const QString& id);

signals:
    void progress(const QString& id, const QString& phase, qint64 received, qint64 total);
    void log(const QString& level, const QString& text);
    void finished(const QString& id, bool ok, const QString& message);

private:
    struct Job {
        QString id;
        QString phase;
        QNetworkReply* reply = nullptr;
        QFile* file = nullptr;
        QString targetFile;
        QString sha;
        qint64 total = 0;
        bool bundleJob = false;
    };

    void startDownload(const QString& id, const QString& url, const QString& sha, const QString& dest,
                       bool isBundle);
    void onReplyFinished(const QString& id);
    void continueWithMember(const QString& id);
    bool extractMember(const QString& id, const QString& member, QString* outPath, QString* err);
    bool installArchive(const QString& id, const QString& archive, const CatalogEntry& entry, QString* err);
    bool verifyFileSha(const QString& file, const QString& sha, QString* actual) const;
    void fail(const QString& id, const QString& message);
    QString cacheDir() const;
    QString tarExe() const;

    QString m_root;
    Catalog* m_catalog = nullptr;
    InstalledFn m_installed;
    QHash<QString, QStringList> m_dependents;
    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, Job> m_jobs;
    QJsonObject m_bundle;
};

}
