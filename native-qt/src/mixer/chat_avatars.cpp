#include "chat_avatars.h"
#include "chat_data.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QImageReader>
#include <QNetworkReply>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>
#include <memory>

namespace versus::mixer {
namespace {
constexpr qsizetype MaxBytes=256*1024;
QImage decodeAvatar(QByteArray bytes) {
    QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);reader.setDecideFormatFromContent(true);
    const auto format=reader.format().toLower();
    if(!QList<QByteArray>{"png","jpeg","webp","gif"}.contains(format))return {};
    const auto size=reader.size();
    if(size.isEmpty()||
        size.width()>2048||size.height()>2048||qint64(size.width())*size.height()>1024*1024||
        size.width()>size.height()*4||size.height()>size.width()*4)return {};
    reader.setScaledSize(size.scaled(84,84,Qt::KeepAspectRatioByExpanding));
    auto image=reader.read();if(image.isNull())return {};
    image=image.scaled(84,84,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
    return image.copy((image.width()-84)/2,(image.height()-84)/2,84,84);
}
}
ChatAvatarCache::ChatAvatarCache(QObject *parent):QObject(parent),network_(this) {
    clock_.start();
    connect(&network_,&QNetworkAccessManager::authenticationRequired,this,
        [](QNetworkReply *reply,QAuthenticator *){reply->abort();});
}
ChatAvatarCache::~ChatAvatarCache() {
    updated={};
    for(auto *reply:network_.findChildren<QNetworkReply*>()){
        disconnect(reply,nullptr,this,nullptr);reply->abort();
    }
}
QPixmap ChatAvatarCache::picture(const QString &value) {
    const auto key=chatAvatarUrl(value);if(key.isEmpty())return {};
    if(const auto picture=pictures_.object(key))return *picture;
    if(const auto until=failures_.object(key)){if(*until>clock_.elapsed())return {};failures_.remove(key);}
    if(pending_.contains(key)||pending_.size()>=4)return {};
    pending_.insert(key);++requests_;
    QNetworkRequest request{QUrl(key)};
    request.setRawHeader("Accept","image/png,image/jpeg,image/webp,image/gif");
    request.setRawHeader("Accept-Encoding","identity");
    request.setRawHeader("User-Agent","Desktop-Studio/avatar");
    request.setRawHeader("Origin","https://socialstream.ninja");
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);request.setTransferTimeout(8000);
    auto *reply=network_.get(request);reply->setReadBufferSize(32*1024);
    auto bytes=std::make_shared<QByteArray>();auto rejected=std::make_shared<bool>(false);
    const auto receive=[reply,bytes,rejected]{
        if(*rejected)return;
        bytes->append(reply->read(MaxBytes+1-bytes->size()));
        if(bytes->size()>MaxBytes){*rejected=true;reply->abort();}
    };
    connect(reply,&QNetworkReply::redirected,this,[reply](const QUrl &url){
        if(chatAvatarUrl(url.toString()).isEmpty())reply->abort();else reply->redirectAllowed();
    });
    connect(reply,&QNetworkReply::metaDataChanged,this,[reply,rejected]{
        if(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()>MaxBytes){*rejected=true;reply->abort();}
    });
    connect(reply,&QIODevice::readyRead,this,receive);
    QTimer::singleShot(10000,reply,[reply]{reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,key,bytes,rejected,receive]{
        receive();
        const bool ok=!*rejected&&reply->error()==QNetworkReply::NoError&&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200;
        reply->deleteLater();
        if(!ok){complete(key,{});return;}
        // Image decoding must not compete with the GUI's video/recording timers.
        const QPointer<ChatAvatarCache> owner(this);
        QThreadPool::globalInstance()->start(QRunnable::create([owner,key,bytes]{
            const auto image=decodeAvatar(*bytes);
            QMetaObject::invokeMethod(QCoreApplication::instance(),[owner,key,image]{
                if(owner)owner->complete(key,image);
            },Qt::QueuedConnection);
        }));
    });
    return {};
}
void ChatAvatarCache::complete(const QString &key,const QImage &image) {
    pending_.remove(key);
    if(image.isNull()){failures_.insert(key,new qint64(clock_.elapsed()+60000));++failed_;}
    else {auto picture=new QPixmap(QPixmap::fromImage(image));picture->setDevicePixelRatio(3);
        pictures_.insert(key,picture,int((image.sizeInBytes()+1023)/1024));++loaded_;}
    if(updated)updated();
}
QJsonObject ChatAvatarCache::status() const {
    return {{"cached",pictures_.size()},{"cache_kib",pictures_.totalCost()},{"in_flight",pending_.size()},
        {"requests",requests_},{"loaded",loaded_},{"failed",failed_}};
}
}
