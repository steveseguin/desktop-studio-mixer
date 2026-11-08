#include "chat_data.h"
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QItemSelectionModel>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUuid>
#include <cmath>
#include <utility>

namespace versus::mixer {
bool chatMatchesDeletion(const QJsonObject &message,const QJsonObject &deletion) {
    if(message.isEmpty()||deletion.value("capture_epoch").toString().isEmpty()||
        message.value("capture_epoch")!=deletion.value("capture_epoch")||message.value("channel")!=deletion.value("channel"))return false;
    if(deletion.value("engineId").isDouble())return message.value("engineId")==deletion.value("engineId");
    const auto name=deletion.value("chatname").toString();
    return !name.isEmpty()&&message.value("chatname").toString().compare(name,Qt::CaseInsensitive)==0;
}
void watchChatSelection(QItemSelectionModel *selection,QObject *context,std::function<void(QString)> changed) {
    QObject::connect(selection,&QItemSelectionModel::selectionChanged,context,[selection,changed]{const auto rows=selection->selectedIndexes();changed(rows.isEmpty()?QString():rows.first().data(Qt::UserRole).toJsonObject()["id"].toString());});
    QObject::connect(selection,&QItemSelectionModel::currentChanged,context,[selection,changed](const QModelIndex &index){if(index.isValid()&&selection->isSelected(index))changed(index.data(Qt::UserRole).toJsonObject()["id"].toString());});
}
QString chatPlatformName(const QString &type) {
    for(const auto &name:QStringList{"YouTube","Twitch","TikTok","Kick","Facebook","Instagram","X","Rumble"})
        if(name.compare(type,Qt::CaseInsensitive)==0)return name;
    return type;
}
QString chatPageStatus(const QJsonObject &probe) {
    if(probe["notFound"].toBool())return "We could not find that channel. Check the link.";
    if(probe["login"].toBool())return "This channel needs sign-in. Open Accounts to continue.";
    if(probe["ended"].toBool()||probe["offline"].toBool())return "Waiting for your stream";
    if(probe["unavailable"].toBool())return "Chat is unavailable for this stream.";
    return {};
}
QString chatSupportLabel(const QJsonObject &message) {
    QStringList labels;
    if(message["event"].toString().contains("gift",Qt::CaseInsensitive))labels<<"Gift";
    for(const auto *key:{"hasDonation","membership"}) {
        const auto value=message[key].toString().trimmed();
        if(!value.isEmpty()&&!labels.contains(value))labels<<value;
    }
    return labels.join(" · ");
}
bool chatIsSupportEvent(const QJsonObject &message) {
    if(!message["hasDonation"].toString().trimmed().isEmpty())return true;
    const auto event=message["event"].toString().toLower();
    if(event.contains("gift"))return true;
    // A regular subscriber's badge is author status, not a new membership.
    return !message["membership"].toString().isEmpty()&&QStringList{"notice","subscription","resub","sponsorship","membermilestone","membership"}.contains(event);
}
QString chatSignInUrl(const QString &platform) {
    if(platform=="Twitch")return "https://www.twitch.tv/login";
    if(platform=="Kick")return "https://kick.com/";
    if(platform=="TikTok")return "https://www.tiktok.com/login";
    if(platform=="Facebook")return "https://www.facebook.com/login/";
    if(platform=="Instagram")return "https://www.instagram.com/accounts/login/";
    if(platform=="X")return "https://x.com/i/flow/login";
    if(platform=="Rumble")return "https://rumble.com/login.php";
    return {}; // Google account authorization needs an external-browser integration.
}
bool chatSignInAllowed(const QString &platform,const QUrl &url) {
    if(url.scheme()!="https"||!url.userInfo().isEmpty()||url.port()!=-1)return false;
    const auto host=url.host().toLower();
    QStringList sites;
    if(platform=="Twitch")sites={"twitch.tv"};
    else if(platform=="Kick")sites={"kick.com"};
    else if(platform=="TikTok")sites={"tiktok.com"};
    else if(platform=="Facebook")sites={"facebook.com"};
    else if(platform=="Instagram")sites={"instagram.com","facebook.com"};
    else if(platform=="X")sites={"x.com","twitter.com"};
    else if(platform=="Rumble")sites={"rumble.com"};
    for(const auto &site:sites)if(host==site||host.endsWith('.'+site))return true;
    return false;
}
// SSN quotes the parent comment inside a reply. Keep the reply, led by who it answers.
QString chatCommentText(const QJsonObject &captured) {
    const auto reply=captured["reply"].toString();
    if(reply.trimmed().isEmpty())return captured["chatmessage"].toString();
    const auto target=QRegularExpression("^(?:[^:@<]*\\s)?(@[^\\s:<]+)").match(captured["initial"].toString()).captured(1);
    // "@Anna hi" doesn't already name "@Ann", so compare the whole mention.
    const auto named=reply.startsWith(target)&&(reply.size()==target.size()||!(reply[target.size()].isLetterOrNumber()||reply[target.size()]=='_'));
    return target.isEmpty()||named?reply:target+" "+reply;
}
bool verifyChatPack(const QString &root) {
    const auto base=QFileInfo(root).canonicalFilePath();if(base.isEmpty())return false;
    QFile sums(base+"/SHA256SUMS");if(!sums.open(QIODevice::ReadOnly)||sums.size()>1024*1024)return false;
    QSet<QString> paths;
    for(auto line:sums.readAll().split('\n')) {
        line=line.trimmed();if(line.isEmpty())continue;
        if(line.size()<67||line.mid(64,2)!="  ")return false;
        const auto digest=line.left(64);const auto relative=QString::fromUtf8(line.mid(66));
        if(!QRegularExpression("^[a-f0-9]{64}$").match(QString::fromLatin1(digest)).hasMatch()||relative.contains('\\')||relative.startsWith('/')||relative.contains(':')||relative.split('/').contains(".."))return false;
        const auto key=QDir::cleanPath(relative).toLower();if(paths.contains(key))return false;paths.insert(key);
        const auto path=QFileInfo(base+"/"+relative).canonicalFilePath();if(!path.startsWith(base+'/',Qt::CaseInsensitive))return false;
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
        QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file)||hash.result().toHex()!=digest)return false;
    }
    if(!paths.contains("manifest.json")||!paths.contains("background.html")||!paths.contains("featured.html"))return false;
    // An old extra script must not remain reachable through the virtual host.
    QDirIterator files(base,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDirIterator::Subdirectories);
    while(files.hasNext()) {const auto path=files.next();const auto key=QDir(base).relativeFilePath(path).toLower();
        if(files.fileInfo().isSymLink())return false;if(files.fileInfo().isDir())continue;
        if(key!="sha256sums"&&key!="pack-version.json"&&!paths.contains(key))return false;
    }
    QFile version(base+"/pack-version.json");if(!version.open(QIODevice::ReadOnly))return false;
    const auto metadata=QJsonDocument::fromJson(version.readAll()).object();
    QFile manifest(base+"/manifest.json");if(!manifest.open(QIODevice::ReadOnly))return false;
    return metadata["manifestSha256"].toString().toLatin1()==QCryptographicHash::hash(manifest.readAll(),QCryptographicHash::Sha256).toHex();
}
bool chatScriptMatches(const QJsonObject &entry,const QUrl &url,bool subframe) {
    if(subframe&&!entry["all_frames"].toBool())return false;
    bool matched=false;for(const auto &p:entry["matches"].toArray())matched|=chatUrlMatches(p.toString(),url);
    for(const auto &p:entry["exclude_matches"].toArray())if(chatUrlMatches(p.toString(),url))return false;
    auto glob=[&](const QJsonValue &p){auto pattern=QRegularExpression::escape(p.toString());pattern.replace("\\*",".*");pattern.replace("\\?",".");return QRegularExpression("^"+pattern+"$").match(url.toString()).hasMatch();};
    if(entry.contains("include_globs")){bool included=false;for(const auto &p:entry["include_globs"].toArray())included|=glob(p);if(!included)return false;}
    for(const auto &p:entry["exclude_globs"].toArray())if(glob(p))return false;
    return matched&&!entry["js"].toArray().isEmpty();
}
bool chatCaptureChannelMatches(const QUrl &configured,const QUrl &candidate) {
    const auto twitch=[](const QUrl &url){return QStringList{"twitch.tv","www.twitch.tv","m.twitch.tv"}.contains(url.host().toLower());};
    if(!twitch(configured))return true;
    if(!twitch(candidate))return false;
    const auto channel=[](const QUrl &url){const auto parts=url.path().split('/',Qt::SkipEmptyParts);return parts.value(parts.value(0)=="popout"||parts.value(0)=="embed"?1:0).toLower();};
    const auto wanted=channel(configured);
    return !wanted.isEmpty()&&channel(candidate)==wanted;
}
bool chatCaptureAllowed(const QJsonObject &manifest,const QUrl &page,const QUrl &sender,bool subframe) {
    if(sender.scheme()!="https"||!sender.userInfo().isEmpty()||sender.port()!=-1)return false;
    // Only entries belonging to this capture page may authorize a child frame.
    // This permits cross-origin all_frames entries without admitting an unrelated platform/ad.
    for(const auto &v:manifest["content_scripts"].toArray()) {
        const auto entry=v.toObject();if(chatScriptMatches(entry,page)&&chatScriptMatches(entry,sender,subframe))return true;
    }
    return false;
}
QString studioDataPath() {
    const auto override = qEnvironmentVariable("VIDEO_MIXER_DATA_DIR");
    return override.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("VideoMixer") : QDir(override).absolutePath();
}
bool chatUrlMatches(const QString &pattern, const QUrl &url) {
    static const QRegularExpression rule("^(\\*|https?):/+([^/]+)(/.*)$");
    const auto match = rule.match(pattern);
    if (!match.hasMatch() || (url.scheme() != "https" && url.scheme() != "http")) return false;
    if (match.captured(1) != "*" && match.captured(1) != url.scheme()) return false;
    const auto host = match.captured(2).toLower();
    if (host != "*" && host != url.host() && !(host.startsWith("*.") &&
        (url.host() == host.mid(2) || url.host().endsWith(host.mid(1))))) return false;
    auto path = QRegularExpression::escape(match.captured(3)); path.replace("\\*", ".*");
    return QRegularExpression("^"+path+"$").match(url.path()+(url.hasQuery() ? "?"+url.query() : "")).hasMatch();
}
QJsonObject detectChatChannel(QString input, const QJsonObject &manifest, QString *error) {
    const auto fail = [&](const char *why) { if (error) *error = QString::fromUtf8(why); return QJsonObject{}; };
    input = input.trimmed();
    if (input.isEmpty() || input.size()>2048 || input.contains(QRegularExpression("\\s"))) return fail("Paste your channel link, or type your channel name.");
    if (input.startsWith('@')) input = "https://www.youtube.com/"+input;
    else if (QRegularExpression("^[a-zA-Z0-9_]{1,25}$").match(input).hasMatch()) input = "https://www.twitch.tv/"+input;
    else if (!input.contains("://")) input.prepend("https://");
    QUrl url(input, QUrl::StrictMode);
    if (!url.isValid() || url.scheme()!="https" || !url.userInfo().isEmpty() || url.port()!=-1 || !url.fragment().isEmpty())
        return fail("Paste your channel link, or type your channel name.");
    for (const auto &item : QUrlQuery(url).queryItems()) if (QRegularExpression("token|password|secret|auth|session|cookie",QRegularExpression::CaseInsensitiveOption).match(item.first).hasMatch())
        return fail("Use a public channel link without sign-in details.");
    const auto host = url.host().toLower(); const auto path = url.path();
    if(host=="localhost"||host.endsWith(".localhost")||host.endsWith(".local")||host.contains(':')||QRegularExpression("^[0-9.]+$").match(host).hasMatch())return fail("Paste your channel link, or type your channel name.");
    const auto parts = path.split('/', Qt::SkipEmptyParts);
    QString platform, name, resolve;
    auto on = [&](QString site) { return host==site || host=="www."+site || host=="m."+site; };
    const auto first=parts.value(0).toLower();
    const QStringList common{"login","signin","signup","register","settings","account","accounts","about","help","search","explore","directory","discover","following","subscriptions","videos","clips","downloads","privacy","terms"};
    if(common.contains(first)||((on("twitch.tv")||on("kick.com"))&&QStringList{"p","turbo","jobs","store","inventory","wallet","category","categories"}.contains(first))||
        (on("instagram.com")&&QStringList{"p","reel","reels","stories","direct"}.contains(first))||
        ((on("x.com")||on("twitter.com"))&&QStringList{"home","i","intent","messages","notifications","compose"}.contains(first))||
        (on("facebook.com")&&QStringList{"watch","gaming","groups","marketplace","reel","reels","live"}.contains(first)))return fail("That is not a channel link. Paste your channel link, or type your channel name.");
    if (on("youtube.com") || host=="youtu.be") {
        platform="YouTube"; QString video = QUrlQuery(url).queryItemValue("v");
        if (host=="youtu.be") video=parts.value(0);
        if (parts.value(0)=="live" || parts.value(0)=="shorts") video=parts.value(1);
        if (!video.isEmpty()) {
            if (!QRegularExpression("^[a-zA-Z0-9_-]{11}$").match(video).hasMatch()) return fail("That YouTube video link is incomplete. Paste the full link.");
            name="YouTube"; url=QUrl("https://www.youtube.com/live_chat?is_popout=1&v="+video);
        } else if (parts.value(0).startsWith('@') || ((parts.value(0)=="channel" || parts.value(0)=="c" || parts.value(0)=="user") && parts.size()>=2)) {
            name=parts.value(parts.value(0).startsWith('@') ? 0 : 1);
            resolve="https://www.youtube.com/"+(parts.value(0).startsWith('@') ? parts[0] : parts[0]+"/"+parts[1])+"/live";
            url=QUrl(resolve);
        } else return fail("Paste a YouTube video or channel link, or enter its @handle.");
    } else if (on("twitch.tv")) {
        platform="Twitch"; name=parts.value(parts.value(0)=="popout" ? 1 : 0).toLower();
        const bool route=parts.size()==1||(parts.size()==3&&parts[0]=="popout"&&parts[2]=="chat")||(parts.size()==2&&QStringList{"chat","videos","clips","about","schedule"}.contains(parts[1]));
        if (!route||common.contains(name)||!QRegularExpression("^[a-z0-9_]{1,25}$").match(name).hasMatch()) return fail("Paste your Twitch channel link or enter its channel name.");
        url=QUrl("https://www.twitch.tv/popout/"+name+"/chat");
    } else if (on("kick.com")) {
        platform="Kick";auto route=parts;
        if(route.size()>=3&&QRegularExpression("^[a-z]{2}(?:-[A-Z]{2})?$").match(route[0]).hasMatch())route.removeFirst();
        name=route.value(route.value(0)=="popout"?1:0).toLower();
        const bool valid=route.size()==1||(route.size()==3&&route[0]=="popout"&&route[2]=="chat")||(route.size()==2&&route[1]=="chatroom");
        if(!valid||common.contains(name)||!QRegularExpression("^[a-z0-9_-]{1,50}$").match(name).hasMatch())return fail("Paste your Kick channel link.");url=QUrl("https://kick.com/popout/"+name+"/chat");
    }
    else if (on("tiktok.com")) { platform="TikTok"; name=parts.value(0); if (!name.startsWith('@')) return fail("Paste a TikTok @name channel link."); url=QUrl("https://www.tiktok.com/"+name+"/live"); }
    else if (on("facebook.com")) { platform="Facebook"; name=parts.value(0); }
    else if (on("instagram.com")) { platform="Instagram"; name=parts.value(0); if(parts.size()==1)url=QUrl("https://www.instagram.com/"+name+"/live"); }
    else if (on("x.com") || on("twitter.com")) { platform="X"; name=parts.value(0); if (parts.size()==1) url=QUrl("https://x.com/"+name+"/livechat"); }
    else if (on("rumble.com")) {
        platform="Rumble";
        if ((parts.value(0)=="c"||parts.value(0)=="user")&&!parts.value(1).isEmpty()) {
            name=parts[1];url=QUrl("https://rumble.com/"+parts[0]+"/"+parts[1]+"/live");
        } else if (parts.value(0)=="chat"&&parts.value(1)=="popup") name=parts.value(2);
        else return fail("Paste your Rumble channel link or its pop-out chat link.");
    }
    bool supported=false;
    for (const auto &entry : manifest["content_scripts"].toArray()) {
        const auto script=entry.toObject(); bool match=false, excluded=false;
        for (const auto &pattern : script["matches"].toArray()) match |= chatUrlMatches(pattern.toString(), url);
        for (const auto &pattern : script["exclude_matches"].toArray()) excluded |= chatUrlMatches(pattern.toString(), url);
        if (match && !excluded && !script["js"].toArray().isEmpty()) { supported=true; if (platform.isEmpty()) { platform=host; name=path; } }
    }
    if (name.isEmpty() || (!supported && resolve.isEmpty())) return fail("We could not find chat at that link. Paste the channel's full link or its pop-out chat link.");
    // Persist public locators only. Authentication tokens are never channel settings.
    for (const auto &item : QUrlQuery(url).queryItems()) if (!QStringList{"v","is_popout","popout","channel","id","video","chat"}.contains(item.first))
        return fail("Use the public channel link without extra tracking or sign-in details.");
    return {{"id",QUuid::createUuid().toString(QUuid::Id128)}, {"name",name.left(128)}, {"platform",platform}, {"url",url.toString()}, {"resolve",resolve}};
}
bool ChatSettings::load(const QString &path, QString *error) {
    QFile file(path); if (!file.exists()) { room=QUuid::createUuid().toString(QUuid::Id128); return true; }
    if (!file.open(QIODevice::ReadOnly) || file.size()>128*1024) { *error="Cannot read your chat channels."; return false; }
    QJsonParseError parse; const auto document=QJsonDocument::fromJson(file.readAll(), &parse); const auto obj=document.object();
    if (parse.error!=QJsonParseError::NoError || obj["version"]!=1 || !obj["channels"].isArray() || obj["channels"].toArray().size()>16 ||
        !QRegularExpression("^[a-zA-Z0-9]{16,64}$").match(obj["room"].toString()).hasMatch()) { *error="Your chat channels could not be read. The saved file has been left untouched."; return false; }
    QSet<QString> ids;
    for (const auto &entry : obj["channels"].toArray()) {
        const auto channel=entry.toObject();const QUrl url(channel["url"].toString());
        bool valid=entry.isObject() && QRegularExpression("^[a-zA-Z0-9]{32}$").match(channel["id"].toString()).hasMatch() && !ids.contains(channel["id"].toString()) &&
            channel["name"].isString() && channel["name"].toString().size()<=128 && channel["platform"].isString() && channel["platform"].toString().size()<=128 &&
            url.isValid() && url.scheme()=="https" && !url.host().isEmpty() && url.userInfo().isEmpty() && url.port()==-1 && url.fragment().isEmpty() && channel["url"].toString().size()<=2048;
        for (auto it=channel.begin();it!=channel.end();++it) valid &= QStringList{"id","name","platform","url","resolve"}.contains(it.key());
        for (const auto &item : QUrlQuery(url).queryItems()) valid &= QStringList{"v","is_popout","popout","channel","id","video","chat"}.contains(item.first);
        if (!channel["resolve"].toString().isEmpty()) {const QUrl resolve(channel["resolve"].toString());valid &= resolve.scheme()=="https"&&resolve.host()=="www.youtube.com"&&resolve.userInfo().isEmpty()&&resolve.port()==-1&&!resolve.hasQuery()&&!resolve.hasFragment();}
        if(!valid){*error="Your chat channels could not be read. The saved file has been left untouched.";return false;}
        ids.insert(channel["id"].toString());
    }
    channels=obj["channels"].toArray(); practice=obj["practice"].toBool(); room=obj["room"].toString(); return true;
}
bool ChatSettings::save(const QString &path, QString *error) const {
    QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile file(path);
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"channels",channels},{"practice",practice},{"room",room}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) { *error="Cannot save your chat channels."; return false; } return true;
}
QJsonObject CommentsLayer::json() const { return {{"enabled",enabled},{"style",style},{"x",rect.x()},{"y",rect.y()},{"w",rect.width()},{"h",rect.height()}}; }
bool CommentsLayer::read(const QJsonValue &value, CommentsLayer &layer) {
    layer={}; if (value.isUndefined()) return true;
    if (!value.isObject()) return false; const auto o=value.toObject();
    layer.style=o["style"].toString("bar"); layer.enabled=o["enabled"].toBool(true);
    if (!QStringList{"bar","bubble","big"}.contains(layer.style)) return false;
    double values[]{o["x"].toDouble(0),o["y"].toDouble(0),o["w"].toDouble(1),o["h"].toDouble(1)};
    for (double v : values) if (!std::isfinite(v) || v<0 || v>1) return false;
    if (values[2]<.01 || values[3]<.01 || values[0]+values[2]>1.001 || values[1]+values[3]>1.001) return false;
    layer.rect={values[0],values[1],values[2],values[3]}; return true;
}
QVariant ChatMessageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row()<0 || index.row()>=rows_.size()) return {};
    const auto &m=rows_[index.row()];
    if (role==Qt::UserRole) return m;
    if (role==Qt::DisplayRole || role==Qt::AccessibleTextRole) {
        auto text=m["chatname"].toString()+" · "+m["type"].toString();
        const auto support=chatSupportLabel(m);if(!support.isEmpty())text+=" · "+support;
        return text+"\n"+m["chatmessage"].toString();
    }
    return {};
}
void ChatMessageModel::append(QJsonObject message) {
    appendBatch({message});
}
void ChatMessageModel::appendBatch(const QList<QJsonObject> &messages) {
    if(messages.isEmpty())return;
    QList<QJsonObject> batch;
    for(auto message:messages.last(std::min<qsizetype>(messages.size(),2000))) {
        const auto meta=message["meta"].toObject();const auto streak=meta["tiktokGiftStreakId"].toString();
        bool replaced=false;
        if(message["type"]=="tiktok"&&message["event"]=="gift"&&!streak.isEmpty()) {
            auto update=[&](QList<QJsonObject> &items,bool visible){
                for(int i=0;i<items.size();++i)if(items[i]["type"]=="tiktok"&&items[i]["event"]=="gift"&&items[i]["channel"]==message["channel"]&&items[i]["capture_epoch"]==message["capture_epoch"]&&items[i]["meta"].toObject()["tiktokGiftStreakId"].toString()==streak) {
                    if(meta["tiktokGiftCount"].toDouble()>=items[i]["meta"].toObject()["tiktokGiftCount"].toDouble()) {
                        message["id"]=items[i]["id"];if(items[i].contains("saved"))message["saved"]=items[i]["saved"];items[i]=message;
                        if(visible)emit dataChanged(index(i),index(i));
                    }
                    return true;
                }return false;
            };
            replaced=update(rows_,true)||update(pending_,false)||update(batch,false);
        }
        if(!replaced)batch.append(message);
    }
    if(batch.isEmpty())return;
    if(paused_){pending_.append(batch);if(pending_.size()>2000)pending_.remove(0,pending_.size()-2000);return;}
    const int remove=int(std::max<qsizetype>(0,rows_.size()+batch.size()-2000));
    if(remove){beginRemoveRows({},0,remove-1);rows_.remove(0,remove);endRemoveRows();}
    const int row=int(rows_.size());beginInsertRows({},row,row+int(batch.size())-1);rows_.append(batch);endInsertRows();
}
void ChatMessageModel::setPaused(bool paused) {
    paused_=paused; if (paused) return;
    appendBatch(std::exchange(pending_,{}));
}
QJsonObject ChatMessageModel::message(const QString &id) const { for (const auto &m : rows_) if (m["id"].toString()==id) return m; return {}; }
QJsonArray ChatMessageModel::messages() const { QJsonArray result; for (const auto &m : rows_) result.append(m); return result; }
void ChatMessageModel::toggleSaved(const QString &id) { for (int i=0;i<rows_.size();++i) if (rows_[i]["id"].toString()==id) { rows_[i]["saved"]=!rows_[i]["saved"].toBool(); emit dataChanged(index(i),index(i)); return; } }
void ChatMessageModel::clearPractice() {
    beginResetModel();rows_.removeIf([](const QJsonObject &m){return m["practice"].toBool();});
    pending_.removeIf([](const QJsonObject &m){return m["practice"].toBool();});endResetModel();
}
void ChatMessageModel::removeMatching(const QJsonObject &deletion) {
    pending_.removeIf([&](const QJsonObject &m){return chatMatchesDeletion(m,deletion);});
    for(int i=int(rows_.size())-1;i>=0;--i)if(chatMatchesDeletion(rows_[i],deletion)) {
        beginRemoveRows({},i,i);rows_.removeAt(i);endRemoveRows();
    }
}
}
