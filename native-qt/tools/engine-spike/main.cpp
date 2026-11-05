#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
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
static quint64 ticks(FILETIME t) { return (quint64(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
static quint64 wallTicks() { FILETIME t;GetSystemTimeAsFileTime(&t);return ticks(t); }
#include "process-meter.h"
static QByteArray json(QJsonObject o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
static QByteArray read(QString p) { QFile f(p);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error(("Cannot read "+p).toStdString());return f.readAll(); }
static void save(QString p,QJsonObject o) { QSaveFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(o).toJson())<0||!f.commit())qFatal("Cannot save %s",qPrintable(p)); }
static QString take(LPWSTR p) { auto s=QString::fromWCharArray(p?p:L"");CoTaskMemFree(p);return s; }
struct Host;
struct Page:QWidget {
    Host &host; int id;QString role,url,mainFrame; bool active=false;
    ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> web;
    Page(Host &h,int i,QString r,QString u):host(h),id(i),role(r),url(u){resize(1100,760);setWindowTitle("engine-spike "+r);winId();}
    ~Page(){if(controller)controller->Close();}
    QJsonObject tab(){return {{"id",id},{"url",url},{"title",role},{"active",active},{"windowId",1},{"status",web?"complete":"loading"}};}
    void start();void configure();
    void post(QJsonObject o){if(web)web->PostWebMessageAsJson(QString::fromUtf8(json(o)).toStdWString().c_str());}
};
struct Peer { int tab;QString frame;QJsonObject data; };
struct Pending {int tab;QString frame,id;int receiver;QString receiverFrame;};
struct Host {
    QString root,out,profile,session,args,shim,twitch;QJsonObject manifest,storage;
    QFile events,feed;ComPtr<ICoreWebView2Environment> environment;
    std::map<int,std::unique_ptr<Page>> pages;std::map<QString,Peer> peers;std::map<QString,Pending> pending;
    int nextTab=1;quint64 sequence=0;UINT32 browserPid=0;ProcessMeter meter;
    void log(QString event,QJsonObject o={}) {o["event"]=event;o["ms"]=double(QDateTime::currentMSecsSinceEpoch());events.write(json(o)+'\n');events.flush();}
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
        if(kind=="log"){auto d=m["detail"].toObject();d["tab"]=p.id;d["role"]=p.role;d["url"]=m["url"];log(m["event"].toString(),d);return;}
        if(kind=="document-ready"){if(m["url"].toString()==p.url)p.mainFrame=frame;log(kind,{{"tab",p.id},{"url",m["url"]},{"frame",frame}});return;}
        if(kind=="bridge-leave"){auto i=peers.find(m["uuid"].toString());if(i!=peers.end()&&i->second.tab==p.id&&i->second.frame==frame)leave(i->first);return;}
        if(kind=="runtime-reply"){auto i=pending.find(m["token"].toString());if(i!=pending.end()&&i->second.receiver==p.id&&i->second.receiverFrame==frame){auto req=i->second;pending.erase(i);reply(req.tab,req.frame,req.id,m["value"]);}return;}
        if(kind!="rpc")return;
        const auto method=m["method"].toString(),id=m["id"].toString();const auto a=m["args"].toArray();
        auto ok=[&](QJsonValue v=QJsonValue()){reply(p.id,frame,id,v);};auto fail=[&](QString e){reply(p.id,frame,id,{},e);};
        log("api",{{"tab",p.id},{"role",p.role},{"name",method},{"args",a}});
        if(method.startsWith("storage.")){
            auto parts=method.split('.');auto area=parts[1],op=parts[2];auto data=storage[area].toObject();
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
            storage[area]=data;save(profile+"/extension-storage.json",storage);ok();if(!changes.isEmpty())broadcast("storage.onChanged",{changes,area});return;
        }
        if(method=="runtime.sendMessage"){
            if(p.role=="engine"){for(auto &[tab,w]:pages)if(tab!=p.id&&!w->mainFrame.isEmpty())w->post({{"kind","runtime"},{"frame",w->mainFrame},{"token","broadcast"},{"message",a.at(0)},{"sender",QJsonObject{{"url",p.url},{"tab",p.tab()}}}});ok();return;}
            for(auto &[tab,w]:pages)if(w->role=="engine"){route(p.id,frame,id,tab,a.at(0));return;}fail("Engine unavailable");return;
        }
        if(method=="tabs.query"){QJsonArray result;auto q=a.at(0).toObject();for(auto &[tab,w]:pages){if(q.contains("active")&&q["active"].toBool()!=w->active)continue;if(q.contains("url")){auto patterns=q["url"].isArray()?q["url"].toArray():QJsonArray{q["url"]};bool match=false;for(auto pattern:patterns)match|=QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern.toString())).match(w->url).hasMatch();if(!match)continue;}result.append(w->tab());}ok(result);return;}
        if(method=="tabs.get"||method=="tabs.getCurrent"){auto target=method=="tabs.getCurrent"?&p:page(a.at(0).toInt());if(target)ok(target->tab());else fail("Unknown tab");return;}
        if(method=="tabs.sendMessage"){route(p.id,frame,id,a.at(0).toInt(),a.at(1));return;}
        if(method=="tabs.update"){auto target=page(a.at(0).toInt());if(!target){fail("Unknown tab");return;}auto props=a.at(1).toObject();target->active=props["active"].toBool();if(props.contains("url"))target->web->Navigate(props["url"].toString().toStdWString().c_str());ok(target->tab());return;}
        if(method=="tabs.create"||method=="host.open"){auto spec=a.at(0).toObject();if(method=="host.open"&&p.role!="host"){fail("Host only");return;}auto tab=open(spec["role"].toString("tool"),spec["url"].toString());ok(page(tab)->tab());return;}
        if(method=="tabs.remove"||method=="host.close"){
            int target=a.at(0).toInt();if(!page(target)||target==p.id){fail("Unknown/self tab close");return;}ok();QTimer::singleShot(0,[this,target]{leaveTab(target);pages.erase(target);broadcast("tabs.onRemoved",{target,QJsonObject{{"windowId",1},{"isWindowClosing",false}}});});return;
        }
        if(method=="host.peers"){QJsonArray result;for(auto &[uuid,peer]:peers){auto d=peer.data;d["tab"]=peer.tab;result.append(d);}ok(result);return;}
        if(method=="host.metrics"){auto u=meter.sample(browserPid,processes());QJsonObject result{{"browserPid",int(browserPid)},{"privateWorkingSetMiB",u.memory},{"processCount",u.count},{"errors",u.errors},{"pageCount",int(pages.size())}};log("metrics",result);ok(result);return;}
        if(method=="host.quit"&&p.role=="host"){ok();QTimer::singleShot(250,qApp,&QApplication::quit);return;}
        if(method.startsWith("debugger.")){
            auto target=page(a.at(0).toObject()["tabId"].toInt());
            // Hard native boundary: no debugger input or evaluation can ever reach Twitch.
            if(!target||target->role!="test-source"||QUrl(target->url).host()!="engine-spike.invalid"){log("debugger-denied",{{"args",a}});fail("Debugger dry run restricted to local test-source");return;}
            log("debugger-dry",{{"tab",target->id},{"url",target->url},{"method",method},{"args",a}});
            broadcast("host.debugger",{QJsonObject{{"tab",target->id},{"url",target->url},{"method",method},{"args",a},{"executed",false}}});
            // Logged stub is intentional; never claim the actual browser command executed.
            ok(QJsonObject{});return;
        }
        if(method=="bridge.join"){
            auto d=a.at(0).toObject();auto uuid=d["uuid"].toString();if(QUrl(m["url"].toString()).host()!="vdo.socialstream.ninja"){fail("Bridge origin required");return;}
            leave(uuid);peers[uuid]={p.id,frame,d};ok();
            // Join reply precedes peer discovery; all events still travel via the real iframe.
            for(auto &[other,peer]:peers)if(other!=uuid){if(views(peers[uuid],peer))connect(uuid,other);if(views(peer,peers[uuid]))connect(other,uuid);}return;
        }
        if(method=="bridge.send"){
            auto uuid=a.at(0).toString();auto i=peers.find(uuid);if(i==peers.end()||i->second.tab!=p.id||i->second.frame!=frame){fail("Unknown bridge sender");return;}
            auto packet=a.at(1).toObject();auto type=packet["type"].toString(),target=packet["UUID"].toString();int count=0;
            for(auto &[other,dest]:peers){if(other==uuid||(!target.isEmpty()&&target!=other))continue;bool edge=type=="pcs"?views(dest,i->second):type=="rpcs"?views(i->second,dest):false;if(!edge)continue;
                deliver(other,{{"dataReceived",packet["sendData"]},{"UUID",uuid}});count++;
                if(page(dest.tab)&&page(dest.tab)->role=="host"&&p.role=="engine") {QJsonObject item{{"ms",double(QDateTime::currentMSecsSinceEpoch())},{"fromTab",p.id},{"payload",packet["sendData"].toObject()["overlayNinja"]}};feed.write(json(item)+'\n');feed.flush();log("host-feed",item);}
            }ok(count);return;
        }
        if(method.startsWith("action.")||method.startsWith("notifications.")||method.startsWith("windows.")){log("api-noop",{{"name",method}});ok(QJsonObject{{"id",1}});return;}
        log("missing-api",{{"name",method},{"tab",p.id}});fail("Unsupported API: "+method);
    }
};
void Page::start(){host.check(host.environment->CreateCoreWebView2Controller(reinterpret_cast<HWND>(winId()),Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this](HRESULT hr,ICoreWebView2Controller*c)->HRESULT{if(!host.check(hr,"controller",id)||!c)return S_OK;controller=c;c->get_CoreWebView2(&web);configure();return S_OK;}).Get()),"create-controller",id);}
void Page::configure(){
    RECT bounds{0,0,width(),height()};controller->put_Bounds(bounds);controller->put_IsVisible(FALSE);
    UINT32 pid=0;web->get_BrowserProcessId(&pid);host.browserPid=pid;host.log("controller-ready",{{"tab",id},{"role",role},{"browserPid",int(pid)}});
    ComPtr<ICoreWebView2_3>w3;web.As(&w3);if(!w3||!host.check(w3->SetVirtualHostNameToFolderMapping(L"socialstream.ninja",host.root.toStdWString().c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW),"mapping",id))return;
    ComPtr<ICoreWebView2Settings>settings;web->get_Settings(&settings);settings->put_AreDefaultScriptDialogsEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);
    EventRegistrationToken token{};
    web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*a)->HRESULT{LPWSTR raw=nullptr;a->get_WebMessageAsJson(&raw);auto m=QJsonDocument::fromJson(take(raw).toUtf8()).object();host.receive(*this,m);return S_OK;}).Get(),&token);
    web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*a)->HRESULT{LPWSTR raw=nullptr;a->get_Uri(&raw);url=take(raw);host.leaveTab(id);mainFrame.clear();host.log("navigation",{{"tab",id},{"url",url}});return S_OK;}).Get(),&token);
    web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*a)->HRESULT{BOOL success=FALSE;COREWEBVIEW2_WEB_ERROR_STATUS status{};a->get_IsSuccess(&success);a->get_WebErrorStatus(&status);host.log("navigation-complete",{{"tab",id},{"success",bool(success)},{"status",int(status)}});host.broadcast("tabs.onUpdated",{id,QJsonObject{{"status",success?"complete":"error"}},tab()});return S_OK;}).Get(),&token);
    web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*a)->HRESULT{COREWEBVIEW2_PROCESS_FAILED_KIND kind;a->get_ProcessFailedKind(&kind);QJsonObject d{{"tab",id},{"kind",int(kind)}};ComPtr<ICoreWebView2ProcessFailedEventArgs2>a2;if(SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a2)))){INT32 code=0;COREWEBVIEW2_PROCESS_FAILED_REASON reason;a2->get_ExitCode(&code);a2->get_Reason(&reason);d["exitCode"]=QString::number(quint32(code),16);d["reason"]=int(reason);}host.log("process-failed",d);return S_OK;}).Get(),&token);
    web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs*a)->HRESULT{a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&token);
    web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([this](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*a)->HRESULT{a->put_Handled(TRUE);host.log("popup-denied",{{"tab",id}});return S_OK;}).Get(),&token);
    ComPtr<ICoreWebView2_22>w22;web.As(&w22);if(w22)w22->AddWebResourceRequestedFilterWithRequestSourceKinds(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL);else web->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs*a)->HRESULT{
        ComPtr<ICoreWebView2WebResourceRequest>req;a->get_Request(&req);LPWSTR raw=nullptr;req->get_Uri(&raw);auto uri=take(raw);QUrl u(uri);COREWEBVIEW2_WEB_RESOURCE_CONTEXT ctx;a->get_ResourceContext(&ctx);
        bool vdo=u.host()=="vdo.socialstream.ninja";bool local=u.host()=="socialstream.ninja"||u.host()=="engine-spike.invalid";
        bool blocked=u.host()=="io.socialstream.ninja"||u.host().endsWith(".vdo.ninja")||u.host()=="vdo.ninja"||vdo;
        QString asset;if(vdo&&ctx==COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT)asset=":/bridge.html";
        if(u.host()=="engine-spike.invalid") {if(u.path()=="/host.html")asset=":/host.html";else if(u.path()=="/test-source.html")asset=":/test-source.html";else blocked=true;}
        host.log("request",{{"tab",id},{"role",role},{"url",uri},{"host",u.host()},{"context",int(ctx)},{"disposition",!asset.isEmpty()?"intercepted":blocked?"blocked":local?"mapped-local":"network"}});
        if(!asset.isEmpty()||blocked){auto data=asset.isEmpty()?QByteArray("Cloud relay blocked by engine-spike"):read(asset);ComPtr<IStream>stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(data.constData()),UINT(data.size())));ComPtr<ICoreWebView2WebResourceResponse>resp;host.environment->CreateWebResourceResponse(stream.Get(),asset.isEmpty()?403:200,asset.isEmpty()?L"Blocked":L"OK",L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store",&resp);a->put_Response(resp.Get());}return S_OK;
    }).Get(),&token);
    for(const auto &name:{L"Network.requestWillBeSent",L"Network.responseReceived",L"Network.webSocketCreated",L"Network.webSocketWillSendHandshakeRequest",L"Network.webSocketHandshakeResponseReceived",L"Network.loadingFailed"}){
        ComPtr<ICoreWebView2DevToolsProtocolEventReceiver>receiver;web->GetDevToolsProtocolEventReceiver(name,&receiver);if(receiver)receiver->add_DevToolsProtocolEventReceived(Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>([this,name](ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs*a)->HRESULT{LPWSTR raw=nullptr;a->get_ParameterObjectAsJson(&raw);host.log("cdp-network",{{"tab",id},{"method",QString::fromWCharArray(name)},{"data",QJsonDocument::fromJson(take(raw).toUtf8()).object()}});return S_OK;}).Get(),&token);
    }
    web->CallDevToolsProtocolMethod(L"Network.enable",L"{}",nullptr);
    // Browser enforcement supplements the main-world WebSocket guard, including workers.
    web->CallDevToolsProtocolMethod(L"Network.setBlockedURLs",L"{\"urls\":[\"wss://*.socialstream.ninja/*\",\"ws://*.socialstream.ninja/*\",\"*://*.vdo.ninja/*\",\"*://vdo.ninja/*\"]}",nullptr);
    QJsonObject cfg{{"tab",id},{"role",role},{"manifest",host.manifest}};
    auto script=host.shim;script.replace("__ENGINE_CONFIG__",QString::fromUtf8(json(cfg)));
    if(role=="twitch")script+="\nif(window===top){addEventListener('load',()=>{\n"+QString::fromUtf8(read(host.root+"/sources/twitch.js"))+"\n});}\n";
    host.check(web->AddScriptToExecuteOnDocumentCreated(script.toStdWString().c_str(),Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([this](HRESULT hr,LPCWSTR)->HRESULT{if(host.check(hr,"document-injection",id))host.check(web->Navigate(url.toStdWString().c_str()),"navigate",id);return S_OK;}).Get()),"add-script",id);
}
int main(int argc,char**argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QCommandLineParser cli;cli.addHelpOption();
    cli.addOptions({{"ssn-root","Read-only SSN checkout","path","C:/Users/Steve/code/social_stream"},{"out","Fresh evidence directory","path"},{"profile","Persistent isolated browser/storage profile","path"},{"room","Local room ID","id","engine-spike-room"},{"remote-debugging-port","Loopback DevTools port","port","9337"},{"browser-args","Additional browser flags","flags","--disable-gpu --in-process-gpu"},{"twitch","Optional Twitch channel","channel"},{"duration-sec","Exit after seconds (0: until host.quit)","seconds","0"}});cli.process(app);
    try {
        if(cli.value("out").isEmpty())throw std::runtime_error("--out required");Host h;h.root=QFileInfo(cli.value("ssn-root")).canonicalFilePath();h.out=QFileInfo(cli.value("out")).absoluteFilePath();
        if(QFileInfo::exists(h.out))throw std::runtime_error("--out must be fresh");QDir().mkpath(h.out);
        h.profile=cli.isSet("profile")?QFileInfo(cli.value("profile")).absoluteFilePath():h.out+"/profile";QDir().mkpath(h.profile);h.session=cli.value("room");h.twitch=cli.value("twitch");
        h.events.setFileName(h.out+"/events.jsonl");h.feed.setFileName(h.out+"/feed.jsonl");
        if(!h.events.open(QIODevice::WriteOnly|QIODevice::NewOnly)||!h.feed.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("Cannot create evidence logs");
        h.manifest=QJsonDocument::fromJson(read(h.root+"/manifest.json")).object();h.shim=QString::fromUtf8(read(":/chrome-shim.js"));
        if(QFileInfo::exists(h.profile+"/extension-storage.json")){
            QJsonParseError error;h.storage=QJsonDocument::fromJson(read(h.profile+"/extension-storage.json"),&error).object();
            if(error.error!=QJsonParseError::NoError||!h.storage["sync"].isObject()||!h.storage["local"].isObject())throw std::runtime_error("Invalid persistent extension storage; refusing to overwrite");
            auto storedRoom=h.storage["sync"].toObject()["streamID"].toString(h.session);
            if(cli.isSet("room")&&h.session!=storedRoom)throw std::runtime_error("--room differs from the persistent profile; use its room or a fresh profile");
            h.session=storedRoom;
        }
        else {h.storage={{"sync",QJsonObject{{"streamID",h.session},{"password",false},{"state",true}}},{"local",QJsonObject{{"settings",QJsonObject{{"textonlymode",true},{"disableYoutubeAutoScroll",true},{"disableAutoLiveYoutube",true}}},{"allowEmptySettings",true}}}};save(h.profile+"/extension-storage.json",h.storage);}
        h.args="--remote-debugging-address=127.0.0.1 --remote-debugging-port="+cli.value("remote-debugging-port")+" --disable-background-timer-throttling --disable-backgrounding-occluded-windows --disable-renderer-backgrounding --host-resolver-rules=\"MAP io.socialstream.ninja 0.0.0.0, MAP vdo.socialstream.ninja 0.0.0.0, MAP *.vdo.ninja 0.0.0.0, MAP vdo.ninja 0.0.0.0\" "+cli.value("browser-args");
        save(h.out+"/run.json",{{"hostPid",int(GetCurrentProcessId())},{"ssnRoot",h.root},{"profile",h.profile},{"ssnVersion",h.manifest["version"]},{"browserArgs",h.args},{"session",h.session},{"started",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();options->put_AdditionalBrowserArguments(h.args.toStdWString().c_str());
        h.check(CreateCoreWebView2EnvironmentWithOptions(nullptr,h.profile.toStdWString().c_str(),options.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&h](HRESULT hr,ICoreWebView2Environment*e)->HRESULT{
            if(!h.check(hr,"environment")||!e)return S_OK;h.environment=e;LPWSTR version=nullptr;e->get_BrowserVersionString(&version);h.log("runtime",{{"version",take(version)}});
            h.open("host","https://engine-spike.invalid/host.html?session="+h.session);h.open("engine","https://socialstream.ninja/background.html");h.open("test-source","https://engine-spike.invalid/test-source.html");
            if(!h.twitch.isEmpty())h.open("twitch","https://www.twitch.tv/popout/"+h.twitch+"/chat");return S_OK;
        }).Get()),"create-environment");
        auto seconds=cli.value("duration-sec").toDouble();if(seconds>0)QTimer::singleShot(int(seconds*1000),&app,&QApplication::quit);
        int result=app.exec();h.pages.clear();h.environment.Reset();CoUninitialize();return result;
    }catch(const std::exception&e){fprintf(stderr,"engine-spike: %s\n",e.what());return 2;}
}
