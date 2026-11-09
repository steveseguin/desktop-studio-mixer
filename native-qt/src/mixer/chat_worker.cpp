#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QScreen>
#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <stdexcept>
#include <cmath>
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;



static QByteArray json(QJsonObject o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
static QByteArray read(QString p) { QFile f(p);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error(("Cannot read "+p).toStdString());return f.readAll(); }
static QString take(LPWSTR p) { auto s=QString::fromWCharArray(p?p:L"");CoTaskMemFree(p);return s; }
#include "chat_engine.h"
#include <QUrlQuery>
#include <QPointer>
#include <QLockFile>
#include <deque>
namespace versus::mixer {
namespace {
struct Host;
struct Page:QWidget {
    Host &host; int id;QString role,url,mainFrame,channel; bool active=false, busy=false; int failures=0, messageCount=0; QElapsedTimer messageClock;
    ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> web;
    std::map<QString,ComPtr<ICoreWebView2Frame2>> captureFrames;
    QSet<qint64> deletedIds;
    qint64 probeAt=0, retryAt=0;int retrySeconds=60;bool probePending=false, terminal=false;
    QWidget *browserArea=nullptr;QLabel *accountLocation=nullptr,*accountHelp=nullptr;QPushButton *accountBack=nullptr;UINT64 accountNavigation=0;
    Page(Host &h,int i,QString r,QString u):host(h),id(i),role(r),url(u){
        resize(1280,720);setWindowTitle("Desktop Studio chat");setAttribute(Qt::WA_QuitOnClose,false);
        if(role=="signin") {
            const auto available=QGuiApplication::primaryScreen()->availableGeometry();
            resize(std::min(1120,available.width()-60),std::min(800,available.height()-60));setMinimumSize(540,480);move(available.center()-rect().center());
            auto *layout=new QVBoxLayout(this);layout->setContentsMargins(12,10,12,10);
            auto *bar=new QHBoxLayout;auto *back=new QPushButton("Back",this);accountBack=back;back->setEnabled(false);bar->addWidget(back);auto *reload=new QPushButton("Reload",this);bar->addWidget(reload);
            accountLocation=new QLabel(this);accountLocation->setTextFormat(Qt::PlainText);bar->addWidget(accountLocation,1);
            auto *done=new QPushButton("Return to chat",this);bar->addWidget(done);layout->addLayout(bar);
            accountHelp=new QLabel("Sign in on the platform's page, then Return to chat. This session is remembered on this device. Use email, username or QR sign-in; third-party sign-in providers are not supported here.",this);
            accountHelp->setTextFormat(Qt::PlainText);accountHelp->setWordWrap(true);layout->addWidget(accountHelp);setProperty("defaultAccountHelp",accountHelp->text());
            browserArea=new QWidget(this);browserArea->setMinimumSize(500,360);browserArea->installEventFilter(this);layout->addWidget(browserArea,1);
            QObject::connect(done,&QPushButton::clicked,this,&QWidget::close);
            QObject::connect(back,&QPushButton::clicked,this,[this]{if(web){BOOL available=FALSE;web->get_CanGoBack(&available);if(available)web->GoBack();}});
            QObject::connect(reload,&QPushButton::clicked,this,[this]{if(web)web->Reload();});
        }
        winId();
    }
    ~Page(){if(controller)controller->Close();}
    QJsonObject tab(){return {{"id",id},{"url",url},{"title",role},{"active",active},{"windowId",1},{"status",web?"complete":"loading"}};}
    void start();void configure();void configureSignIn();void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object,QEvent *event) override {if(object==browserArea&&event->type()==QEvent::Resize&&controller){RECT b{0,0,browserArea->width(),browserArea->height()};controller->put_Bounds(b);}return QWidget::eventFilter(object,event);}
    void moveEvent(QMoveEvent *event) override {QWidget::moveEvent(event);if(controller)controller->NotifyParentWindowPositionChanged();}
    void snapshot(QString token);
    void receiveNative(ICoreWebView2WebMessageReceivedEventArgs *args,ICoreWebView2Frame *sender=nullptr);
    void post(QJsonObject o){const auto bytes=QString::fromUtf8(json(o)).toStdWString();auto it=captureFrames.find(o["frame"].toString());if(it!=captureFrames.end())it->second->PostWebMessageAsJson(bytes.c_str());else if(web)web->PostWebMessageAsJson(bytes.c_str());}
};
struct Peer { int tab;QString frame;QJsonObject data; };
struct Pending {int tab;QString frame,id;int receiver;QString receiverFrame;};
struct Host {
    const QString captureEpoch=QUuid::createUuid().toString(QUuid::Id128);
    QString root,profile,session,shim;QJsonObject manifest,storage,diagnostic; qint64 startedAt=0;
    std::map<QString,qint64> lastComment;
    qint64 oversizedMessages=0;
    QString accountNotice;bool clearingAccounts=false;
    QJsonArray feed; QJsonObject states; QJsonArray channels; bool practice=false, ready=false; QString error, renderedToken, pendingToken; QJsonObject pendingCard; qint64 requestedAt=0; std::function<void(QImage,QString)> frame; QTimer timer;ComPtr<ICoreWebView2Environment> environment;
    std::map<int,std::unique_ptr<Page>> pages;std::map<QString,Peer> peers;std::map<QString,Pending> pending;
    int nextTab=1;quint64 sequence=0;UINT32 browserPid=0;
    void log(QString event,QJsonObject o={}) {
        if(event=="native-error") {error="Chat is unavailable. Restart Studio to try again.";diagnostic=o;}
    }
    QString captureBundle;
    std::unique_ptr<QLockFile> profileOwner;
    QJsonObject names,timing,startupTiming;
    const bool resourceDiagnostics=qEnvironmentVariableIntValue("VIDEO_MIXER_CHAT_RESOURCE_DIAGNOSTICS")==1;
    qint64 resourceDiagnosticAt=0;bool resourceDiagnosticPending=false;QJsonArray resourceProcesses;
    void sampleResources();
    void setup(const QJsonObject &c);
    void configureChannels(const QJsonObject &c);
    void finishSignIn(int id);
    void reloadCaptures(const QString &platform={});
    void clearAccountData(Page *holder);
    void tick();
    QJsonObject command(const QJsonObject &c);
    bool check(HRESULT hr,QString op,int tab=0){if(SUCCEEDED(hr))return true;log("native-error",{{"operation",op},{"tab",tab},{"hresult",QString::number(quint32(hr),16)}});return false;}
    Page *page(int id){auto i=pages.find(id);return i==pages.end()?nullptr:i->second.get();}
    int open(QString role,QString url){int id=nextTab++;auto p=std::make_unique<Page>(*this,id,role,url);pages[id]=std::move(p);pages[id]->start();log("page-open",{{"tab",id},{"role",role},{"url",url}});return id;}
    void broadcast(QString name,QJsonArray args){for(auto &[id,p]:pages)p->post({{"kind","event"},{"name",name},{"args",args}});}
    void reply(int tab,QString frame,QString id,QJsonValue value={},QString error={}){if(auto p=page(tab))p->post({{"kind","reply"},{"frame",frame},{"id",id},{"value",value},{"error",error}});}
    void deliver(QString uuid,QJsonObject packet){auto i=peers.find(uuid);if(i==peers.end())return;auto &p=i->second;if(auto w=page(p.tab))w->post({{"kind","bridge"},{"frame",p.frame},{"packet",packet}});}
    bool views(const Peer &viewer,const Peer &publisher){
        auto a=viewer.data,b=publisher.data;
        if(a["room"]!=b["room"]||a["password"]!=b["password"]||b["push"].toString().isEmpty())return false;
        if(a["exclude"].toString().split(',').contains(b["push"].toString()))return false;
        // A blank push in dock.html implies room auto-view; background's explicit blank view disables it.
        return a["scene"].toBool()||a["view"].toString().split(',').contains(b["push"].toString())||
          (a["label"]=="dock" && a["view"].toString().isEmpty());
    }
    void connect(QString viewer,QString publisher){
        deliver(viewer,{{"action","view-connection"},{"UUID",publisher},{"value",true}});
        deliver(viewer,{{"action","view-connection-info"},{"UUID",publisher},{"value",QJsonObject{{"label",peers[publisher].data["label"]}}}});
        deliver(publisher,{{"action","push-connection"},{"UUID",viewer},{"value",true}});
        deliver(publisher,{{"action","push-connection-info"},{"UUID",viewer},{"value",QJsonObject{{"label",peers[viewer].data["label"]}}}});
    }
    void leave(QString uuid){auto i=peers.find(uuid);if(i==peers.end())return;auto old=i->second;for(auto &[other,p]:peers)if(other!=uuid){if(views(p,old))deliver(other,{{"action","view-connection"},{"UUID",uuid},{"value",false}});if(views(old,p))deliver(other,{{"action","push-connection"},{"UUID",uuid},{"value",false}});}peers.erase(uuid);}
    void leaveTab(int tab){QStringList ids;for(auto &[id,p]:peers)if(p.tab==tab)ids<<id;for(auto id:ids)leave(id);}
    void closePage(int tab) {
        auto p=page(tab);if(!p)return;
        const bool capture=p->role=="capture"||p->role=="practice";
        leaveTab(tab);pages.erase(tab);
        // SSN uses tabs.onRemoved to retire its preferred YouTube source and
        // per-tab caches. Without it, a replacement tab's comments are discarded.
        if(capture)for(auto &[id,receiver]:pages)if(receiver->role=="engine")
            receiver->post({{"kind","event"},{"name","tabs.onRemoved"},{"args",QJsonArray{tab,QJsonObject{{"windowId",1},{"isWindowClosing",false}}}}});
    }
    void route(int from,QString frame,QString requestId,int to,QJsonValue message){
        auto p=page(to);if(!p||p->mainFrame.isEmpty()){reply(from,frame,requestId,{},"Receiving end does not exist");return;}
        auto token=QString::number(++sequence);pending[token]={from,frame,requestId,to,p->mainFrame};
        auto source=page(from);p->post({{"kind","runtime"},{"frame",p->mainFrame},{"token",token},{"message",message},{"sender",QJsonObject{{"id","engine-spike-local"},{"url",source->url},{"tab",source->tab()}}}});
        QTimer::singleShot(19000,[this,token]{auto i=pending.find(token);if(i!=pending.end()){auto p=i->second;pending.erase(i);reply(p.tab,p.frame,p.id,{},"Async runtime listener did not respond");}});
    }
    void receive(Page &p,QJsonObject m){
        auto kind=m["kind"].toString(),frame=m["frame"].toString();
        if(kind=="log")return;
        if(kind=="document-ready"){if(m["url"].toString()==p.url)p.mainFrame=frame;log(kind,{{"tab",p.id},{"url",m["url"]},{"frame",frame}});return;}
        if(kind=="bridge-leave"){auto i=peers.find(m["uuid"].toString());if(i!=peers.end()&&i->second.tab==p.id&&i->second.frame==frame)leave(i->first);return;}
        if(kind=="runtime-reply"){auto i=pending.find(m["token"].toString());if(i!=pending.end()&&i->second.receiver==p.id&&i->second.receiverFrame==frame){auto req=i->second;pending.erase(i);reply(req.tab,req.frame,req.id,m["value"]);}return;}
        if(kind!="rpc")return;
        const auto method=m["method"].toString(),id=m["id"].toString();const auto a=m["args"].toArray();
        if(method.size()>128 || id.size()>64 || frame.size()>64 || !m["args"].isArray() || a.size()>8)return;
        auto ok=[&](QJsonValue v=QJsonValue()){reply(p.id,frame,id,v);};auto fail=[&](QString e){reply(p.id,frame,id,{},e);};
        if(method=="practice.ready"&&p.role=="practice"&&a.isEmpty()){p.setProperty("practiceReady",true);ok();return;}
        const bool trusted=p.role=="engine" || p.role=="host" || p.role=="overlay";
        if (!trusted) {
            if (method!="runtime.sendMessage" || a.size()!=1 || !a[0].isObject()) { fail("Capture API denied"); return; }
            auto packet=a[0].toObject();
            if(packet.size()==1&&packet.value("delete").isObject()) {
                // Deletions inherit the authenticated capture page's channel.
                // An SSN message ID only has meaning within this engine lifetime.
                const auto deletion=packet.value("delete").toObject();
                bool validId=false;const auto messageId=deletion.value("id").toVariant().toLongLong(&validId);
                const auto name=deletion.value("chatname").toString();
                if(deletion.contains("id")&&(!validId||messageId<=0)){fail("Invalid comment deletion ID");return;}
                if((!validId||messageId<=0)&&(name.isEmpty()||name.size()>256)){fail("Invalid comment deletion");return;}
                QJsonObject removal{{"removed",true},{"channel",p.channel},{"capture_epoch",captureEpoch}};
                if(validId&&messageId>0){removal["engineId"]=messageId;p.deletedIds.insert(messageId);if(p.deletedIds.size()>4000)p.deletedIds.erase(p.deletedIds.begin());}
                else removal["chatname"]=name;
                if(feed.size()==2000)feed.removeAt(0);feed.append(removal);ok();return;
            }
            if (packet.size()!=1 || (!packet.contains("message") && !packet.contains("getSettings"))) { fail("Capture command denied"); return; }
            if (packet.contains("message")) {
                if (!ready || (p.role=="practice" && !practice)) { fail("Chat is getting ready"); return; }
                const auto input=packet["message"].toObject();
                if (!input["chatmessage"].isString()) { fail("Invalid comment"); return; }
                const auto comment=chatCommentText(input);
                if(input["chatmessage"].toString().size()>8000||comment.size()>8000||input["chatname"].toString().size()>256){++oversizedMessages;fail("Comment exceeds the size limit");return;}
                QJsonObject clean;
                for (const auto &key : {"chatname","hasDonation","membership","event"}) if(input[key].isString()) clean[key]=input[key].toString().left(256);
                if(input["event"].isBool()&&input["event"].toBool())clean["event"]="notice";
                QJsonObject meta;const auto incomingMeta=input["meta"].toObject();
                for(const auto *key:{"tiktokGiftStreakId","giftName"})if(incomingMeta[key].isString()&&incomingMeta[key].toString().size()<=256)meta[key]=incomingMeta[key];
                const auto count=incomingMeta["tiktokGiftCount"].toDouble();if(std::isfinite(count)&&count>0&&count<=1e9)meta["tiktokGiftCount"]=count;
                static const QRegularExpression kickId("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
                if(p.property("platform")=="Kick"&&kickId.match(incomingMeta["messageId"].toString()).hasMatch())meta["messageId"]=incomingMeta["messageId"];
                if(!meta.isEmpty())clean["meta"]=meta;
                clean["chatmessage"]=comment;
                const auto avatar=chatAvatarUrl(input["chatimg"].toString());if(!avatar.isEmpty())clean["chatimg"]=avatar;
                clean["textonly"]=input["textonly"].toBool();
                const auto practiceType=input["type"].toString("twitch");
                clean["type"]=p.role=="practice"?(QStringList{"twitch","youtube","kick","tiktok","facebook","instagram","x","rumble"}.contains(practiceType)?practiceType:QString("twitch")):p.property("platform").toString().toLower();
                clean["tid"]=p.id;
                packet["message"]=clean;
            }
            for(auto &[tab,w]:pages)if(w->role=="engine"){route(p.id,frame,id,tab,packet);return;}
            fail("Engine unavailable"); return;
        }
        if (method.startsWith("bridge.") && p.role!="engine" && p.role!="host" && p.role!="overlay") { fail("Bridge denied"); return; }
        if(method=="host.comment" && p.role=="host") {
            auto payload=a.at(0).toObject();
            if(payload["chatmessage"].isString() && payload["id"].isDouble()) {
                auto origin=page(payload["tid"].toInt());
                if(!origin){fail("Unknown capture");return;}
                if(origin->deletedIds.contains(payload["id"].toInteger())){ok();return;}
                payload["capture_epoch"]=captureEpoch;
                const auto avatar=chatAvatarUrl(payload["chatimg"].toString());
                if(avatar.isEmpty())payload.remove("chatimg");else payload["chatimg"]=avatar;
                payload["channel"]=origin->channel;
                payload["practice"]=origin->role=="practice";
                states[origin->channel]="Reading comments";lastComment[origin->channel]=QDateTime::currentMSecsSinceEpoch();
                if(feed.size()==2000)feed.removeAt(0);feed.append(payload);
            }
            ok();return;
        }
        if(method.startsWith("storage.")){
            auto parts=method.split('.');if(parts.size()!=3 || (parts[1]!="local"&&parts[1]!="sync")){fail("Invalid storage request");return;}auto area=parts[1],op=parts[2];auto data=storage[area].toObject();
            if(op=="get"){
                QJsonObject result;auto key=a.at(0);
                if(key.isNull()||key.isUndefined())result=data;
                else if(key.isObject()){result=key.toObject();for(auto it=result.begin();it!=result.end();++it)if(data.contains(it.key()))it.value()=data[it.key()];}
                else {auto keys=key.isArray()?key.toArray():QJsonArray{key};for(auto k:keys)if(data.contains(k.toString()))result[k.toString()]=data[k.toString()];}ok(result);return;
            }
            if(op=="getBytesInUse"){ok(int(json(data).size()));return;}
            QJsonObject changes;
            if(op=="set"){auto incoming=a.at(0).toObject();for(auto it=incoming.begin();it!=incoming.end();++it){if(data[it.key()]!=it.value()){changes[it.key()]=QJsonObject{{"oldValue",data[it.key()]},{"newValue",it.value()}};data[it.key()]=it.value();}}}
            else if(op=="remove"||op=="clear"){QJsonArray keys;if(op=="clear")for(auto k:data.keys())keys.append(k);else keys=a.at(0).isArray()?a.at(0).toArray():QJsonArray{a.at(0)};for(auto k:keys)if(data.contains(k.toString())){changes[k.toString()]=QJsonObject{{"oldValue",data[k.toString()]}};data.remove(k.toString());}}
            else {fail("Unsupported storage operation");return;}
            storage[area]=data;ok();if(!changes.isEmpty())broadcast("storage.onChanged",{changes,area});return;
        }
        if(method=="runtime.sendMessage"){
            if(p.role=="engine"){for(auto &[tab,w]:pages)if(tab!=p.id&&!w->mainFrame.isEmpty())w->post({{"kind","runtime"},{"frame",w->mainFrame},{"token","broadcast"},{"message",a.at(0)},{"sender",QJsonObject{{"url",p.url},{"tab",p.tab()}}}});ok();return;}
            for(auto &[tab,w]:pages)if(w->role=="engine"){route(p.id,frame,id,tab,a.at(0));return;}fail("Engine unavailable");return;
        }
        if(method=="tabs.query"){QJsonArray result;auto q=a.at(0).toObject();for(auto &[tab,w]:pages){if(q.contains("active")&&q["active"].toBool()!=w->active)continue;if(q.contains("url")){auto patterns=q["url"].isArray()?q["url"].toArray():QJsonArray{q["url"]};bool match=false;for(auto pattern:patterns)match|=QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern.toString())).match(w->url).hasMatch();if(!match)continue;}result.append(w->tab());}ok(result);return;}
        if(method=="tabs.get"||method=="tabs.getCurrent"){auto target=method=="tabs.getCurrent"?&p:page(a.at(0).toInt());if(target)ok(target->tab());else fail("Unknown tab");return;}
        if(method=="tabs.sendMessage"){route(p.id,frame,id,a.at(0).toInt(),a.at(1));return;}
        if(method=="tabs.update" || method=="tabs.create" || method=="tabs.remove" || method.startsWith("debugger.")) { fail("Input and page control are disabled"); return; }
        if(method=="bridge.join"){
            auto d=a.at(0).toObject();auto uuid=d["uuid"].toString();if(m["frameOrigin"].toString()!="https://vdo.socialstream.ninja" || QUrl(m["url"].toString()).host()!="vdo.socialstream.ninja"){fail("Bridge origin required");return;}
            leave(uuid);peers[uuid]={p.id,frame,d};if(p.role=="overlay")startupTiming["overlay_handshake_ms"]=QDateTime::currentMSecsSinceEpoch()-startedAt;ok();
            // Join reply precedes peer discovery; all events still travel via the real iframe.
            for(auto &[other,peer]:peers)if(other!=uuid){if(views(peers[uuid],peer))connect(uuid,other);if(views(peer,peers[uuid]))connect(other,uuid);}return;
        }
        if(method=="bridge.send"){
            auto uuid=a.at(0).toString();auto i=peers.find(uuid);if(i==peers.end()||i->second.tab!=p.id||i->second.frame!=frame){fail("Unknown bridge sender");return;}
            auto packet=a.at(1).toObject();auto type=packet["type"].toString(),target=packet["UUID"].toString();int count=0;
            for(auto &[other,dest]:peers){if(other==uuid||(!target.isEmpty()&&target!=other))continue;bool edge=type=="pcs"?views(dest,i->second):type=="rpcs"?views(i->second,dest):false;if(!edge)continue;
                deliver(other,{{"dataReceived",packet["sendData"]},{"UUID",uuid}});count++;

            }ok(count);return;
        }
        if(method.startsWith("action.")||method.startsWith("notifications.")||method.startsWith("windows.")){log("api-noop",{{"name",method}});ok(QJsonObject{{"id",1}});return;}
        log("missing-api",{{"name",method},{"tab",p.id}});fail("Unsupported API: "+method);
    }
};
void Host::reloadCaptures(const QString &platform) {
    for(auto &[id,p]:pages)if(p->role=="capture"&&p->web&&(platform.isEmpty()||p->property("platform").toString()==platform)) {
        p->terminal=false;p->failures=0;p->retrySeconds=60;p->retryAt=0;p->probeAt=0;
        states[p->channel]="Checking chat after account change";
        p->web->Navigate(p->property("captureUrl").toString().toStdWString().c_str());
    }
}
void Host::finishSignIn(int id) {
    auto p=page(id);if(!p||p->role!="signin")return;
    const auto platform=p->property("platform").toString();pages.erase(id);
    accountNotice="Chat rechecked. If sign-in is still requested, reopen Accounts.";
    reloadCaptures(platform);
}
void Host::clearAccountData(Page *holder) {
    // Keep one inert view alive while WebView2 clears its shared profile. Closing
    // the last view first invalidates the asynchronous profile operation.
    ComPtr<ICoreWebView2Profile2> profile;ComPtr<ICoreWebView2_13> view;holder->web.As(&view);
    if(view){ComPtr<ICoreWebView2Profile> base;view->get_Profile(&base);if(base)base.As(&profile);}
    QPointer<QTimer> owner(&timer);const int tab=holder->id;
    const auto finish=[this,owner,tab](HRESULT result){
        if(!owner)return;clearingAccounts=false;
        accountNotice=SUCCEEDED(result)?"Signed out of chat accounts on this device.":"Could not clear account sessions. Try Accounts again.";
        QTimer::singleShot(0,&timer,[this,tab]{pages.erase(tab);configureChannels({{"channels",channels},{"practice",practice}});});
    };
    if(!profile){finish(E_NOINTERFACE);return;}
    const auto kinds=static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES|COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_DOM_STORAGE);
    const auto hr=profile->ClearBrowsingData(kinds,Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>([finish](HRESULT result)->HRESULT {finish(result);return S_OK;}).Get());
    if(FAILED(hr))finish(hr);
}
void Page::closeEvent(QCloseEvent *event) {
    event->accept();hide();
    if(role=="signin") {auto *owner=&host;const int tab=id;QTimer::singleShot(0,&host.timer,[owner,tab]{owner->finishSignIn(tab);});}
}
void Page::configureSignIn() {
    // A normal platform window sharing only the capture browser profile. No
    // extension shim, host bridge, script injection or credential inspection.
    RECT bounds{0,0,browserArea->width(),browserArea->height()};controller->put_Bounds(bounds);controller->put_IsVisible(TRUE);
    ComPtr<ICoreWebView2Settings> settings;web->get_Settings(&settings);settings->put_AreHostObjectsAllowed(FALSE);settings->put_IsWebMessageEnabled(FALSE);settings->put_AreDevToolsEnabled(FALSE);
    ComPtr<ICoreWebView2Settings4> autofill;settings.As(&autofill);if(autofill){autofill->put_IsPasswordAutosaveEnabled(FALSE);autofill->put_IsGeneralAutofillEnabled(FALSE);}
    ComPtr<ICoreWebView2_8> audio;web.As(&audio);if(audio)audio->put_IsMuted(TRUE);
    EventRegistrationToken token{};
    const auto navigate=[this](const QString &target){
        if(chatSignInAllowed(property("platform").toString(),QUrl(target)))return true;
        setProperty("signInBlocked",true);
        accountHelp->setText("This sign-in provider is not supported in Studio. Choose Reload and use the platform's email, username or QR code option.");
        return false;
    };
    web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this,navigate](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs *args)->HRESULT {
        LPWSTR raw=nullptr;args->get_Uri(&raw);const auto target=take(raw);
        if(!navigate(target)){args->put_Cancel(TRUE);return S_OK;}
        args->get_NavigationId(&accountNavigation);
        setProperty("signInBlocked",false);accountHelp->setText(property("defaultAccountHelp").toString());url=target;accountLocation->setText("https://"+QUrl(target).host());return S_OK;
    }).Get(),&token);
    web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs *args)->HRESULT {
        UINT64 navigation=0;args->get_NavigationId(&navigation);
        if(navigation!=accountNavigation||property("signInBlocked").toBool())return S_OK;
        BOOL success=FALSE;args->get_IsSuccess(&success);
        COREWEBVIEW2_WEB_ERROR_STATUS reason{};args->get_WebErrorStatus(&reason);
        // Redirects and replacement navigations can cancel an earlier request.
        // Their completion must not overwrite the state of the current page.
        if(success)accountHelp->setText(property("defaultAccountHelp").toString());
        else if(reason!=COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)accountHelp->setText("The sign-in page could not load. Use Reload, or Return to chat and try Accounts again.");
        return S_OK;
    }).Get(),&token);
    web->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>([this](ICoreWebView2*,IUnknown*)->HRESULT {BOOL available=FALSE;web->get_CanGoBack(&available);accountBack->setEnabled(available);return S_OK;}).Get(),&token);
    web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([this,navigate](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs *args)->HRESULT {
        args->put_Handled(TRUE);LPWSTR raw=nullptr;args->get_Uri(&raw);const auto target=take(raw);
        if(navigate(target))web->Navigate(target.toStdWString().c_str());return S_OK;
    }).Get(),&token);
    web->add_WindowCloseRequested(Callback<ICoreWebView2WindowCloseRequestedEventHandler>([this](ICoreWebView2*,IUnknown*)->HRESULT {close();return S_OK;}).Get(),&token);
    web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs *args)->HRESULT {args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&token);
    ComPtr<ICoreWebView2_4> downloads;web.As(&downloads);if(downloads)downloads->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs *args)->HRESULT {args->put_Cancel(TRUE);return S_OK;}).Get(),&token);
    web->Navigate(url.toStdWString().c_str());
}
void Page::start(){
    ComPtr<ICoreWebView2Environment10> env; host.environment.As(&env);
    if(!env){host.error="Chat needs a newer Microsoft Edge WebView2 Runtime.";return;}
    ComPtr<ICoreWebView2ControllerOptions> options;env->CreateCoreWebView2ControllerOptions(&options);
    options->put_ProfileName(role=="capture"||role=="signin"||role=="account-clear"?L"capture":L"engine");
    QPointer<Page> self(this);
    const auto completed=[self](HRESULT hr,ICoreWebView2Controller*c)->HRESULT{
        if(!self)return S_OK;
        if(FAILED(hr)||!c) {
            if(self->role=="account-clear") {auto *owner=&self->host;const int tab=self->id;owner->clearingAccounts=false;owner->accountNotice="Could not clear account sessions. Try Accounts again.";QTimer::singleShot(0,&owner->timer,[owner,tab]{owner->pages.erase(tab);owner->configureChannels({{"channels",owner->channels},{"practice",owner->practice}});});}
            else if(self->role=="signin")self->accountHelp->setText("The sign-in window could not start. Return to chat, then try Accounts again.");
            else self->host.check(hr,"controller",self->id);
            return S_OK;
        }
        self->controller=c;c->get_CoreWebView2(&self->web);self->configure();return S_OK;
    };
    const auto result=env->CreateCoreWebView2ControllerWithOptions(reinterpret_cast<HWND>(browserArea?browserArea->winId():winId()),options.Get(),Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(completed).Get());
    if(!host.check(result,"create-controller",id))completed(result,nullptr);
}
void Page::receiveNative(ICoreWebView2WebMessageReceivedEventArgs *a,ICoreWebView2Frame *sender) {
    if(!messageClock.isValid()||messageClock.elapsed()>1000){messageClock.restart();messageCount=0;}
    if(++messageCount>(role=="capture"?400:2000))return;
    LPWSTR raw=nullptr;a->get_Source(&raw);const auto origin=take(raw);
    if(role=="capture") {if(!chatCaptureChannelMatches(QUrl(property("captureUrl").toString()),QUrl(origin))||!chatCaptureAllowed(host.manifest,QUrl(url),QUrl(origin),sender!=nullptr))return;}
    else if(origin!=url)return;
    a->get_WebMessageAsJson(&raw);const size_t length=raw?wcsnlen_s(raw,32769):0;
    if(length>32768){if(role=="capture"||role=="practice")++host.oversizedMessages;CoTaskMemFree(raw);return;}
    auto m=QJsonDocument::fromJson(take(raw).toUtf8()).object();
    if(role=="capture") {
        // Source is supplied by WebView2, not by the page's postMessage payload.
        m["url"]=origin;m["frameOrigin"]=QUrl(origin).adjusted(QUrl::RemovePath|QUrl::RemoveQuery|QUrl::RemoveFragment).toString();
        const auto key=m["frame"].toString();if(key.isEmpty()||key.size()>64)return;
        if(sender){ComPtr<ICoreWebView2Frame2> f;if(FAILED(sender->QueryInterface(IID_PPV_ARGS(&f))))return;
            auto it=captureFrames.find(key);if(key==mainFrame||(it!=captureFrames.end()&&it->second.Get()!=f.Get()))return;captureFrames[key]=f;
            if(m["kind"]=="document-ready")return;
        }else if(captureFrames.contains(key))return;
    }
    host.receive(*this,m);
}
void Page::configure(){
    if(role=="account-clear"){controller->put_IsVisible(FALSE);host.clearAccountData(this);return;}
    if(role=="signin"){configureSignIn();return;}
    // Capture documents need animation frames for platform rendering and DOM
    // cleanup. Hiding the controller suspends those frames even when the SSN
    // source reports document.visibilityState as visible. The owning QWidget
    // remains hidden; this does not open a capture window on the desktop.
    // Kick flushes busy chat in batches into a virtual list. Render enough of
    // that list to retain a whole burst. Set the browser bounds directly: Windows
    // clamps a hidden top-level QWidget to the desktop's available height.
    const int captureHeight=role=="capture"&&QUrl(url).host()=="kick.com"?8192:height();
    RECT bounds{0,0,width(),captureHeight};controller->put_Bounds(bounds);controller->put_IsVisible(role=="overlay" || role=="capture" ? TRUE : FALSE);
    if(role=="overlay"){ComPtr<ICoreWebView2Controller2> c2;controller.As(&c2);if(c2)c2->put_DefaultBackgroundColor({0,0,0,0});}
    UINT32 pid=0;web->get_BrowserProcessId(&pid);host.browserPid=pid;host.log("controller-ready",{{"tab",id},{"role",role},{"browserPid",int(pid)}});
    ComPtr<ICoreWebView2_3>w3;web.As(&w3);if(!w3||!host.check(w3->SetVirtualHostNameToFolderMapping(L"socialstream.ninja",host.root.toStdWString().c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW),"mapping",id))return;
    ComPtr<ICoreWebView2Settings>settings;web->get_Settings(&settings);settings->put_AreDefaultScriptDialogsEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);
    EventRegistrationToken token{};
    web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*a)->HRESULT{
        receiveNative(a);return S_OK;
    }).Get(),&token);
    if(role=="capture") {
        ComPtr<ICoreWebView2_4> framed;web.As(&framed);QPointer<Page> self(this);
        if(framed)framed->add_FrameCreated(Callback<ICoreWebView2FrameCreatedEventHandler>([self](ICoreWebView2*,ICoreWebView2FrameCreatedEventArgs *args)->HRESULT{
            if(!self)return S_OK;ComPtr<ICoreWebView2Frame> frame;args->get_Frame(&frame);ComPtr<ICoreWebView2Frame2> f;frame.As(&f);if(!f)return S_OK;
            EventRegistrationToken token{};
            f->add_NavigationStarting(Callback<ICoreWebView2FrameNavigationStartingEventHandler>([self](ICoreWebView2Frame*,ICoreWebView2NavigationStartingEventArgs *a)->HRESULT{
                if(!self)return S_OK;LPWSTR raw=nullptr;a->get_Uri(&raw);const auto target=take(raw);
                if(target!="about:blank"&&!chatCaptureAllowed(self->host.manifest,QUrl(self->url),QUrl(target),true))a->put_Cancel(TRUE);
                return S_OK;
            }).Get(),&token);
            f->add_WebMessageReceived(Callback<ICoreWebView2FrameWebMessageReceivedEventHandler>([self](ICoreWebView2Frame *sender,ICoreWebView2WebMessageReceivedEventArgs *a)->HRESULT{if(self)self->receiveNative(a,sender);return S_OK;}).Get(),&token);
            f->add_Destroyed(Callback<ICoreWebView2FrameDestroyedEventHandler>([self](ICoreWebView2Frame *sender,IUnknown*)->HRESULT{if(self)std::erase_if(self->captureFrames,[&](const auto &v){return static_cast<ICoreWebView2Frame*>(v.second.Get())==sender;});return S_OK;}).Get(),&token);
            return S_OK;
        }).Get(),&token);
    }
    web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*a)->HRESULT{LPWSTR raw=nullptr;a->get_Uri(&raw);auto target=take(raw);QUrl next(target),old(url);
        bool allowed=next.scheme()=="https"&&next.userInfo().isEmpty()&&next.port()==-1;
        if(role=="capture") {
            auto normalize=[](QString h){if(h.startsWith("www."))h=h.mid(4);if(h=="twitter.com")h="x.com";return h;};
            const QUrl original(property("captureUrl").toString());
            // A raid must not silently replace a saved channel with its target.
            if(!chatCaptureChannelMatches(original,next)){
                if(next.path().contains("login"))host.states[channel]="This channel needs sign-in. Open Accounts to continue.";
                a->put_Cancel(TRUE);return S_OK;
            }
            allowed &= normalize(next.host())==normalize(original.host());
            bool path=false;for(const auto &v:host.manifest["content_scripts"].toArray())path|=chatScriptMatches(v.toObject(),next);
            // A resolver may visit its own channel or a watch page before opening chat.
            allowed &= path || (next.host()=="www.youtube.com"&&(next.path()=="/watch"||next.path()==QUrl(property("resolve").toString()).path()));
            if(!allowed)host.states[channel]=next.path().contains("login")||next.host()=="accounts.google.com"?"This channel needs sign-in. Open Accounts to continue.":"Chat could not open that page. Check the channel link.";
        }else allowed &= next.host()==old.host();
        if(!allowed){a->put_Cancel(TRUE);return S_OK;}
        url=target;captureFrames.clear();setProperty("practiceReady",false);host.leaveTab(id);mainFrame.clear();host.log("navigation",{{"tab",id},{"url",url}});return S_OK;}).Get(),&token);
    web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*a)->HRESULT{
        BOOL success=FALSE;a->get_IsSuccess(&success);if(role=="overlay"&&success)host.startupTiming["overlay_load_ms"]=QDateTime::currentMSecsSinceEpoch()-host.startedAt;
        if(role=="capture") {
            ComPtr<ICoreWebView2NavigationCompletedEventArgs2> http;a->QueryInterface(IID_PPV_ARGS(&http));int code=0;if(http)http->get_HttpStatusCode(&code);
            if(code==404){terminal=true;host.states[channel]="We could not find that channel. Check the link.";}
            else if(success){failures=0;host.states[channel]=property("resolve").toString().isEmpty()||url.contains("/live_chat")?"Waiting for comments":"Waiting for your stream";probeAt=QDateTime::currentMSecsSinceEpoch()+1500;}
            else if(!terminal){host.states[channel]="Comments paused. Reconnecting.";QPointer<Page> self(this);QTimer::singleShot(std::min(60000,2000<<std::min(failures++,5)),this,[self]{if(self&&self->web&&!self->terminal)self->web->Reload();});}
        }
        host.broadcast("tabs.onUpdated",{id,QJsonObject{{"status",success?"complete":"error"}},tab()});return S_OK;
    }).Get(),&token);
    web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*a)->HRESULT{COREWEBVIEW2_PROCESS_FAILED_KIND kind;a->get_ProcessFailedKind(&kind);QJsonObject d{{"tab",id},{"kind",int(kind)}};ComPtr<ICoreWebView2ProcessFailedEventArgs2>a2;if(SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a2)))){INT32 code=0;COREWEBVIEW2_PROCESS_FAILED_REASON reason;a2->get_ExitCode(&code);a2->get_Reason(&reason);d["exitCode"]=QString::number(quint32(code),16);d["reason"]=int(reason);}host.log("process-failed",d);host.states[channel]="Comments paused. Reconnecting.";
        if(role=="engine")host.ready=false;
        if(role=="overlay"){host.pendingToken.clear();host.renderedToken.clear();}
        if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED){QCoreApplication::exit(3);return S_OK;}
        QPointer<Page> self(this);QTimer::singleShot(2000,this,[self]{if(self&&self->web)self->web->Reload();});return S_OK;}).Get(),&token);
    web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs*a)->HRESULT{a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&token);
    web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([this](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*a)->HRESULT{a->put_Handled(TRUE);host.log("popup-denied",{{"tab",id}});return S_OK;}).Get(),&token);
    ComPtr<ICoreWebView2_8> w8;web.As(&w8);if(w8)w8->put_IsMuted(TRUE);
    ComPtr<ICoreWebView2_4> w4;web.As(&w4);if(w4)w4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs*a)->HRESULT{a->put_Cancel(TRUE);return S_OK;}).Get(),&token);
    ComPtr<ICoreWebView2_22>w22;web.As(&w22);if(w22)w22->AddWebResourceRequestedFilterWithRequestSourceKinds(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL);else web->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs*a)->HRESULT{
        ComPtr<ICoreWebView2WebResourceRequest>req;a->get_Request(&req);LPWSTR raw=nullptr;req->get_Uri(&raw);auto uri=take(raw);QUrl u(uri);COREWEBVIEW2_WEB_RESOURCE_CONTEXT ctx;a->get_ResourceContext(&ctx);
        bool vdo=u.host()=="vdo.socialstream.ninja";bool local=u.host()=="socialstream.ninja"||u.host()=="studio-chat.invalid";
        bool blocked=u.host()=="io.socialstream.ninja"||u.host().endsWith(".vdo.ninja")||u.host()=="vdo.ninja"||vdo;
        if(role=="capture"&&ctx==COREWEBVIEW2_WEB_RESOURCE_CONTEXT_IMAGE&&!chatAvatarUrl(uri).isEmpty()) {
            // A failed image makes YouTube replace its public avatar URL with a
            // placeholder. Satisfy the hidden page with one transparent pixel;
            // only visible native rows download the actual profile picture.
            static const auto pixel=QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNgYGBgAAAABQABpfZFQAAAAABJRU5ErkJggg==");
            ComPtr<IStream>stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(pixel.constData()),UINT(pixel.size())));
            ComPtr<ICoreWebView2WebResourceResponse>response;
            host.environment->CreateWebResourceResponse(stream.Get(),200,L"OK",L"Content-Type: image/png\r\nCache-Control: no-store",&response);
            a->put_Response(response.Get());return S_OK;
        }
        if(role=="capture") {
            blocked |= ctx==COREWEBVIEW2_WEB_RESOURCE_CONTEXT_IMAGE||ctx==COREWEBVIEW2_WEB_RESOURCE_CONTEXT_MEDIA||ctx==COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FONT;
            for(const auto &domain:QStringList{"googlevideo.com","amazon-adsystem.com","scorecardresearch.com","doubleclick.net","googlesyndication.com","google-analytics.com","adsrvr.org","adnxs.com"})blocked|=u.host()==domain||u.host().endsWith('.'+domain);
        }
        QString asset;if(vdo&&(role=="engine"||role=="host"||role=="overlay")&&ctx==COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT)asset=":/mixer/chat/bridge.html";
        if(u.host()=="studio-chat.invalid") {if(u.path()=="/host.html")asset=":/mixer/chat/host.html";else if(u.path()=="/test-source.html")asset=":/mixer/chat/practice.html";else blocked=true;}
        host.log("request",{{"tab",id},{"role",role},{"url",uri},{"host",u.host()},{"context",int(ctx)},{"disposition",!asset.isEmpty()?"intercepted":blocked?"blocked":local?"mapped-local":"network"}});
        if(!asset.isEmpty()||blocked){auto data=asset.isEmpty()?QByteArray("Cloud relay blocked by engine-spike"):read(asset);ComPtr<IStream>stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(data.constData()),UINT(data.size())));ComPtr<ICoreWebView2WebResourceResponse>resp;host.environment->CreateWebResourceResponse(stream.Get(),asset.isEmpty()?403:200,asset.isEmpty()?L"Blocked":L"OK",L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store",&resp);a->put_Response(resp.Get());}return S_OK;
    }).Get(),&token);
    // Browser enforcement supplements the main-world WebSocket guard, including workers.
    // This session exists only to enforce the relay block list. Chromium's
    // default inspector buffers retain response bodies we never read (including
    // long-running chat polls). Keep enforcement, without a second body cache.
    web->CallDevToolsProtocolMethod(L"Network.enable",L"{\"maxTotalBufferSize\":0,\"maxResourceBufferSize\":0}",nullptr);
    web->CallDevToolsProtocolMethod(L"Network.setBlockedURLs",L"{\"urls\":[\"wss://*.socialstream.ninja/*\",\"ws://*.socialstream.ninja/*\",\"*://*.vdo.ninja/*\",\"*://vdo.ninja/*\"]}",nullptr);
    QJsonObject cfg{{"tab",id},{"role",role},{"manifest",host.manifest}};
    auto script=host.shim;script.replace("__ENGINE_CONFIG__",QString::fromUtf8(json(cfg)));
    if(role=="capture") {
        script+=host.captureBundle;
        QUrl capture(url);if(capture.host()=="www.youtube.com")capture=QUrl("https://www.youtube.com/live_chat?is_popout=1&v=abcdefghijk");
        const QString sourceParameters=capture.host()=="kick.com"?"fetch":"";
        for(const auto &v:host.manifest["content_scripts"].toArray()) {
            const auto entry=v.toObject();if(!chatScriptMatches(entry,capture)&&!chatScriptMatches(entry,QUrl(url)))continue;
            script+="\nwindow.__chatSpike.register("+QString::fromUtf8(json(entry))+",function("+sourceParameters+"){\n";
            for(const auto &file:entry["js"].toArray())script+=QString::fromUtf8(read(host.root+"/"+file.toString()))+"\n;\n";
            script+="\n});\n";
        }
    }
    QPointer<Page> self(this);
    host.check(web->AddScriptToExecuteOnDocumentCreated(script.toStdWString().c_str(),Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([self](HRESULT hr,LPCWSTR)->HRESULT{if(self&&self->web&&self->host.check(hr,"document-injection",self->id))self->host.check(self->web->Navigate(self->url.toStdWString().c_str()),"navigate",self->id);return S_OK;}).Get()),"add-script",id);
}
void Page::snapshot(QString token) {
    if (!web || busy) return;
    ComPtr<IStream> stream; if (FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))) return;
    busy=true;setProperty("snapshotAt",QDateTime::currentMSecsSinceEpoch()); QPointer<Page> self(this);
    const auto result=web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),
        Callback<ICoreWebView2CapturePreviewCompletedHandler>([self,stream,token](HRESULT hr)->HRESULT {
            if(!self)return S_OK; self->busy=false;
            if(FAILED(hr) || (self->host.pendingToken!=token && self->host.renderedToken!=token))return S_OK;
            STATSTG stat{};HGLOBAL buffer=nullptr;
            if(FAILED(stream->Stat(&stat,STATFLAG_NONAME)) || stat.cbSize.QuadPart>32*1024*1024 || FAILED(GetHGlobalFromStream(stream.Get(),&buffer)))return S_OK;
            auto bytes=static_cast<const uchar*>(GlobalLock(buffer));if(!bytes)return S_OK;
            QElapsedTimer decode;decode.start();auto image=QImage::fromData(bytes,int(stat.cbSize.QuadPart),"PNG");if(self->host.pendingToken==token)self->host.timing["png_decode_ms"]=decode.elapsed();GlobalUnlock(buffer);
            if(!image.isNull()) {if(self->host.pendingToken==token){self->host.timing["snapshot_ms"]=QDateTime::currentMSecsSinceEpoch()-self->property("snapshotAt").toLongLong();self->host.timing["worker_show_to_frame_ms"]=QDateTime::currentMSecsSinceEpoch()-self->host.requestedAt;}self->host.frame(image,token);self->host.renderedToken=token;if(self->host.pendingToken==token)self->host.pendingToken.clear();}
            return S_OK;
        }).Get());
    if(FAILED(result))busy=false;
}
void Host::setup(const QJsonObject &c) {
    startedAt=QDateTime::currentMSecsSinceEpoch();
    root=QCoreApplication::applicationDirPath()+"/ssn";
    if(!verifyChatPack(root))throw std::runtime_error("Invalid chat pack");
    profile=studioDataPath()+"/chat";QDir().mkpath(profile);
    profileOwner=std::make_unique<QLockFile>(profile+"/studio-profile.lock");profileOwner->setStaleLockTime(0);
    if(!profileOwner->tryLock(0)){error="Chat is open in another Studio window. Close that window and restart this one to use chat here.";return;}
    session=c["room"].toString();
    if(!QRegularExpression("^[a-zA-Z0-9]{16,64}$").match(session).hasMatch())throw std::runtime_error("Invalid chat identity");
    manifest=QJsonDocument::fromJson(read(root+"/manifest.json")).object();
    shim=QString::fromUtf8(read(":/mixer/chat/chrome-shim.js"));
    captureBundle=QString::fromUtf8(read(":/mixer/chat/capture.js"));
    storage={{"sync",QJsonObject{{"streamID",session},{"password",false},{"state",true}}},{"local",QJsonObject{}}};
    channels=c["channels"].toArray();practice=c["practice"].toBool();
    auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    // This worker reads chat DOM and renders occasional static comment cards.
    // Software rendering avoids a separate GPU texture/cache allocation for
    // these hidden pages. The video compositor and browser sources use their
    // own environments and retain hardware acceleration.
    options->put_AdditionalBrowserArguments(L"--disable-gpu --disable-background-timer-throttling --disable-backgrounding-occluded-windows --disable-renderer-backgrounding --host-resolver-rules=\"MAP io.socialstream.ninja 0.0.0.0, MAP vdo.socialstream.ninja 0.0.0.0, MAP *.vdo.ninja 0.0.0.0, MAP vdo.ninja 0.0.0.0\"");
    check(CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.toStdWString().c_str(),options.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this](HRESULT hr,ICoreWebView2Environment *e)->HRESULT {
        if(!check(hr,"environment")||!e)return S_OK;environment=e;
        open("host","https://studio-chat.invalid/host.html?session="+session);
        open("engine","https://socialstream.ninja/background.html");
        const auto overlay=open("overlay","https://socialstream.ninja/featured.html?session="+session+"&password=false&noavatar&hidesource&notime&transition=none");
        page(overlay)->resize(1280,720);
        configureChannels({{"channels",channels},{"practice",practice}});
        return S_OK;
    }).Get()),"environment");
    QObject::connect(&timer,&QTimer::timeout,&timer,[this]{tick();});timer.start(33);
    // Studio cards contain plain text and no animation. Keep the acknowledged
    // image until the next show/style/gift token instead of encoding and
    // decoding another full PNG fifteen times a second while a card is held.
}
void Host::configureChannels(const QJsonObject &c) {
    channels=c["channels"].toArray();practice=c["practice"].toBool();
    if(!environment||!ready||clearingAccounts)return;
    QSet<QString> wanted;for(const auto &v:channels)wanted.insert(v.toObject()["id"].toString());if(practice)wanted.insert("practice");
    std::vector<int> remove;for(auto &[id,p]:pages)if((p->role=="capture"||p->role=="practice"||p->role=="signin")&&!wanted.contains(p->channel))remove.push_back(id);
    for(int id:remove){states.remove(page(id)->channel);closePage(id);}
    QJsonArray incoming=channels;if(practice)incoming.append(QJsonObject{{"id","practice"},{"url","https://studio-chat.invalid/test-source.html"},{"platform","Twitch"}});
    for(const auto &v:incoming) {
        const auto ch=v.toObject();const auto key=ch["id"].toString();bool found=false;for(auto &[id,p]:pages)found|=(p->role=="capture"||p->role=="practice")&&p->channel==key;
        if(found)continue;
        auto id=open(key=="practice"?"practice":"capture",ch["url"].toString());auto p=page(id);p->channel=key;
        p->setProperty("captureUrl",ch["url"].toString());p->retryAt=QDateTime::currentMSecsSinceEpoch()+60000;
        p->setProperty("platform",ch["platform"].toString());p->setProperty("resolve",ch["resolve"].toString());
        states[key]=ch["resolve"].toString().isEmpty()?"Connecting to chat":"Waiting for your stream";
    }
}
void Host::sampleResources() {
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if(!resourceDiagnostics||now-resourceDiagnosticAt<30000)return;
    resourceDiagnosticAt=now;
    // Opt-in diagnostics for long recordings. These approximate JS/DOM counts
    // and normal WebView2 process/frame metadata contain no message bodies,
    // account storage, full URLs, response bodies or browser debugging session.
    if(clearingAccounts||std::any_of(pages.begin(),pages.end(),[](const auto &entry){return entry.second->role=="signin";}))return;
    for(auto &[id,p]:pages) {
        if(!p->web||p->role=="signin"||p->role=="account-clear"||p->property("resourcePending").toBool())continue;
        p->setProperty("resourcePending",true);
        QPointer<Page> self(p.get());
        const auto hr=p->web->ExecuteScript(L"({viewportHeight:innerHeight,chatHeight:document.getElementById('chatroom-messages')?.clientHeight||0,chatRows:document.querySelectorAll('#chatroom-messages [data-index]').length,nodes:document.getElementsByTagName('*').length,heapUsed:performance.memory?.usedJSHeapSize||0,heapTotal:performance.memory?.totalJSHeapSize||0,resources:performance.getEntriesByType('resource').length,bridge:window.__engineSpike?.stats?.()})",
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>([self](HRESULT hr,LPCWSTR result)->HRESULT{
                if(!self)return S_OK;self->setProperty("resourcePending",false);
                if(SUCCEEDED(hr)&&result){
                    const auto data=QJsonDocument::fromJson(QString::fromWCharArray(result).toUtf8()).object();QJsonObject safe,bridge;
                    for(const auto *key:{"viewportHeight","chatHeight","chatRows","nodes","heapUsed","heapTotal","resources"})if(data[key].isDouble())safe[key]=data[key];
                    const auto rawBridge=data["bridge"].toObject();for(const auto *key:{"pending","events","children"})if(rawBridge[key].isDouble())bridge[key]=rawBridge[key];
                    safe["bridge"]=bridge;safe["sampledAt"]=QDateTime::currentMSecsSinceEpoch();self->setProperty("resourceStats",safe);
                }
                return S_OK;
            }).Get());
        if(FAILED(hr))p->setProperty("resourcePending",false);
    }
    ComPtr<ICoreWebView2Environment13> e13;if(!environment||FAILED(environment.As(&e13))||resourceDiagnosticPending)return;
    resourceDiagnosticPending=true;
    QPointer<QTimer> alive(&timer);
    const auto hr=e13->GetProcessExtendedInfos(Callback<ICoreWebView2GetProcessExtendedInfosCompletedHandler>(
        [this,alive](HRESULT hr,ICoreWebView2ProcessExtendedInfoCollection *all)->HRESULT{
            if(!alive)return S_OK;
            resourceDiagnosticPending=false;if(FAILED(hr)||!all)return S_OK;
            QJsonArray rows;UINT32 count=0;all->get_Count(&count);
            for(UINT32 i=0;i<std::min(count,UINT32(128));++i){
                ComPtr<ICoreWebView2ProcessExtendedInfo> extended;ComPtr<ICoreWebView2ProcessInfo> info;
                if(FAILED(all->GetValueAtIndex(i,&extended))||!extended||FAILED(extended->get_ProcessInfo(&info))||!info)continue;
                INT32 pid=0;COREWEBVIEW2_PROCESS_KIND kind;info->get_ProcessId(&pid);info->get_Kind(&kind);
                QJsonArray frames;ComPtr<ICoreWebView2FrameInfoCollection> collection;
                if(SUCCEEDED(extended->get_AssociatedFrameInfos(&collection))&&collection){
                    ComPtr<ICoreWebView2FrameInfoCollectionIterator> iterator;collection->GetIterator(&iterator);BOOL current=FALSE;
                    while(iterator&&frames.size()<64&&SUCCEEDED(iterator->get_HasCurrent(&current))&&current){
                        ComPtr<ICoreWebView2FrameInfo> info;if(FAILED(iterator->GetCurrent(&info))||!info)break;LPWSTR raw=nullptr;info->get_Source(&raw);const QUrl source(take(raw));
                        // Resource diagnostics never retain query strings or account URLs.
                        const auto host=source.host();QString role="external";
                        if(host=="socialstream.ninja")role=source.path()=="/background.html"?"engine":source.path()=="/featured.html"?"overlay":"asset";
                        else if(host=="studio-chat.invalid")role="host";
                        frames.append(QJsonObject{{"host",host},{"role",role}});BOOL next=FALSE;iterator->MoveNext(&next);
                    }
                }
                rows.append(QJsonObject{{"pid",pid},{"kind",int(kind)},{"frames",frames}});
            }
            resourceProcesses=rows;return S_OK;
        }).Get());
    if(FAILED(hr))resourceDiagnosticPending=false;
}
void Host::tick() {
    const auto now=QDateTime::currentMSecsSinceEpoch();
    sampleResources();
    if(!ready && now-startedAt>20000 && error.isEmpty())error="Chat is unavailable. Restart Studio to try again.";
    for(auto &[id,p]:pages) {
        if(!p->web||p->terminal||p->role=="signin"||p->role=="account-clear")continue;
        const auto ping=p->property("pingAt").toLongLong();
        if(p->property("pingPending").toBool() && now-ping>10000) {
            p->setProperty("pingPending",false);p->setProperty("pingAt",now);
            if(p->role=="overlay"){pendingToken.clear();renderedToken.clear();}
            if(p->role=="engine")ready=false;
            p->web->Reload();states[p->channel]="Comments paused. Reconnecting.";
        } else if(!p->property("pingPending").toBool() && now-ping>5000) {
            p->setProperty("pingAt",now);p->setProperty("pingPending",true);QPointer<Page> self(p.get());
            p->web->ExecuteScript(L"0",Callback<ICoreWebView2ExecuteScriptCompletedHandler>([self](HRESULT,LPCWSTR)->HRESULT{if(self)self->setProperty("pingPending",false);return S_OK;}).Get());
        }
        if(p->role=="engine" && !ready && now-p->property("readyProbeAt").toLongLong()>333) {
            p->setProperty("readyProbeAt",now);
            p->web->ExecuteScript(L"!!(window.ssappBackgroundLoadState?.status==='ready' && typeof loadedFirst!=='undefined' && loadedFirst && isExtensionOn)",
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>([this](HRESULT hr,LPCWSTR result)->HRESULT{
                    bool hostPeer=false,enginePeer=false;
                    for(const auto &[uuid,peer]:peers)if(auto p=page(peer.tab)){hostPeer|=p->role=="host";enginePeer|=p->role=="engine";}
                    if(SUCCEEDED(hr)&&QString::fromWCharArray(result)=="true"&&hostPeer&&enginePeer&&!ready){ready=true;startupTiming["engine_ready_ms"]=QDateTime::currentMSecsSinceEpoch()-startedAt;error.clear();configureChannels({{"channels",channels},{"practice",practice}});}return S_OK;
                }).Get());
        }
        if(p->role=="capture" && !p->terminal && !p->probePending && now>=p->probeAt) {
            p->probeAt=now+10000;p->probePending=true;QPointer<Page> self(p.get());
            const auto script=QString::fromUtf8(read(":/mixer/chat/page-probe.js"));
            p->web->ExecuteScript(script.toStdWString().c_str(),
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>([self](HRESULT hr,LPCWSTR result)->HRESULT {
                    if(!self)return S_OK;self->probePending=false;if(FAILED(hr))return S_OK;
                    const auto outer=QJsonDocument::fromJson(("["+QString::fromWCharArray(result)+"]").toUtf8()).array();if(outer.isEmpty())return S_OK;
                    const auto value=QJsonDocument::fromJson(outer.at(0).toString().toUtf8()).object();
                    // SPA history changes can bypass NavigationStarting. Reject
                    // their messages above and return to the configured channel.
                    const QUrl configured(self->property("captureUrl").toString());
                    if(!value["url"].toString().isEmpty()&&!chatCaptureChannelMatches(configured,QUrl(value["url"].toString()))) {
                        self->host.states[self->channel]="Comments paused. Reconnecting.";
                        self->web->Navigate(configured.toString().toStdWString().c_str());return S_OK;
                    }
                    const auto now=QDateTime::currentMSecsSinceEpoch();
                    const auto name=value["name"].toString().left(128);if(!name.isEmpty())self->host.names[self->channel]=name;
                    auto resolve=self->property("resolve").toString();
                    if(resolve.isEmpty()&&QRegularExpression("^https://www[.]youtube[.]com/channel/[a-zA-Z0-9_-]+/live$").match(value["resolve"].toString()).hasMatch()) {
                        resolve=value["resolve"].toString();self->setProperty("resolve",resolve);
                    }
                    const auto status=chatPageStatus(value);if(!status.isEmpty())self->host.states[self->channel]=status;
                    if(value["notFound"].toBool()){self->terminal=true;return S_OK;}
                    if(value["login"].toBool())return S_OK;
                    if(!resolve.isEmpty()) {
                        if(!self->url.contains("/live_chat")) {
                            const auto video=value["video"].toString();
                            if(value["live"].toBool()&&QRegularExpression("^[a-zA-Z0-9_-]{11}$").match(video).hasMatch()) {
                                
                                self->web->Navigate(("https://www.youtube.com/live_chat?is_popout=1&v="+video).toStdWString().c_str());
                            }else if(now>=self->retryAt) {
                                self->host.states[self->channel]="Waiting for your stream";
                                self->retryAt=now+self->retrySeconds*1000;self->retrySeconds=std::min(300,self->retrySeconds*2);
                                self->web->Navigate(resolve.toStdWString().c_str());
                            }
                        }else if((value["ended"].toBool()||value["offline"].toBool()||value["unavailable"].toBool())&&now>=self->retryAt) {
                            self->host.states[self->channel]="Waiting for your stream";
                            self->retryAt=now+self->retrySeconds*1000;self->retrySeconds=std::min(300,self->retrySeconds*2);self->web->Navigate(resolve.toStdWString().c_str());
                        }else if(value["live"].toBool()&&!value["ended"].toBool()&&!value["offline"].toBool()&&!value["unavailable"].toBool()) {
                            // A new page has no status yet. Only a confirmed live stream clears the backoff.
                            self->retrySeconds=60;self->retryAt=0;
                        }
                    }
                    return S_OK;
                }).Get());
        }
    }
    if(pendingToken.isEmpty())return;
    if(now-requestedAt>15000){error="The comment could not be shown. Try Show on screen again.";pendingToken.clear();return;}
    Page *host=nullptr,*overlay=nullptr;QString uuid;
    for(auto &[id,p]:pages){if(p->role=="host")host=p.get();if(p->role=="overlay")overlay=p.get();}
    if(!host||!overlay||!host->web||!overlay->web)return;
    for(auto &[key,peer]:peers)if(peer.tab==overlay->id)uuid=key;
    if(uuid.isEmpty())return;
    // Keep retrying until the exact text has rendered and output opacity is visible.
    auto packet=pendingCard;packet.remove("style");packet["textonly"]=true;
    packet["chatname"]=packet["chatname"].toString().toHtmlEscaped();
    // The native integration renders a complete, plain-text support label.
    // featured.html's generic metadata chooses donation OR membership.
    const auto support=chatSupportLabel(pendingCard);
    packet["hasDonation"]="";packet["membership"]="";packet["studioSupport"]=support;
    const auto token=pendingToken;QPointer<Page> target(overlay);
    const QString script="(()=>{const o=document.getElementById('output'),m=document.querySelector('#output .hl-message'),s=document.querySelector('#output .studio-support');const expected="+
        QString::fromUtf8(QJsonDocument(QJsonArray{support}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)+
        ";return !!(o&&m&&document.fonts.status==='loaded'&&(!expected||(s&&s.textContent===expected))&&Number(getComputedStyle(o).opacity)>.9&&m.textContent==="+
        QString::fromUtf8(QJsonDocument(QJsonArray{pendingCard["chatmessage"]}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)+
        "&&document.documentElement.dataset.studioCard==="+QString::fromUtf8(QJsonDocument(QJsonArray{token}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)+
        ");})()";
    overlay->web->ExecuteScript(script.toStdWString().c_str(),Callback<ICoreWebView2ExecuteScriptCompletedHandler>([target,token](HRESULT hr,LPCWSTR result)->HRESULT {
        if(target && target->host.pendingToken==token && SUCCEEDED(hr) && QString::fromWCharArray(result)=="true"){target->host.timing["render_ready_ms"]=QDateTime::currentMSecsSinceEpoch()-target->host.requestedAt;target->snapshot(token);}return S_OK;
    }).Get());
    if(overlay->property("sentToken").toString()!=token || now-overlay->property("sentAt").toLongLong()>2000) {
        timing["dispatch_ms"]=now-requestedAt;overlay->setProperty("sentToken",token);overlay->setProperty("sentAt",now);
        auto card=packet;card["studioToken"]=token;
        const auto script="window.feature&&feature("+QString::fromUtf8(json(card))+","+QString::fromUtf8(QJsonDocument(QJsonArray{uuid}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)+")";
        host->web->ExecuteScript(script.toStdWString().c_str(),nullptr);
    }
}
QJsonObject Host::command(const QJsonObject &c) {
    const auto op=c["op"].toString();
    if(op=="start"){setup(c);return {{"ok",true}};}
    if(op=="configure"){configureChannels(c);return {{"ok",true}};}
    if(op=="signin") {
        if(!ready||!environment||clearingAccounts)return {{"error","Wait for chat to finish connecting, then try Accounts again."}};
        QJsonObject channel;for(const auto &v:channels)if(v.toObject()["id"]==c["channel"])channel=v.toObject();
        if(channel.isEmpty())return {{"error","Add a channel before signing in."}};
        const auto platform=channel["platform"].toString(),target=chatSignInUrl(platform);
        if(target.isEmpty())return {{"error","YouTube public chat works without sign-in. Google account sign-in is not supported in Studio yet."}};
        for(auto &[id,p]:pages)if(p->role=="signin") {p->show();p->raise();p->activateWindow();accountNotice="Finish or close the existing sign-in window before opening another account.";return {{"ok",true}};}
        const int tab=open("signin",target);auto p=page(tab);p->channel=channel["id"].toString();p->setProperty("platform",platform);
        p->setWindowTitle("Sign in to "+platform+" - Desktop Studio");p->show();p->raise();p->activateWindow();
        QTimer::singleShot(150,p,[p]{p->raise();p->activateWindow();}); // Let the parent menu finish closing first.
        accountNotice="Sign-in window open. Return to chat when you have finished.";return {{"ok",true}};
    }
    if(op=="signout") {
        if(clearingAccounts)return {{"error","Sign-out is already in progress."}};
        if(!environment||!ready)return {{"error","Wait for chat to finish connecting before signing out."}};
        // Close site documents before clearing so an active page cannot restore
        // its old session from in-memory state while deletion is in flight.
        std::vector<int> windows;for(auto &[id,p]:pages)if(p->role=="signin"||p->role=="capture")windows.push_back(id);for(int id:windows)closePage(id);
        clearingAccounts=true;accountNotice="Signing out of chat accounts...";open("account-clear","about:blank");
        return {{"ok",true}};
    }
    if(op=="poll") {
        QJsonArray batch;int bytes=0;
        while(!feed.isEmpty() && bytes<24000){auto item=feed.takeAt(0);bytes+=json(item.toObject()).size();batch.append(item);}
        bool overlayReady=false;for(const auto &[uuid,peer]:peers)if(auto p=page(peer.tab);p&&p->role=="overlay")overlayReady=true;
        bool practiceReady=!practice;for(auto &[id,p]:pages)if(p->role=="practice")practiceReady=p->property("practiceReady").toBool();
        // A channel that has gone quiet, or a capture that has stopped, must not look like it is still reading.
        auto shownStates=states;const auto now=QDateTime::currentMSecsSinceEpoch();
        for(const auto &[channel,at]:lastComment)if(now-at>300000&&shownStates[channel].toString()=="Reading comments")shownStates[channel]="No new comments in 5 minutes";
        QString accountPlatform;for(auto &[id,p]:pages)if(p->role=="signin")accountPlatform=p->property("platform").toString();
        QJsonArray resourcePages;
        if(resourceDiagnostics)for(auto &[id,p]:pages)if(p->role!="signin"&&p->role!="account-clear")resourcePages.append(QJsonObject{{"tab",id},{"role",p->role},{"platform",p->property("platform").toString()},{"stats",p->property("resourceStats").toJsonObject()}});
        return {{"ok",true},{"ready",ready&&practiceReady&&overlayReady},{"error",error},{"account",QJsonObject{{"open",!accountPlatform.isEmpty()},{"platform",accountPlatform},{"clearing",clearingAccounts},{"notice",accountNotice}}},{"oversizedMessages",oversizedMessages},{"channels",shownStates},{"names",names},{"timing",timing},{"startup_timing",startupTiming},{"messages",batch},{"renderedToken",renderedToken},{"pendingToken",pendingToken},{"browserPid",int(browserPid)},{"diagnostic",diagnostic},{"pageCount",int(pages.size())},{"pageResources",QJsonObject{{"processes",resourceProcesses},{"pages",resourcePages}}}};
    }
    if(op=="inject") {
        if(!practice||!ready)return {{"error","Practice chat is not ready."}};
        for(auto &[id,p]:pages)if(p->role=="practice" && p->web && p->property("practiceReady").toBool()) {
            auto data=c["message"].toObject();data["textonly"]=true;
            p->web->ExecuteScript(("window.capture&&capture("+QString::fromUtf8(json(data))+")").toStdWString().c_str(),nullptr);return {{"ok",true}};
        }
    }
    if(op=="show") {
        if(!ready)return {{"error","Chat is getting ready."}};
        pendingToken=c["token"].toString();pendingCard=c["message"].toObject();requestedAt=QDateTime::currentMSecsSinceEpoch();error.clear();timing={};
        for(auto &[id,p]:pages)if(p->role=="overlay" && p->web) {
            const auto style=c["style"].toString();
            QString css="#output,#output *{transition:none!important;animation:none!important}html,body{background:transparent!important}#output{position:fixed!important;left:36px!important;right:36px!important;bottom:36px!important;width:auto!important} .hl-c-cont{padding:20px!important;border-radius:10px!important;background:rgba(13,22,32,.94)!important;display:flex!important;flex-direction:column!important} .hl-name{position:static!important;background:transparent!important;color:#ffb454!important;padding:0!important;font-size:24px!important;order:0!important}.hl-message{background:transparent!important;color:white!important;padding:8px 0 0!important;font-size:30px!important;box-shadow:none!important;width:auto!important;order:1!important}.hl-img,.sourcetype{display:none!important}";
            if(style=="bubble")css+="#output{right:auto!important;max-width:760px!important}.hl-c-cont{background:white!important;border-radius:40px!important}.hl-message,.hl-name{color:#10202e!important}";
            if(style=="big")css+="#output{left:126px!important;right:126px!important;top:198px!important;bottom:auto!important}.hl-message{font-size:44px!important}.hl-c-cont{padding:40px!important}";
            css+=".studio-support{position:static!important;order:1!important;color:#ffcf86;font-size:20px;line-height:1.3;white-space:pre-wrap;overflow-wrap:anywhere;margin-top:6px}.hl-message{order:2!important}";
            if(style=="bubble")css+=".studio-support{color:#725018}";
            const auto quoted=QString::fromUtf8(QJsonDocument(QJsonArray{css}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
            p->web->ExecuteScript(("(()=>{let s=document.getElementById('studio-style');if(!s){s=document.createElement('style');s.id='studio-style';document.head.append(s)}s.textContent="+quoted+";})()").toStdWString().c_str(),nullptr);
        }
        return {{"ok",true}};
    }
    if(op=="hide") {
        pendingToken.clear();renderedToken.clear();
        for(auto &[uuid,peer]:peers)if(page(peer.tab)&&page(peer.tab)->role=="overlay")deliver(uuid,{{"dataReceived",QJsonObject{{"overlayNinja",false}}}});
        return {{"ok",true}};
    }
    return {{"error","Chat command is unavailable."}};
}
} // namespace
struct ChatWorker::Impl { Host host; };
ChatWorker::ChatWorker(std::function<void(QImage,QString)> frame) : impl_(std::make_unique<Impl>()) { impl_->host.frame=std::move(frame); }
ChatWorker::~ChatWorker() = default;
QJsonObject ChatWorker::command(const QJsonObject &command) {
    try{return impl_->host.command(command);}catch(...){impl_->host.error="Chat is unavailable. Reinstall Studio to restore its chat files.";return {{"error",impl_->host.error}};}
}
}
