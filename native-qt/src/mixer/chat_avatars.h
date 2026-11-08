#pragma once
#include <QCache>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <functional>

namespace versus::mixer {
class ChatAvatarCache : public QObject {
public:
    explicit ChatAvatarCache(QObject *parent);
    ~ChatAvatarCache() override;
    QPixmap picture(const QString &url);
    QJsonObject status() const;
    std::function<void()> updated;
private:
    QNetworkAccessManager network_;
    QCache<QString,QPixmap> pictures_{4096}; // KiB; visible rows only request images.
    QCache<QString,qint64> failures_{256};
    QElapsedTimer clock_;
    QSet<QString> pending_;
    qint64 requests_=0, loaded_=0, failed_=0;
    void complete(const QString &key, const QImage &image);
};
}
