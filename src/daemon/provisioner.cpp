#include "daemon/provisioner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "daemon/catalog.h"
#include "shared/protocol.h"

namespace mcphub {

Provisioner::Provisioner(QString root, Catalog* catalog, InstalledFn installed, QObject* parent)
    : QObject(parent), m_root(std::move(root)), m_catalog(catalog), m_installed(std::move(installed)) {
    m_nam = new QNetworkAccessManager(this);
    QFile f(QDir(m_root).absoluteFilePath(QStringLiteral("bundle.json")));
    if (f.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) m_bundle = doc.object();
    }
}

QString Provisioner::cacheDir() const {
    return QDir(m_root).absoluteFilePath(QStringLiteral("cache/downloads"));
}

QString Provisioner::tarExe() const {
    const QString sys = QStringLiteral("C:/Windows/System32/tar.exe");
    return QFileInfo::exists(sys) ? sys : QStringLiteral("tar");
}

bool Provisioner::verifyFileSha(const QString& file, const QString& sha, QString* actual) const {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return false;
    const QString got = QString::fromLatin1(h.result().toHex());
    if (actual) *actual = got;
    return sha.isEmpty() ? true : got.compare(sha, Qt::CaseInsensitive) == 0;
}

void Provisioner::fail(const QString& id, const QString& message) {
    auto it = m_jobs.find(id);
    if (it != m_jobs.end()) {
        if (it->file) {
            it->file->close();
            it->file->deleteLater();
        }
        if (it->reply) {
            it->reply->abort();
            it->reply->deleteLater();
        }
        QFile::remove(it->targetFile + QStringLiteral(".part"));
        m_jobs.erase(it);
    }
    emit log(QStringLiteral("error"), QStringLiteral("安装 %1 失败：%2").arg(id, message));
    emit finished(id, false, message);
    for (const auto& child : m_dependents.take(id)) {
        emit log(QStringLiteral("error"), QStringLiteral("依赖 %1 安装失败，%2 取消").arg(id, child));
        emit finished(child, false, QStringLiteral("依赖 %1 安装失败").arg(id));
    }
}

void Provisioner::apply(const QString& id) {
    if (!m_catalog) return;
    const auto* e = m_catalog->find(id);
    if (!e) {
        emit finished(id, false, QStringLiteral("未知工具 %1").arg(id));
        return;
    }
    if (m_jobs.contains(id)) {
        emit finished(id, false, QStringLiteral("已在安装中"));
        return;
    }
    if (m_installed && m_installed(id)) {
        emit finished(id, true, QStringLiteral("已就绪，无需安装"));
        return;
    }
    if (e->installType == QStringLiteral("manual")) {
        emit finished(id, false, QStringLiteral("%1 需手动安装后登记目录").arg(e->name));
        return;
    }

    for (const auto& dep : e->requires) {
        const auto* de = m_catalog->find(dep);
        if (!de || de->installType == QStringLiteral("manual")) continue;
        if (m_installed && m_installed(dep)) continue;
        if (m_jobs.contains(dep)) {
            m_dependents[dep].append(id);
            emit log(QStringLiteral("info"), QStringLiteral("%1 等待依赖 %2 安装完成").arg(id, dep));
            return;
        }
        m_dependents[dep].append(id);
        emit log(QStringLiteral("info"), QStringLiteral("%1 缺少依赖 %2，先安装依赖").arg(id, de->name));
        apply(dep);
        return;
    }

    const QString member = m_bundle.value("members").toObject().value(id).toString();
    const QString bundleUrl = m_bundle.value("url").toString();
    const QString bundleSha = m_bundle.value("sha256").toString();
    const QString bundleName = m_bundle.value("file").toString(QStringLiteral("mcphub-pack.zip"));
    const QString bundleFile = cacheDir() + QLatin1Char('/') + bundleName;

    if (!bundleUrl.isEmpty() && !member.isEmpty()) {
        if (QFileInfo::exists(bundleFile) && verifyFileSha(bundleFile, bundleSha, nullptr)) {
            Job j;
            j.id = id;
            j.phase = QStringLiteral("bundle");
            j.targetFile = bundleFile;
            j.sha = bundleSha;
            j.bundleJob = true;
            m_jobs.insert(id, j);
            emit log(QStringLiteral("info"), QStringLiteral("%1 使用缓存总包（%2）").arg(id, bundleName));
            emit progress(id, QStringLiteral("extract"), 0, 1);
            QTimer::singleShot(0, this, [this, id] { continueWithMember(id); });
            return;
        }
        emit log(QStringLiteral("info"), QStringLiteral("%1 开始下载总包 %2").arg(id, bundleName));
        startDownload(id, bundleUrl, bundleSha, bundleFile, true);
        return;
    }

    if (e->sources.isEmpty()) {
        emit finished(id, false, QStringLiteral("没有可用的下载源"));
        return;
    }
    const auto src = e->sources.first();
    if (src.sha256.isEmpty()) {
        emit finished(id, false, QStringLiteral("源 %1 未提供 sha256，已拒绝").arg(src.name));
        return;
    }
    const QString fileName = QFileInfo(QUrl(src.url).path()).fileName();
    emit log(QStringLiteral("info"), QStringLiteral("%1 开始下载 %2").arg(id, fileName));
    startDownload(id, src.url, src.sha256, cacheDir() + QLatin1Char('/') + id + QLatin1Char('-') + fileName, false);
}

void Provisioner::startDownload(const QString& id, const QString& url, const QString& sha,
                                const QString& dest, bool isBundle) {
    QDir().mkpath(QFileInfo(dest).absolutePath());
    auto* file = new QFile(dest + QStringLiteral(".part"), this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file->deleteLater();
        fail(id, QStringLiteral("无法写入 %1").arg(dest + QStringLiteral(".part")));
        return;
    }

    Job j;
    j.id = id;
    j.phase = QStringLiteral("download");
    j.targetFile = dest;
    j.sha = sha;
    j.bundleJob = isBundle;
    j.file = file;
    m_jobs.insert(id, j);

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("mcphub/0.1.2"));
    auto* reply = m_nam->get(req);
    m_jobs[id].reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply] {
        auto it = m_jobs.find(id);
        if (it == m_jobs.end() || !it->file) return;
        it->file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 recv, qint64 total) {
        auto it = m_jobs.find(id);
        if (it == m_jobs.end()) return;
        it->total = total;
        emit progress(id, it->phase, recv, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, id] { onReplyFinished(id); });

    QTimer::singleShot(15 * 60 * 1000, this, [this, id] {
        auto it = m_jobs.find(id);
        if (it != m_jobs.end()) fail(id, QStringLiteral("下载超时（15 分钟）"));
    });
}

void Provisioner::onReplyFinished(const QString& id) {
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return;
    Job& j = *it;
    auto* reply = j.reply;
    const bool netErr = reply && reply->error() != QNetworkReply::NoError;
    const QString errText = reply ? reply->errorString() : QStringLiteral("reply lost");
    if (j.file) {
        j.file->flush();
        j.file->close();
        j.file->deleteLater();
        j.file = nullptr;
    }
    if (reply) reply->deleteLater();

    const QString part = j.targetFile + QStringLiteral(".part");
    if (netErr) {
        const QString target = j.targetFile;
        const bool isBundle = j.bundleJob;
        m_jobs.erase(it);
        QFile::remove(part);
        Q_UNUSED(isBundle)
        Q_UNUSED(target)
        emit log(QStringLiteral("error"), QStringLiteral("%1 下载失败：%2").arg(id, errText));
        emit finished(id, false, errText);
        return;
    }

    QFile::remove(j.targetFile);
    if (!QFile::rename(part, j.targetFile)) {
        fail(id, QStringLiteral("重命名临时文件失败"));
        return;
    }
    QString actual;
    if (!verifyFileSha(j.targetFile, j.sha, &actual)) {
        const QString got = actual;
        fail(id, QStringLiteral("sha256 不匹配（期望 %1… 实际 %2…）").arg(j.sha.left(12), got.left(12)));
        QFile::remove(j.targetFile);
        return;
    }
    emit progress(id, QStringLiteral("verify"), 1, 1);
    emit log(QStringLiteral("info"), QStringLiteral("%1 下载校验通过（%2 MB）")
                                          .arg(id)
                                          .arg(QFileInfo(j.targetFile).size() / 1024.0 / 1024.0, 0, 'f', 1));

    const bool isBundle = j.bundleJob;
    if (isBundle) {
        continueWithMember(id);
    } else {
        const auto* e = m_catalog->find(id);
        QString err;
        if (!e || !installArchive(id, j.targetFile, *e, &err)) {
            fail(id, err.isEmpty() ? QStringLiteral("解压失败") : err);
            return;
        }
        m_jobs.remove(id);
        emit progress(id, QStringLiteral("done"), 1, 1);
        emit finished(id, true, QStringLiteral("安装完成"));
        for (const auto& child : m_dependents.take(id)) {
            emit log(QStringLiteral("info"), QStringLiteral("依赖 %1 就绪，继续安装 %2").arg(id, child));
            QTimer::singleShot(0, this, [this, child] { apply(child); });
        }
    }
}

void Provisioner::continueWithMember(const QString& id) {
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return;
    const auto* e = m_catalog->find(id);
    if (!e) {
        fail(id, QStringLiteral("工具已从未知"));
        return;
    }
    const QString member = m_bundle.value("members").toObject().value(id).toString();
    const QString memberSha = m_bundle.value("memberSha256").toObject().value(id).toString();
    if (member.isEmpty()) {
        fail(id, QStringLiteral("总包中没有 %1 的成员").arg(id));
        return;
    }

    emit progress(id, QStringLiteral("extract"), 0, 1);
    QString memberPath, err;
    if (!extractMember(id, member, &memberPath, &err)) {
        fail(id, err);
        return;
    }
    if (!memberSha.isEmpty()) {
        QString actual;
        if (!verifyFileSha(memberPath, memberSha, &actual)) {
            fail(id, QStringLiteral("内层 %1 sha256 不匹配").arg(member));
            return;
        }
    }
    if (!installArchive(id, memberPath, *e, &err)) {
        fail(id, err);
        return;
    }
    const QString tmpDir = cacheDir() + QStringLiteral("/extract-") + id;
    QDir(tmpDir).removeRecursively();

    m_jobs.remove(id);
    emit progress(id, QStringLiteral("done"), 1, 1);
    emit log(QStringLiteral("info"), QStringLiteral("%1 已安装到 %2").arg(e->name, QDir::toNativeSeparators(e->dest)));
    emit finished(id, true, QStringLiteral("安装完成"));
    for (const auto& child : m_dependents.take(id)) {
        emit log(QStringLiteral("info"), QStringLiteral("依赖 %1 就绪，继续安装 %2").arg(id, child));
        QTimer::singleShot(0, this, [this, child] { apply(child); });
    }
}

bool Provisioner::extractMember(const QString& id, const QString& member, QString* outPath, QString* err) {
    const QString bundleFile = m_jobs.value(id).targetFile;
    const QString tmpDir = cacheDir() + QStringLiteral("/extract-") + id;
    QDir(tmpDir).removeRecursively();
    QDir().mkpath(tmpDir);

    QProcess p;
    p.setProgram(tarExe());
    p.setArguments({QStringLiteral("-xf"), bundleFile, QStringLiteral("-C"), tmpDir, member});
    p.start();
    if (!p.waitForStarted(5000) || !p.waitForFinished(10 * 60 * 1000) || p.exitCode() != 0) {
        if (err) *err = QStringLiteral("解包总包失败：%1").arg(QString::fromLocal8Bit(p.readAllStandardError()));
        return false;
    }
    const QString path = tmpDir + QLatin1Char('/') + member;
    if (!QFileInfo::exists(path)) {
        if (err) *err = QStringLiteral("总包里找不到 %1").arg(member);
        return false;
    }
    if (outPath) *outPath = path;
    return true;
}

bool Provisioner::installArchive(const QString& id, const QString& archive, const CatalogEntry& entry,
                                 QString* err) {
    const QString dest = expandPath(entry.dest, m_root);
    if (dest.isEmpty()) {
        if (err) *err = QStringLiteral("catalog 未声明 dest");
        return false;
    }
    if (QFileInfo::exists(dest) && !QDir(dest).isEmpty()) {
        if (err) *err = QStringLiteral("目标目录已存在且非空：%1").arg(QDir::toNativeSeparators(dest));
        return false;
    }

    const QString staging = QDir(m_root).absoluteFilePath(
        QStringLiteral(".staging/%1-%2").arg(id).arg(QDateTime::currentMSecsSinceEpoch()));
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);

    QProcess p;
    p.setProgram(tarExe());
    p.setArguments({QStringLiteral("-xf"), archive, QStringLiteral("-C"), staging, QStringLiteral("--strip-components"),
                    QStringLiteral("1")});
    p.start();
    if (!p.waitForStarted(5000) || !p.waitForFinished(10 * 60 * 1000) || p.exitCode() != 0) {
        const QString msg = QStringLiteral("解压失败：%1").arg(QString::fromLocal8Bit(p.readAllStandardError()).left(200));
        QDir(staging).removeRecursively();
        if (err) *err = msg;
        return false;
    }

    QDir().mkpath(QFileInfo(dest).absolutePath());

    bool moved = false;
    for (int i = 0; i < 10 && !moved; ++i) {
        if (QFileInfo::exists(dest)) QDir(dest).removeRecursively();
        moved = QDir().rename(staging, dest);
        if (!moved) QThread::msleep(180);
    }
    if (!moved) {
        const int files = QDir(staging).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size();
        QDir(staging).removeRecursively();
        if (err) {
            *err = QStringLiteral("移动到 %1 失败（staging %2 项，dest 存在=%3）")
                       .arg(QDir::toNativeSeparators(dest))
                       .arg(files)
                       .arg(QFileInfo::exists(dest));
        }
        return false;
    }
    return true;
}

}
