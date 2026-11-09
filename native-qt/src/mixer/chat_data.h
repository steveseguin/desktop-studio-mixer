#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QUrl>
#include <functional>
class QItemSelectionModel;

namespace versus::mixer {
QString studioDataPath();
bool chatUrlMatches(const QString &pattern, const QUrl &url);
bool chatScriptMatches(const QJsonObject &entry, const QUrl &url, bool subframe = false);
bool chatCaptureAllowed(const QJsonObject &manifest, const QUrl &page, const QUrl &sender, bool subframe);
bool chatCaptureChannelMatches(const QUrl &configured, const QUrl &candidate);
bool verifyChatPack(const QString &root);
QString chatPlatformName(const QString &type);
QString chatAvatarUrl(const QString &value);
QString chatAvatarProfileUrl(const QString &value);
QString chatPageStatus(const QJsonObject &probe);
QString chatCommentText(const QJsonObject &captured);
QString chatSupportLabel(const QJsonObject &message);
bool chatIsSupportEvent(const QJsonObject &message);
QString chatSignInUrl(const QString &platform);
bool chatSignInAllowed(const QString &platform, const QUrl &url);
bool chatMatchesDeletion(const QJsonObject &message, const QJsonObject &deletion);
void watchChatSelection(QItemSelectionModel *selection, QObject *context, std::function<void(QString)> changed);
QJsonObject detectChatChannel(QString input, const QJsonObject &manifest, QString *error);
struct ChatSettings {
    QJsonArray channels;
    bool practice = false;
    QString room;
    bool load(const QString &path, QString *error);
    bool save(const QString &path, QString *error) const;
};
struct CommentsLayer {
    bool enabled = true;
    QString style = "bar";
    QRectF rect{0, 0, 1, 1};
    QJsonObject json() const;
    static bool read(const QJsonValue &value, CommentsLayer &layer);
};
class ChatMessageModel : public QAbstractListModel {
public:
    explicit ChatMessageModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(rows_.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    void append(QJsonObject message);
    void appendBatch(const QList<QJsonObject> &messages);
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    int pendingCount() const { return int(pending_.size()); }
    QJsonObject message(const QString &id) const;
    QJsonArray messages() const;
    void toggleSaved(const QString &id);
    void clearPractice();
    void removeMatching(const QJsonObject &deletion);
private:
    QList<QJsonObject> rows_, pending_;
    bool paused_ = false;
};
}
