#include "chat_avatars.h"
#include "chat_data.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QImageReader>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>
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
    profileWake_.setSingleShot(true);
    connect(&profileWake_,&QTimer::timeout,this,[this]{if(updated)updated();});
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
    const auto profile=chatAvatarProfileUrl(key);
    if(!profile.isEmpty()){
        // Two profile lookups per second, even while a busy feed repaints. No
        // queue of departed rows: a repaint retries only the currently visible ones.
        const auto wait=nextProfileAt_-clock_.elapsed();
        if(wait>0){if(!profileWake_.isActive())profileWake_.start(int(wait));return {};}
        nextProfileAt_=clock_.elapsed()+500;
    }
    pending_.insert(key);++requests_;
    fetch(key,profile.isEmpty()?key:profile,!profile.isEmpty());
    return {};
}
void ChatAvatarCache::fetch(const QString &key,const QString &url,bool profile) {
    if(profile)++profileRequests_;
    const qsizetype limit=profile?16*1024:MaxBytes;
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Accept",profile?"application/json":"image/png,image/jpeg,image/webp,image/gif");
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
    const auto receive=[reply,bytes,rejected,limit]{
        if(*rejected)return;
        bytes->append(reply->read(limit+1-bytes->size()));
        if(bytes->size()>limit){*rejected=true;reply->abort();}
    };
    connect(reply,&QNetworkReply::redirected,this,[reply,profile](const QUrl &url){
        if(profile||chatAvatarUrl(url.toString()).isEmpty()||!chatAvatarProfileUrl(url.toString()).isEmpty())reply->abort();else reply->redirectAllowed();
    });
    connect(reply,&QNetworkReply::metaDataChanged,this,[reply,rejected,limit]{
        if(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()>limit){*rejected=true;reply->abort();}
    });
    connect(reply,&QIODevice::readyRead,this,receive);
    QTimer::singleShot(10000,reply,[reply]{reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,key,bytes,rejected,receive,profile]{
        receive();
        const auto code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(profile){const auto bucket=code>=100&&code<=599?QString::number(code):QString("network_error");profileResponses_[bucket]=profileResponses_[bucket].toDouble()+1;}
        if(profile&&code==429){
            const auto raw=reply->rawHeader("Retry-After");bool numeric=false;auto retry=raw.toLongLong(&numeric);
            if(!numeric){const auto date=QDateTime::fromString(QString::fromLatin1(raw),Qt::RFC2822Date);retry=date.isValid()?QDateTime::currentDateTimeUtc().secsTo(date):60;}
            const qint64 seconds=std::clamp<qint64>(retry,30,3600);
            nextProfileAt_=clock_.elapsed()+seconds*1000;profileWake_.start(int(seconds*1000));
        }
        const bool ok=!*rejected&&reply->error()==QNetworkReply::NoError&&
            code==200;
        reply->deleteLater();
        if(!ok){complete(key,{});return;}
        if(profile){
            const auto photo=chatAvatarUrl(QJsonDocument::fromJson(*bytes).object()["profilepic"].toString());
            if(photo.isEmpty()||!chatAvatarProfileUrl(photo).isEmpty()){complete(key,{});return;}
            fetch(key,photo,false);return;
        }
        // Image decoding must not compete with the GUI's video/recording timers.
        const QPointer<ChatAvatarCache> owner(this);
        QThreadPool::globalInstance()->start(QRunnable::create([owner,key,bytes]{
            const auto image=decodeAvatar(*bytes);
            QMetaObject::invokeMethod(QCoreApplication::instance(),[owner,key,image]{
                if(owner)owner->complete(key,image);
            },Qt::QueuedConnection);
        }));
    });
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
        {"requests",requests_},{"loaded",loaded_},{"failed",failed_},{"profile_requests",profileRequests_},{"profile_responses",profileResponses_}};
}
}
