#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <atomic>
#include <memory>

namespace versus::mixer {
// The active lease follows Open/Save As. Exporting a snapshot takes a temporary
// lease too, so another Studio instance cannot overwrite an open project.
class ShowFileAccess {
public:
    struct Lease {
        QString path;
        std::unique_ptr<QLockFile> lock;
        bool allowed = false;
    };
    Lease reserve(const QString &path) const {
        const QFileInfo info(path);
        const auto canonical = info.canonicalFilePath();
        Lease lease{canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical};
        if (active_.allowed && lease.path.compare(active_.path,Qt::CaseInsensitive) == 0) { lease.allowed = true; return lease; }
        QDir().mkpath(QFileInfo(lease.path).absolutePath());
        lease.lock = std::make_unique<QLockFile>(lease.path+".lock");
        // A production can remain open for hours. Never steal a live lock just
        // because its file is older than QLockFile's default stale interval.
        lease.lock->setStaleLockTime(0);
        lease.allowed = lease.lock->tryLock(0);
        return lease;
    }
    void activate(Lease lease) { if (lease.allowed && lease.lock) active_ = std::move(lease); }
    QString path() const { return active_.path; }
private:
    Lease active_;
};
inline ShowFileAccess &showFileAccess() { static ShowFileAccess access; return access; }

inline QString writeShowFile(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return file.errorString();
    return {};
}
// Runs off the UI thread. The final folder only appears after every asset and
// the show have been written. Never overwrite or remove a caller-owned folder.
inline QString collectShow(QJsonObject show, const QString &destination, const std::atomic<bool> &cancelled) {
    const QFileInfo target(destination);
    if (!target.isAbsolute() || target.exists() || !target.dir().exists()) return "Choose a new folder inside an existing directory.";
    QTemporaryDir temporary(target.dir().filePath(".desktop-studio-XXXXXX"));
    if (!temporary.isValid()) return "Could not create the collection folder.";
    QDir root(temporary.path()); if (!root.mkdir("assets")) return "Could not create the assets folder.";
    auto sources = show["sources"].toArray(); QHash<QString,QString> copied;
    QByteArray buffer(1024*1024,Qt::Uninitialized);
    for (int n=0; n<sources.size(); ++n) {
        if (cancelled) return "Show collection cancelled.";
        auto source = sources[n].toObject(); const auto kind = source["kind"].toString();
        if (kind != "image" && kind != "media") continue;
        const QFileInfo asset(source["locator"].toString());
        if (!asset.isFile()) return "Missing asset: "+source["name"].toString()+". Relink it before collecting.";
        const auto identity = asset.canonicalFilePath();
        auto relative = copied.value(identity);
        if (relative.isEmpty()) {
            relative = "assets/"+QString::number(n)+"-"+asset.fileName();
            QFile input(asset.absoluteFilePath()); QSaveFile output(root.filePath(relative));
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return "Could not copy asset: "+asset.fileName();
            while (!input.atEnd()) {
                if (cancelled) return "Show collection cancelled.";
                const auto count = input.read(buffer.data(),buffer.size());
                if (count < 0 || output.write(buffer.constData(),count) != count) return "Asset copy failed: "+asset.fileName();
            }
            if (!output.commit()) return "Could not finish asset: "+asset.fileName();
            copied.insert(identity,relative);
        }
        source["locator"] = relative; sources[n] = source;
    }
    show["sources"] = sources;
    const auto error = writeShowFile(root.filePath("show.mixer.json"),QJsonDocument(show).toJson());
    if (!error.isEmpty()) return error;
    if (cancelled) return "Show collection cancelled.";
    if (!QDir().rename(temporary.path(),target.absoluteFilePath())) return "Could not finish the new folder. Its name may already be in use.";
    temporary.setAutoRemove(false); return {};
}
}
