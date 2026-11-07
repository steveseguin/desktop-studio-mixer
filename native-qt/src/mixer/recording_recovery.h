#pragma once
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>

namespace versus::mixer {
// A show owns its recording journal, just as it owns its autosave lock. Write
// before starting a writer; only retire a path after finalization or export.
class RecordingRecovery {
    QString journal_;
    QString last_;
    QStringList pending_;
    bool save() const {
        QSaveFile file(journal_);
        if (!file.open(QIODevice::WriteOnly)) return false;
        const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"last",last_},{"pending",QJsonArray::fromStringList(pending_)}}).toJson();
        return file.write(bytes)==bytes.size() && file.commit();
    }
public:
    void load(const QString &show) {
        journal_=show+".recordings.json";last_.clear();pending_.clear();
        QFile file(journal_);if (!file.open(QIODevice::ReadOnly) || file.size()>1024*1024) return;
        const auto data=QJsonDocument::fromJson(file.readAll()).object();
        if (data["version"].toInt()!=1) return;
        const auto last=data["last"].toString();
        if (QFileInfo(last).isAbsolute()) last_=last;
        for (const auto &value:data["pending"].toArray()) {
            const QFileInfo info(value.toString());
            if (info.isAbsolute() && info.isFile() && info.size()>0 && info.suffix().compare("mkv",Qt::CaseInsensitive)==0)
                pending_.append(info.absoluteFilePath());
        }
        pending_.removeDuplicates();
    }
    bool begin(const QStringList &paths) {
        const auto previous=pending_;const auto last=last_;
        for (const auto &path:paths) pending_.append(QFileInfo(path).absoluteFilePath());
        pending_.removeDuplicates();last_=QFileInfo(paths.first()).absoluteFilePath();
        if (save()) return true;
        pending_=previous;last_=last;return false;
    }
    void finished(const QStringList &paths) {
        for (const auto &path:paths) pending_.removeAll(QFileInfo(path).absoluteFilePath());
        save(); // A failed update keeps the original journal for another recovery attempt.
    }
    QStringList pending() const {return pending_;}
    QString lastPath() const {return last_;}
};
}
