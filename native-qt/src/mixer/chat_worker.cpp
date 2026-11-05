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
    Page(Host &h,int i,QString r,QString u):host(h),id(i),role(r),url(u){resize(1280,720);setWindowTitle("Desktop Studio chat");winId();}
    ~Page(){if(controller)controller->Close();}
    QJsonObject tab(){return {{"id",id},{"url",url},{"title",role},{"active",active},{"windowId",1},{"status",web?"complete":"loading"}};}
    void start();void configure(); void snapshot(QString token);
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
    QJsonArray feed; QJsonObject states; QJsonArray channels; bool practice=false, ready=false; QString error, renderedToken, pendingToken; QJsonObject pendingCard; qint64 requestedAt=0; std::function<void(QImage,QString)> frame; QTimer timer;ComPtr<ICoreWebView2Environment> environment;
    std::map<int,std::unique_ptr<Page>> pages;std::map<QString,Peer> peers;std::map<QString,Pending> pending;
    int nextTab=1;quint64 sequence=0;UINT32 browserPid=0;
    void log(QString event,QJsonObject o={}) {
        if(event=="native-error") {error="Chat is unavailable. Restart Studio to try again.";diagnostic=o;}
    }
    QString captureBundle;
    std::unique_ptr<QLockFile> profileOwner;
    QJsonObject names,timing,startupTiming;
    QTimer snapshots;
    void setup(const QJsonObject &c);
    void configureChannels(const QJsonObject &c);
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
                for (const auto &key : {"chatname","hasDonation","membership"}) if(input[key].isString()) clean[key]=input[key].toString().left(256);
                clean["chatmessage"]=comment;
                clean["textonly"]=input["textonly"].toBool();
                const auto practiceType=input["type"].toString("twitch");
                clean["type"]=p.role=="practice"?(QStringList{"twitch","youtube"}.contains(practiceType)?practiceType:QString("twitch")):p.property("platform").toString().toLower();
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
void Page::start(){
    ComPtr<ICoreWebView2Environment10> env; host.environment.As(&env);
    if(!env){host.error="Chat needs a newer Microsoft Edge WebView2 Runtime.";return;}
    ComPtr<ICoreWebView2ControllerOptions> options;env->CreateCoreWebView2ControllerOptions(&options);
    options->put_ProfileName(role=="capture"?L"capture":L"engine");
    QPointer<Page> self(this);
    host.check(env->CreateCoreWebView2ControllerWithOptions(reinterpret_cast<HWND>(winId()),options.Get(),Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([self](HRESULT hr,ICoreWebView2Controller*c)->HRESULT{
        if(!self)return S_OK; if(!self->host.check(hr,"controller",self->id)||!c)return S_OK;
        self->controller=c;c->get_CoreWebView2(&self->web);self->configure();return S_OK;
    }).Get()),"create-controller",id);
}
void Page::receiveNative(ICoreWebView2WebMessageReceivedEventArgs *a,ICoreWebView2Frame *sender) {
    if(!messageClock.isValid()||messageClock.elapsed()>1000){messageClock.restart();messageCount=0;}
    if(++messageCount>(role=="capture"?400:2000))return;
    LPWSTR raw=nullptr;a->get_Source(&raw);const auto origin=take(raw);
    if(role=="capture") {if(!chatCaptureAllowed(host.manifest,QUrl(url),QUrl(origin),sender!=nullptr))return;}
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
    RECT bounds{0,0,width(),height()};controller->put_Bounds(bounds);controller->put_IsVisible(role=="overlay" ? TRUE : FALSE);
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
            allowed &= normalize(next.host())==normalize(original.host());
            bool path=false;for(const auto &v:host.manifest["content_scripts"].toArray())path|=chatScriptMatches(v.toObject(),next);
            // A resolver may visit its own channel or a watch page before opening chat.
            allowed &= path || (next.host()=="www.youtube.com"&&(next.path()=="/watch"||next.path()==QUrl(property("resolve").toString()).path()));
            if(!allowed)host.states[channel]=next.path().contains("login")?"This channel needs sign-in. Sign-in is not available in Studio yet.":"Chat could not open that page. Check the channel link.";
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
    web->CallDevToolsProtocolMethod(L"Network.enable",L"{}",nullptr);
    web->CallDevToolsProtocolMethod(L"Network.setBlockedURLs",L"{\"urls\":[\"wss://*.socialstream.ninja/*\",\"ws://*.socialstream.ninja/*\",\"*://*.vdo.ninja/*\",\"*://vdo.ninja/*\"]}",nullptr);
    QJsonObject cfg{{"tab",id},{"role",role},{"manifest",host.manifest}};
    auto script=host.shim;script.replace("__ENGINE_CONFIG__",QString::fromUtf8(json(cfg)));
    if(role=="capture") {
        script+=host.captureBundle;
        QUrl capture(url);if(capture.host()=="www.youtube.com")capture=QUrl("https://www.youtube.com/live_chat?is_popout=1&v=abcdefghijk");
        for(const auto &v:host.manifest["content_scripts"].toArray()) {
            const auto entry=v.toObject();if(!chatScriptMatches(entry,capture)&&!chatScriptMatches(entry,QUrl(url)))continue;
            script+="\nwindow.__chatSpike.register("+QString::fromUtf8(json(entry))+",function(){\n";
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
    options->put_AdditionalBrowserArguments(L"--disable-background-timer-throttling --disable-backgrounding-occluded-windows --disable-renderer-backgrounding --host-resolver-rules=\"MAP io.socialstream.ninja 0.0.0.0, MAP vdo.socialstream.ninja 0.0.0.0, MAP *.vdo.ninja 0.0.0.0, MAP vdo.ninja 0.0.0.0\"");
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
    QObject::connect(&snapshots,&QTimer::timeout,&snapshots,[this]{if(pendingToken.isEmpty()&&!renderedToken.isEmpty())for(auto &[id,p]:pages)if(p->role=="overlay")p->snapshot(renderedToken);});snapshots.start(67);
}
void Host::configureChannels(const QJsonObject &c) {
    channels=c["channels"].toArray();practice=c["practice"].toBool();
    if(!environment||!ready)return;
    QSet<QString> wanted;for(const auto &v:channels)wanted.insert(v.toObject()["id"].toString());if(practice)wanted.insert("practice");
    std::vector<int> remove;for(auto &[id,p]:pages)if((p->role=="capture"||p->role=="practice")&&!wanted.contains(p->channel))remove.push_back(id);
    for(int id:remove){states.remove(page(id)->channel);leaveTab(id);pages.erase(id);}
    QJsonArray incoming=channels;if(practice)incoming.append(QJsonObject{{"id","practice"},{"url","https://studio-chat.invalid/test-source.html"},{"platform","Twitch"}});
    for(const auto &v:incoming) {
        const auto ch=v.toObject();const auto key=ch["id"].toString();bool found=false;for(auto &[id,p]:pages)found|=p->channel==key;
        if(found)continue;
        auto id=open(key=="practice"?"practice":"capture",ch["url"].toString());auto p=page(id);p->channel=key;
        p->setProperty("captureUrl",ch["url"].toString());p->retryAt=QDateTime::currentMSecsSinceEpoch()+60000;
        p->setProperty("platform",ch["platform"].toString());p->setProperty("resolve",ch["resolve"].toString());
        states[key]=ch["resolve"].toString().isEmpty()?"Connecting to chat":"Waiting for your stream";
    }
}
void Host::tick() {
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if(!ready && now-startedAt>20000 && error.isEmpty())error="Chat is unavailable. Restart Studio to try again.";
    for(auto &[id,p]:pages) {
        if(!p->web||p->terminal)continue;
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
    const auto token=pendingToken;QPointer<Page> target(overlay);
    const QString script="(()=>{const o=document.getElementById('output'),m=document.querySelector('#output .hl-message');return !!(o&&m&&Number(getComputedStyle(o).opacity)>.9&&m.textContent==="+
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
    if(op=="poll") {
        QJsonArray batch;int bytes=0;
        while(!feed.isEmpty() && bytes<24000){auto item=feed.takeAt(0);bytes+=json(item.toObject()).size();batch.append(item);}
        bool overlayReady=false;for(const auto &[uuid,peer]:peers)if(auto p=page(peer.tab);p&&p->role=="overlay")overlayReady=true;
        bool practiceReady=!practice;for(auto &[id,p]:pages)if(p->role=="practice")practiceReady=p->property("practiceReady").toBool();
        // A channel that has gone quiet, or a capture that has stopped, must not look like it is still reading.
        auto shownStates=states;const auto now=QDateTime::currentMSecsSinceEpoch();
        for(const auto &[channel,at]:lastComment)if(now-at>300000&&shownStates[channel].toString()=="Reading comments")shownStates[channel]="No new comments in 5 minutes";
        return {{"ok",true},{"ready",ready&&practiceReady&&overlayReady},{"error",error},{"oversizedMessages",oversizedMessages},{"channels",shownStates},{"names",names},{"timing",timing},{"startup_timing",startupTiming},{"messages",batch},{"renderedToken",renderedToken},{"pendingToken",pendingToken},{"browserPid",int(browserPid)},{"diagnostic",diagnostic},{"pageCount",int(pages.size())}};
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
