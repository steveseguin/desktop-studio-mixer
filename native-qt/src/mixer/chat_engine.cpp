#include "chat_engine.h"
#include "versus/mixer/process_worker.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QLockFile>
#include <QElapsedTimer>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace versus::mixer {
struct ChatEngine::Impl {
    std::atomic<bool> running{true};
    std::mutex mutex;
    std::deque<QJsonObject> commands;
    QJsonObject configuration, health;
    QJsonArray messages;
    QImage image;
    QString imageToken;
    std::thread thread;
    std::unique_ptr<QLockFile> owner;
};
ChatEngine::ChatEngine(QObject *parent) : QObject(parent), model(this), impl_(std::make_unique<Impl>()) {
    const auto root=QCoreApplication::applicationDirPath()+"/ssn";
    packAvailable=verifyChatPack(root);
    QFile manifestFile(root+"/manifest.json");
    if(manifestFile.open(QIODevice::ReadOnly))manifest=QJsonDocument::fromJson(manifestFile.readAll()).object();
    QDir().mkpath(studioDataPath());
    impl_->owner=std::make_unique<QLockFile>(studioDataPath()+"/chat-owner.lock");impl_->owner->setStaleLockTime(0);
    writable_=impl_->owner->tryLock(0);
    if(!writable_)settingsError="Chat is open in another Studio window. Close that window and restart this one to use chat here.";
    else writable_=settings.load(studioDataPath()+"/chat.json",&settingsError);
    if(!writable_)settings.channels={};
    auto *state=impl_.get();
    state->thread=std::thread([state] {
        ProcessWorker worker; QJsonObject configured; auto nextStart=std::chrono::steady_clock::now();
        qint64 lastHeartbeat=-1;auto heartbeatAt=nextStart;
        auto resourceAt=nextStart,recoveryUntil=nextStart;QJsonObject resources;QString transferredToken;qint64 transferMs=0;
        while(state->running) {
            QJsonObject wanted;std::deque<QJsonObject> commands;
            {std::lock_guard lock(state->mutex);wanted=state->configuration;commands.swap(state->commands);}
            const bool needed=wanted["available"].toBool() && (!wanted["channels"].toArray().isEmpty()||wanted["practice"].toBool());
            if(!needed) {
                if(worker.alive())worker.stop();
                configured={};resources={};
                // Removing the last connection releases the worker. Its previous
                // ready flag and memory sample must not remain as live status.
                std::lock_guard lock(state->mutex);
                state->health={{"ready",false}};state->messages={};
                state->image={};state->imageToken.clear();
            }
            else {
                if(!worker.alive() && std::chrono::steady_clock::now()>=nextStart) {
                    nextStart=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                    // start() replaces the old job, so read any memory-limit event it left first.
                    if(worker.resourceUsage()["limit_exceeded"].toBool())recoveryUntil=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                    if(worker.start("chat")) {auto c=wanted;c["op"]="start";worker.call(c);configured=wanted;heartbeatAt=std::chrono::steady_clock::now();lastHeartbeat=-1;}
                }
                if(worker.alive()) {
                    const auto now=std::chrono::steady_clock::now();
                    if(now>=resourceAt){resources=worker.resourceUsage();resourceAt=now+std::chrono::seconds(1);if(resources["limit_exceeded"].toBool()){recoveryUntil=now+std::chrono::seconds(10);nextStart=now+std::chrono::seconds(3);}}
                    if(resources["limit_exceeded"].toBool()&&!worker.alive()){{std::lock_guard lock(state->mutex);state->health={{"ready",false},{"resources",resources},{"error","Chat used too many resources. Restarting chat; your output continues."}};}std::this_thread::sleep_for(std::chrono::milliseconds(50));continue;}
                    if(wanted!=configured){auto c=wanted;c["op"]="configure";worker.call(c);configured=wanted;}
                    for(const auto &c:commands) {if(c["op"]=="signin")worker.allowForegroundActivation();const auto reply=worker.call(c);if(reply.contains("error")){std::lock_guard lock(state->mutex);state->health["error"]=reply["error"];}}
                    auto result=worker.call({{"op","poll"}});
                    QElapsedTimer transfer;transfer.start();QString imageToken;auto image=worker.image(false,&imageToken);if(imageToken!=transferredToken){transferMs=transfer.elapsed();transferredToken=imageToken;}
                    const auto heartbeat=worker.status()["heartbeat"].toInteger();
                    if(heartbeat!=lastHeartbeat){lastHeartbeat=heartbeat;heartbeatAt=std::chrono::steady_clock::now();}
                    else if(std::chrono::steady_clock::now()-heartbeatAt>std::chrono::seconds(10))worker.terminate();
                    std::lock_guard lock(state->mutex);
                    for(const auto &m:result["messages"].toArray()){if(state->messages.size()==2000)state->messages.removeAt(0);state->messages.append(m);}
                    result.remove("messages");
                    result["resources"]=resources;auto timing=result["timing"].toObject();timing["frame_transfer_ms"]=transferMs;result["timing"]=timing;
                    if(std::chrono::steady_clock::now()<recoveryUntil)result["error"]="Chat used too many resources. Restarting chat; your output continues.";
                    if(!result["ready"].toBool()) {
                        if(!result["ok"].toBool()&&std::chrono::steady_clock::now()>=recoveryUntil)result["error"]="Comments paused. Reconnecting.";
                        else if(result["error"].toString().isEmpty()&&!state->health["error"].toString().isEmpty())result["error"]=state->health["error"];
                    }
                    state->health=result;
                    if(!image.isNull()&&!imageToken.isEmpty()){state->image=image;state->imageToken=imageToken;}
                } else {std::lock_guard lock(state->mutex);state->health={{"ready",false},{"resources",resources},{"error",std::chrono::steady_clock::now()<recoveryUntil?"Chat used too many resources. Restarting chat; your output continues.":"Comments paused. Reconnecting."}};}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        worker.stop();
    });
    connect(&poll_,&QTimer::timeout,this,[this] {
        QJsonArray messages;
        {std::lock_guard lock(impl_->mutex);messages=std::exchange(impl_->messages,{});health=impl_->health;
            if(!impl_->image.isNull()){image_=impl_->image;renderedToken_=impl_->imageToken;}}
        QList<QJsonObject> batch;
        for(const auto &entry:messages){auto m=entry.toObject();
            if(m.value("removed").toBool()){model.appendBatch(batch);batch.clear();emit commentDeleted(m);model.removeMatching(m);continue;}
            if(m["practice"].toBool()&&!settings.practice)continue;m["engineId"]=m.value("id");m["id"]=QUuid::createUuid().toString(QUuid::Id128);batch.append(m);
        }model.appendBatch(batch);
        emit updated();
    });poll_.start(33);configure();
}
ChatEngine::~ChatEngine(){poll_.stop();impl_->running=false;if(impl_->thread.joinable())impl_->thread.join();}
void ChatEngine::configure() {
    std::lock_guard lock(impl_->mutex);impl_->configuration={{"available",packAvailable&&writable_},{"channels",settings.channels},{"practice",settings.practice},{"room",settings.room}};
}
QJsonObject ChatEngine::addChannel(const QString &input) {
    if(!writable_)return {{"error",settingsError}};
    if(!packAvailable)return {{"error","Chat is unavailable. Reinstall Studio to restore its chat files."}};
    if(settings.channels.size()>=16)return {{"error","You can add up to 16 channels."}};
    QString error;auto channel=detectChatChannel(input,manifest,&error);if(channel.isEmpty())return {{"error",error}};
    for(const auto &v:settings.channels)if(v.toObject()["url"]==channel["url"])return {{"error","That channel is already added."}};
    settings.channels.append(channel);
    if(!settings.save(studioDataPath()+"/chat.json",&settingsError)){settings.channels.removeLast();return {{"error",settingsError}};}
    configure();return {{"ok",true},{"channel",channel}};
}
bool ChatEngine::removeChannel(const QString &id) {
    if(!writable_)return false;
    for(int i=0;i<settings.channels.size();++i)if(settings.channels[i].toObject()["id"].toString()==id){auto old=settings.channels;settings.channels.removeAt(i);if(!settings.save(studioDataPath()+"/chat.json",&settingsError)){settings.channels=old;return false;}configure();return true;}
    return false;
}
bool ChatEngine::setPractice(bool enabled) {
    if(!writable_||!packAvailable)return false;
    const auto old=settings.practice;settings.practice=enabled;
    if(!settings.save(studioDataPath()+"/chat.json",&settingsError)){settings.practice=old;return false;}configure();return true;
}
void ChatEngine::send(QJsonObject command) {std::lock_guard lock(impl_->mutex);if(impl_->commands.size()<100)impl_->commands.push_back(std::move(command));}
}
