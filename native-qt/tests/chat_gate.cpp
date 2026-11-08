#include "chat_data.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QItemSelectionModel>
#include <QSignalSpy>
using namespace versus::mixer;
class ChatGate : public QObject {
    Q_OBJECT
    QJsonObject manifest;
private slots:
    void initTestCase() {
        QFile file(QCoreApplication::applicationDirPath()+"/ssn/manifest.json");
        QVERIFY2(file.open(QIODevice::ReadOnly),"Run sync-ssn-pack.ps1 before the chat gates");
        manifest=QJsonDocument::fromJson(file.readAll()).object();QVERIFY(!manifest.isEmpty());
        for(const auto &entry:manifest["content_scripts"].toArray())for(const auto &script:entry.toObject()["js"].toArray())
            QVERIFY2(QFile::exists(QCoreApplication::applicationDirPath()+"/ssn/"+script.toString()),qPrintable(script.toString()));
    }
    void links_data() {
        QTest::addColumn<QString>("input");QTest::addColumn<QString>("platform");QTest::addColumn<QString>("path");
        QTest::newRow("video")<<QString("https://www.youtube.com/watch?v=abcdefghijk")<<QString("YouTube")<<QString("/live_chat");
        QTest::newRow("short-link")<<QString("https://youtu.be/abcdefghijk")<<QString("YouTube")<<QString("/live_chat");
        QTest::newRow("handle")<<QString("@neonorbit")<<QString("YouTube")<<QString("/@neonorbit/live");
        QTest::newRow("channel")<<QString("https://youtube.com/channel/UC123")<<QString("YouTube")<<QString("/channel/UC123/live");
        QTest::newRow("twitch-name")<<QString("neonorbit")<<QString("Twitch")<<QString("/popout/neonorbit/chat");
        QTest::newRow("twitch-chat")<<QString("https://www.twitch.tv/popout/neonorbit/chat")<<QString("Twitch")<<QString("/popout/neonorbit/chat");
        QTest::newRow("kick")<<QString("https://kick.com/neonorbit")<<QString("Kick")<<QString("/popout/neonorbit/chat");
        QTest::newRow("kick-chat")<<QString("https://kick.com/popout/neonorbit/chat")<<QString("Kick")<<QString("/popout/neonorbit/chat");
        QTest::newRow("kick-old-chat")<<QString("https://kick.com/neonorbit/chatroom")<<QString("Kick")<<QString("/popout/neonorbit/chat");
        QTest::newRow("tiktok")<<QString("https://www.tiktok.com/@neonorbit")<<QString("TikTok")<<QString("/@neonorbit/live");
        QTest::newRow("facebook")<<QString("https://www.facebook.com/neonorbit/live")<<QString("Facebook")<<QString("/neonorbit/live");
        QTest::newRow("instagram")<<QString("https://www.instagram.com/neonorbit/live")<<QString("Instagram")<<QString("/neonorbit/live");
        QTest::newRow("x")<<QString("https://x.com/neonorbit")<<QString("X")<<QString("/neonorbit/livechat");
        QTest::newRow("rumble")<<QString("https://rumble.com/c/NeonOrbit")<<QString("Rumble")<<QString("/c/NeonOrbit/live");
        QTest::newRow("rumble-chat")<<QString("https://rumble.com/chat/popup/123456789")<<QString("Rumble")<<QString("/chat/popup/123456789");
    }
    void links() {QFETCH(QString,input);QFETCH(QString,platform);QFETCH(QString,path);QString error;auto c=detectChatChannel(input,manifest,&error);QVERIFY2(!c.isEmpty(),qPrintable(error));QCOMPARE(c["platform"].toString(),platform);QCOMPARE(QUrl(c["url"].toString()).path(),path);}
    void rejectedLinks() {
        for(const auto &input:QStringList{"https://youtube.com.evil.test/@name","https://user:secret@twitch.tv/name","javascript:alert(1)","https://example.org/chat","https://www.twitch.tv/name?token=secret","https://www.youtube.com/watch?v=short","https://rumble.com/v123-chat.html"}) {
            QString error;QVERIFY(detectChatChannel(input,manifest,&error).isEmpty());QVERIFY(!error.isEmpty());
        }
        QVERIFY(chatUrlMatches("*://*.twitch.tv/*",QUrl("https://www.twitch.tv/a")));
        QVERIFY(!chatUrlMatches("*://*.twitch.tv/*",QUrl("https://twitch.tv.evil.test/a")));
    }
    void replyText() {
        QCOMPARE(chatCommentText({{"chatmessage","hello"}}),QString("hello"));
        QCOMPARE(chatCommentText({{"chatmessage","Replying to @Ann: long quote:  hi"},{"initial","Replying to @Ann: long quote"},{"reply","hi"}}),QString("@Ann hi"));
        QCOMPARE(chatCommentText({{"chatmessage","x"},{"initial","mail me@site: more"},{"reply","hi"}}),QString("hi"));
        QCOMPARE(chatCommentText({{"chatmessage","x"},{"initial","Replying to @Ann"},{"reply","@Ann hi"}}),QString("@Ann hi"));
        QCOMPARE(chatCommentText({{"chatmessage","x"},{"initial","Replying to @Ann"},{"reply","@Anna hi"}}),QString("@Ann @Anna hi"));
        QCOMPARE(chatCommentText({{"chatmessage","x"},{"initial","Replying to @Ann"},{"reply","@Ann, hi"}}),QString("@Ann, hi"));
        QCOMPARE(chatCommentText({{"chatmessage","x"},{"initial","no target here"},{"reply","hi"}}),QString("hi"));
        QCOMPARE(chatCommentText({{"chatmessage","kept"},{"initial","Replying to @Ann"},{"reply","  "}}),QString("kept"));
    }
    void settingsRoundtrip() {
        QTemporaryDir dir(QDir(studioDataPath()).filePath("settings-gate-XXXXXX"));QVERIFY(dir.isValid());
        ChatSettings a,b;QString error;const auto path=dir.path()+"/chat.json";QVERIFY(a.load(path,&error));
        a.practice=true;a.channels.append(detectChatChannel("neonorbit",manifest,&error));QVERIFY(a.save(path,&error));QVERIFY(b.load(path,&error));
        QCOMPARE(a.channels,b.channels);QCOMPARE(a.room,b.room);QVERIFY(b.practice);
        QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("{broken");f.close();QVERIFY(!b.load(path,&error));
    }
    void reservedAndUnsafeInputs() {
        for(const auto &input:QStringList{"https://www.twitch.tv/directory","https://kick.com/categories","https://instagram.com/explore","https://x.com/home","https://facebook.com/watch","https://youtube.com/feed/subscriptions","javascript:alert(1)","file:///C:/temp/a.html","https://127.0.0.1/chat","https://[::1]/chat","https://localhost/chat","https://user:pass@twitch.tv/name","hello world!",""}) {
            QString error;QVERIFY2(detectChatChannel(input,manifest,&error).isEmpty(),qPrintable(input));QVERIFY(!error.isEmpty());
            if(input=="hello world!"||input.isEmpty())QCOMPARE(error,QString("Paste your channel link, or type your channel name."));
        }
    }
    void platformPageStates() {
        QCOMPARE(chatPageStatus({{"notFound",true}}),QString("We could not find that channel. Check the link."));
        QCOMPARE(chatPageStatus({{"login",true}}),QString("This channel needs sign-in. Open Accounts to continue."));
        QCOMPARE(chatPageStatus({{"ended",true}}),QString("Waiting for your stream"));
        QCOMPARE(chatPageStatus({{"offline",true}}),QString("Waiting for your stream"));
        QCOMPARE(chatPlatformName("youtube"),QString("YouTube"));
    }
    void authenticatedCaptureUrls() {
        QUrl twitch("https://www.twitch.tv/popout/neonorbit/chat");
        QVERIFY(chatCaptureAllowed(manifest,twitch,twitch,false));
        QVERIFY(!chatCaptureAllowed(manifest,twitch,twitch,true)); // Twitch is not all_frames.
        QVERIFY(!chatCaptureAllowed(manifest,twitch,QUrl("https://ads.example/chat"),true));
        QVERIFY(!chatCaptureAllowed(manifest,twitch,QUrl("https://www.youtube.com/live_chat?v=abcdefghijk"),true));
        QUrl ebay("https://www.ebay.com/ebaylive/events/123"),frame("https://ir.ebaystatic.com/cr/ebaylivepubweb/liveassets/shoplive/chat");
        QVERIFY(chatCaptureAllowed(manifest,ebay,frame,true));
        QVERIFY(!chatCaptureAllowed(manifest,ebay,QUrl("https://ir.ebaystatic.com/advertisement"),true));
    }
    void accountNavigationBoundaries() {
        QVERIFY(chatSignInAllowed("Twitch",QUrl("https://passport.twitch.tv/login")));
        QVERIFY(chatSignInAllowed("Instagram",QUrl("https://www.facebook.com/login/")));
        for(const auto &url:QStringList{"https://twitch.tv.evil.test/login","https://evil-twitch.tv/login","http://www.twitch.tv/login","https://user:secret@www.twitch.tv/login","https://www.twitch.tv:444/login","file:///C:/secret","javascript:alert(1)","https://accounts.google.com/o/oauth2/auth","https://www.instagram.com/accounts/login/"})QVERIFY2(!chatSignInAllowed("Twitch",QUrl(url)),qPrintable(url));
        QVERIFY(chatSignInUrl("YouTube").isEmpty());
        QVERIFY(chatSignInUrl("unknown").isEmpty());
        QVERIFY(!chatSignInAllowed("unknown",QUrl("https://example.com")));
    }
    void twitchChannelRedirects() {
        const QUrl saved("https://www.twitch.tv/popout/lirik/chat");
        for(const auto &url:QStringList{"https://www.twitch.tv/popout/lirik/chat?popout=", "https://www.twitch.tv/lirik/chat", "https://www.twitch.tv/embed/LIRIK/chat"})
            QVERIFY2(chatCaptureChannelMatches(saved,QUrl(url)),qPrintable(url));
        for(const auto &url:QStringList{"https://www.twitch.tv/popout/lirik_247/chat", "https://www.twitch.tv/lirik_247/chat", "https://www.twitch.tv/another", "https://www.twitch.tv/login", "https://twitch.tv.evil.test/popout/lirik/chat"})
            QVERIFY2(!chatCaptureChannelMatches(saved,QUrl(url)),qPrintable(url));
        // A second explicitly selected channel keeps its own capture scope.
        QVERIFY(chatCaptureChannelMatches(QUrl("https://www.twitch.tv/popout/lirik_247/chat"),QUrl("https://www.twitch.tv/lirik_247/chat")));
        // YouTube still resolves its channel URL to the next live-video chat.
        QVERIFY(chatCaptureChannelMatches(QUrl("https://www.youtube.com/@LofiGirl/live"),QUrl("https://www.youtube.com/live_chat?v=abcdefghijk")));
    }
    void giftStreakIsolation() {
        ChatMessageModel model;
        auto gift=[](QString id,QString channel,QString epoch,int count){return QJsonObject{{"id",id},{"channel",channel},{"capture_epoch",epoch},{"type","tiktok"},{"event","gift"},{"chatmessage",QString::number(count)},{"meta",QJsonObject{{"tiktokGiftStreakId","same-streak"},{"tiktokGiftCount",count}}}};};
        model.append(gift("first","one","session",1));model.toggleSaved("first");model.setPaused(true);
        model.append(gift("later","one","session",5));model.append(gift("stale","one","session",3));
        QCOMPARE(model.rowCount(),1);QCOMPARE(model.message("first")["chatmessage"].toString(),QString("5"));QVERIFY(model.message("first")["saved"].toBool());
        model.append(gift("other-channel","two","session",2));model.append(gift("other-session","one","new-session",2));
        QCOMPARE(model.pendingCount(),2);model.setPaused(false);QCOMPARE(model.rowCount(),3);
    }
    void supportEventClassification() {
        QVERIFY(!chatIsSupportEvent({{"type","twitch"},{"membership","SUBSCRIBER"},{"chatmessage","ordinary comment"}}));
        QVERIFY(chatIsSupportEvent({{"type","twitch"},{"membership","SUBSCRIBER"},{"event","subscription_gift"}}));
        QVERIFY(chatIsSupportEvent({{"type","youtube"},{"membership","6 months"},{"event","membermilestone"}}));
        QVERIFY(chatIsSupportEvent({{"type","tiktok"},{"event","gift"}}));
        QVERIFY(chatIsSupportEvent({{"hasDonation","100 KICKs"}}));
    }
    void verifiedPack() {
        QVERIFY(verifyChatPack(QCoreApplication::applicationDirPath()+"/ssn"));
        QTemporaryDir dir(QDir(studioDataPath()).filePath("pack-gate-XXXXXX"));QVERIFY(dir.isValid());
        const QString root=QCoreApplication::applicationDirPath()+"/ssn";
        QDirIterator it(root,QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()){auto file=it.next();auto target=dir.path()+"/"+QDir(root).relativeFilePath(file);QDir().mkpath(QFileInfo(target).absolutePath());QVERIFY(QFile::copy(file,target));}
        QVERIFY(verifyChatPack(dir.path()));
        QFile extra(dir.path()+"/stale.js");QVERIFY(extra.open(QIODevice::WriteOnly));extra.write("stale");extra.close();QVERIFY(!verifyChatPack(dir.path()));QVERIFY(extra.remove());
        QFile code(dir.path()+"/featured.html");QVERIFY(code.open(QIODevice::Append));code.write("tampered");code.close();QVERIFY(!verifyChatPack(dir.path()));
    }
    void batchedModel() {
        ChatMessageModel model;QSignalSpy inserts(&model,&QAbstractItemModel::rowsInserted);
        QList<QJsonObject> batch;for(int i=0;i<50;++i)batch.append({{"id",QString::number(i)},{"chatmessage","burst"}});
        model.appendBatch(batch);QCOMPARE(model.rowCount(),50);QCOMPARE(inserts.count(),1);
        model.setPaused(true);model.appendBatch(batch);QCOMPARE(model.pendingCount(),50);model.setPaused(false);QCOMPARE(inserts.count(),2);QCOMPARE(model.pendingCount(),0);
    }
    void accessibleSelectionAndDeselect() {
        ChatMessageModel model;model.append({{"id","one"},{"chatmessage","hello"}});
        QItemSelectionModel selection(&model);QString selected;
        watchChatSelection(&selection,this,[&](QString id){selected=id;model.setPaused(!id.isEmpty());});
        selection.select(model.index(0),QItemSelectionModel::ClearAndSelect); // UIA Select does not set current.
        QVERIFY(!selection.currentIndex().isValid());QCOMPARE(selected,QString("one"));QVERIFY(model.paused());
        model.append({{"id","two"},{"chatmessage","buffered"}});QCOMPARE(model.pendingCount(),1);
        selection.clearSelection();QVERIFY(selected.isEmpty());QVERIFY(!model.paused());QCOMPARE(model.pendingCount(),0);QCOMPARE(model.rowCount(),2);
    }
    void showLayerRoundtrip() {
        CommentsLayer layer;QVERIFY(CommentsLayer::read(QJsonValue(QJsonValue::Undefined),layer));QCOMPARE(layer.style,QString("bar"));
        layer.style="bubble";layer.enabled=false;layer.rect={.1,.2,.8,.7};auto saved=QJsonDocument(QJsonObject{{"version",1},{"comments",layer.json()}}).toJson();
        CommentsLayer loaded;QVERIFY(CommentsLayer::read(QJsonDocument::fromJson(saved).object()["comments"],loaded));QCOMPARE(loaded.json(),layer.json());
        QVERIFY(!CommentsLayer::read(QJsonObject{{"style","unknown"}},loaded));
        QVERIFY(!CommentsLayer::read(QJsonObject{{"x",.8},{"w",.5}},loaded));
    }
    void boundedPausedModel() {
        ChatMessageModel model;
        for(int i=0;i<2000;++i)model.append({{"id",QString::number(i)},{"chatmessage","<img src=x onerror=alert(1)>"}});
        QPersistentModelIndex selected=model.index(1500);const auto before=selected.data(Qt::UserRole);
        model.setPaused(true);QSignalSpy inserted(&model,&QAbstractItemModel::rowsInserted);
        for(int i=2000;i<5000;++i)model.append({{"id",QString::number(i)},{"chatmessage","new"}});
        QCOMPARE(model.rowCount(),2000);QCOMPARE(model.pendingCount(),2000);QCOMPARE(inserted.count(),0);QCOMPARE(selected.data(Qt::UserRole),before);
        QVERIFY(model.index(0).data().toString().contains("<img"));model.toggleSaved("1500");QVERIFY(model.message("1500")["saved"].toBool());
        model.setPaused(false);QCOMPARE(model.rowCount(),2000);QCOMPARE(model.pendingCount(),0);QCOMPARE(model.index(1999).data(Qt::UserRole).toJsonObject()["id"].toString(),QString("4999"));
    }
};
QTEST_GUILESS_MAIN(ChatGate)
#include "chat_gate.moc"
