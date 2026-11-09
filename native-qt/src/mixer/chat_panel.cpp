#include "chat_panel.h"
#include "chat_avatars.h"
#include "studio_icons.h"
#include "versus/ui/studio_preferences.h"
#include <QButtonGroup>
#include <QApplication>
#include <QProgressBar>
#include <QScopedValueRollback>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QToolButton>
#include <QResizeEvent>
#include <QTextLayout>
#include <QUuid>

namespace versus::mixer {
using ui::studioText;
class ChannelChip : public QToolButton {
public:
    using QToolButton::QToolButton;
    QString label;
    void updateLabel() {setText(fontMetrics().elidedText(label,Qt::ElideRight,std::max(0,width()-44)));}
    QSize minimumSizeHint() const override {return {0,32};}
protected:
    void resizeEvent(QResizeEvent *event) override {QToolButton::resizeEvent(event);updateLabel();}
};
static int wrappedText(const QString &text,const QFont &font,int width,QPainter *painter=nullptr,QPointF origin={}) {
    auto paragraphs=text;paragraphs.replace('\n',QChar::LineSeparator);
    QTextLayout layout(paragraphs,font);QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);layout.setTextOption(option);
    qreal height=0;layout.beginLayout();
    for(;;){auto line=layout.createLine();if(!line.isValid())break;line.setLineWidth(std::max(1,width));line.setPosition(QPointF(0,height));height+=line.height();}
    layout.endLayout();if(painter)layout.draw(painter,origin);return int(height+1);
}
class ChatFilter : public QSortFilterProxyModel {
public:
    QString query, mode="all";
    QSet<QString> hidden;
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void refresh() { invalidateFilter(); }
protected:
    bool filterAcceptsRow(int row,const QModelIndex &parent) const override {
        const auto m=sourceModel()->index(row,0,parent).data(Qt::UserRole).toJsonObject();
        return !hidden.contains(m["channel"].toString()) && (mode!="saved"||m["saved"].toBool()) &&
            (mode!="thanks"||chatIsSupportEvent(m)) &&
            (query.isEmpty()||(m["chatname"].toString()+" "+m["chatmessage"].toString()+" "+chatSupportLabel(m)).contains(query,Qt::CaseInsensitive));
    }
};
class ChatDelegate : public QStyledItemDelegate {
    ChatAvatarCache *avatars_;
public:
    ChatDelegate(ChatAvatarCache *avatars,QObject *parent):QStyledItemDelegate(parent),avatars_(avatars){}
    QSize sizeHint(const QStyleOptionViewItem &o,const QModelIndex &) const override { return {280,std::max(60,o.fontMetrics.height()*3+10)}; }
    void paint(QPainter *p,const QStyleOptionViewItem &o,const QModelIndex &index) const override {
        const auto m=index.data(Qt::UserRole).toJsonObject();p->save();p->setClipRect(o.rect);p->setRenderHint(QPainter::Antialiasing);
        const auto bg=o.palette.color(QPalette::Base),fg=o.palette.color(QPalette::Text);p->fillRect(o.rect,bg);
        if(o.state&QStyle::State_Selected){auto tint=QColor("#3989c9");tint.setAlpha(45);p->fillRect(o.rect,tint);p->setPen(QPen(bg.lightness()<128?QColor("#9fd2f7"):QColor("#125e97"),2));p->setBrush(Qt::NoBrush);p->drawRoundedRect(o.rect.adjusted(1,1,-1,-1),3,3);}
        const auto type=chatPlatformName(m["type"].toString());
        QColor accent=type=="YouTube"?QColor("#ffb3a8"):type=="Twitch"?QColor("#d3bcff"):QColor("#8fe8e6");
        const auto avatar=QRect(o.rect.left()+8,o.rect.top()+9,28,28);p->setPen(Qt::NoPen);p->setBrush(accent);p->drawEllipse(avatar);
        auto font=o.font;font.setBold(true);p->setFont(font);p->setPen(QColor("#10202e"));p->drawText(avatar,Qt::AlignCenter,m["chatname"].toString().left(1).toUpper());
        const auto picture=avatars_->picture(m["chatimg"].toString());
        if(!picture.isNull()){p->save();QPainterPath circle;circle.addEllipse(avatar);p->setClipPath(circle,Qt::IntersectClip);p->setRenderHint(QPainter::SmoothPixmapTransform);p->drawPixmap(avatar,picture);p->restore();}
        auto r=o.rect.adjusted(44,5,-8,-5);const int line=o.fontMetrics.height();
        auto small=o.font;small.setPointSizeF(std::max(7.0,small.pointSizeF()-1));
        const auto amount=chatSupportLabel(m);const int platformWidth=QFontMetrics(small).horizontalAdvance(type)+8;
        const int amountWidth=amount.isEmpty()?0:std::min(75,QFontMetrics(small).horizontalAdvance(amount)+10);
        const int nameWidth=std::max(24,r.width()-platformWidth-amountWidth-6);
        p->setPen(fg);p->drawText(QRect(r.left(),r.top(),nameWidth,line),Qt::AlignVCenter,p->fontMetrics().elidedText(m["chatname"].toString(),Qt::ElideRight,nameWidth));
        p->setFont(small);p->setPen(bg.lightness()<128?accent:accent.darker(270));p->drawText(QRect(r.left()+nameWidth+4,r.top(),platformWidth,line),Qt::AlignVCenter,type);
        if(amountWidth){QRect chip(r.right()-amountWidth,r.top(),amountWidth,line);p->setPen(Qt::NoPen);p->setBrush(QColor("#ffb454"));p->drawRoundedRect(chip,3,3);p->setPen(QColor("#10202e"));p->drawText(chip.adjusted(4,0,-4,0),Qt::AlignCenter,p->fontMetrics().elidedText(amount,Qt::ElideRight,chip.width()-8));}
        p->setFont(o.font);p->setPen(fg);r.setTop(r.top()+line+1);
        const bool onAir=o.widget->property("onAirId").toString()==m["id"].toString();
        if(m["saved"].toBool()||onAir){p->drawText(QRect(r.left(),r.top(),r.width(),line),Qt::AlignVCenter,p->fontMetrics().elidedText(m["chatmessage"].toString(),Qt::ElideRight,r.width()));small.setBold(true);p->setFont(small);p->drawText(r.adjusted(0,line,0,0),Qt::AlignTop,(m["saved"].toBool()?studioText("Saved")+"   ":QString())+(onAir?studioText("ON SCREEN"):QString()));}
        else wrappedText(m["chatmessage"].toString(),o.font,r.width(),p,r.topLeft());p->restore();
    }
};
class ChatCard : public QWidget {
public:
    QJsonObject message;QString style="bar";
    using QWidget::QWidget;
    int supportHeight() const {const auto text=chatSupportLabel(message);return text.isEmpty()?0:wrappedText(text,font(),width()-24)+6;}
    void fitContent() {
        auto f=font();if(style=="big")f.setPointSizeF(f.pointSizeF()*1.25);
        const int textHeight=wrappedText(message["chatmessage"].toString(),f,width()-24);
        setMinimumHeight(std::max(92,textHeight+44+supportHeight()));
    }
protected:
    void resizeEvent(QResizeEvent *event) override {QWidget::resizeEvent(event);fitContent();}
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const bool bubble=style=="bubble";p.setBrush(bubble?QColor(Qt::white):QColor("#0d1620"));p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect().adjusted(2,2,-2,-2),bubble?22:6,bubble?22:6);
        p.setPen(bubble?QColor("#51606e"):QColor("#ffb454"));auto f=font();f.setBold(true);p.setFont(f);
        const auto heading=message["chatname"].toString()+" · "+chatPlatformName(message["type"].toString());
        p.drawText(rect().adjusted(12,8,-12,-8),Qt::AlignTop|Qt::TextSingleLine,p.fontMetrics().elidedText(heading,Qt::ElideRight,std::max(0,width()-24)));
        f.setBold(false);p.setFont(f);p.setPen(bubble?QColor("#725018"):QColor("#ffcf86"));
        if(supportHeight())wrappedText(chatSupportLabel(message),f,width()-24,&p,QPointF(12,32));
        if(style=="big")f.setPointSizeF(f.pointSizeF()*1.25);p.setFont(f);p.setPen(bubble?QColor("#10202e"):QColor(Qt::white));
        wrappedText(message["chatmessage"].toString(),f,width()-24,&p,QPointF(12,32+supportHeight()));
    }
};
static QLabel *plainLabel(QWidget *parent) { auto label=new QLabel(parent);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setMinimumWidth(0);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Maximum);return label; }
ChatPanel::ChatPanel(QWidget *parent) : QWidget(parent),engine(this) {
    setObjectName("chatPanel");setMinimumWidth(300);setMaximumWidth(360);
    setStyleSheet("QWidget#chatPanel QPushButton, QWidget#chatPanel QToolButton { background:#192b3d; color:#eaf4ff; border:1px solid #304358; border-radius:3px; padding:5px 8px; } QWidget#chatPanel QPushButton:checked, QWidget#chatPanel QToolButton:checked { background:#16435a; color:#eaf4ff; } QWidget#chatPanel QPushButton#showComment { background:#9fd2f7; color:#10202e; font-weight:bold; } QWidget#chatPanel QPushButton#showComment:disabled { background:palette(button); color:palette(mid); }");
    auto root=new QVBoxLayout(this);root->setContentsMargins(12,10,12,10);root->setSpacing(8);
    auto *connections=new QHBoxLayout;root->addLayout(connections);
    addMore_=new QPushButton(studioText("+ Add"),this);connections->addWidget(addMore_,1);
    auto *accounts=new QPushButton(studioText("Accounts"),this);accounts->setAccessibleName(studioText("Chat accounts"));accounts->setToolTip(studioText("Sign in to a connected channel's platform, or clear saved chat account sessions."));connections->addWidget(accounts);
    auto *accountMenu=new QMenu(accounts);accounts->setMenu(accountMenu);
    connect(accountMenu,&QMenu::aboutToShow,this,[this,accountMenu]{
        accountMenu->clear();QSet<QString> platforms;
        for(const auto &value:engine.settings.channels){const auto channel=value.toObject();const auto platform=channel["platform"].toString();if(platforms.contains(platform))continue;platforms.insert(platform);
            auto *action=accountMenu->addAction(studioText("Sign in to ")+platform+"...",this,[this,channel]{const auto result=command({{"command","chat.signin"},{"id",channel["id"]}});if(result.contains("error"))QMessageBox::information(this,studioText("Chat account"),result["error"].toString());});action->setEnabled(engine.health["ready"].toBool());
        }
        if(platforms.isEmpty()){auto *empty=accountMenu->addAction(studioText("Add a channel first"));empty->setEnabled(false);}
        accountMenu->addSeparator();auto *clear=accountMenu->addAction(studioText("Sign out of all chat accounts..."),this,[this]{
            if(QMessageBox::question(this,studioText("Sign out of chat accounts"),studioText("Clear saved sign-in sessions for all chat platforms on this device? Your channels and show are kept."),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Yes)command({{"command","chat.signout"},{"confirmed",true}});
        });clear->setEnabled(!platforms.isEmpty()&&engine.health["ready"].toBool()&&!engine.health["account"].toObject()["clearing"].toBool());
    });
    connect(addMore_,&QPushButton::clicked,this,[this]{setupOpen_=!setupOpen_;refresh();if(setupOpen_)input_->setFocus();});
    setup_=new QWidget(this);auto setup=new QVBoxLayout(setup_);setup->setContentsMargins(0,0,0,0);root->addWidget(setup_);
    auto title=plainLabel(this);title->setText(studioText("See your comments here"));auto titleFont=title->font();titleFont.setPointSize(15);titleFont.setBold(true);title->setFont(titleFont);setup->addWidget(title);
    input_=new QLineEdit(this);input_->setPlaceholderText(studioText("Channel link or @YouTube handle"));input_->setAccessibleName(studioText("Your channel link or name"));input_->setMinimumHeight(38);input_->setToolTip(studioText("A plain channel name connects to Twitch. Use a link for Kick. After adding a channel, open Accounts if sign-in is needed."));setup->addWidget(input_);
    auto add=new QPushButton(studioText("Add this channel"),this);setup->addWidget(add);
    connect(add,&QPushButton::clicked,this,[this]{if(input_->text().trimmed().isEmpty())return;auto result=engine.addChannel(input_->text());if(result.contains("error"))hint_->setText(studioText(result["error"].toString().toUtf8().constData()));else{setupOpen_=false;input_->clear();hint_->setText(studioText("Comments will appear here as soon as they arrive."));}refresh();});
    connect(input_,&QLineEdit::returnPressed,add,&QPushButton::click);
    hint_=plainLabel(this);hint_->setText(studioText("Twitch, YouTube and Kick. Plain names use Twitch; @handles use YouTube. Other platforms are experimental."));setup->addWidget(hint_);
    feedControls_=new QWidget(this);auto feedControls=new QVBoxLayout(feedControls_);feedControls->setContentsMargins(0,0,0,0);feedControls->setSpacing(8);root->addWidget(feedControls_);
    auto chipsScroll=new QScrollArea(this);chipsScroll_=chipsScroll;chipsScroll->setWidgetResizable(true);chipsScroll->setFrameShape(QFrame::NoFrame);chipsScroll->setAccessibleName(studioText("Connected channels"));
    chipsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto chipsWidget=new QWidget(chipsScroll);chips_=new QVBoxLayout(chipsWidget);chips_->setContentsMargins(0,0,0,0);chips_->setSpacing(4);chips_->setAlignment(Qt::AlignTop);chipsScroll->setWidget(chipsWidget);feedControls->addWidget(chipsScroll);
    connect(qApp,&QApplication::focusChanged,this,[this](QWidget *,QWidget *focused){
        if(focused&&chipsScroll_->isAncestorOf(focused))chipsScroll_->ensureWidgetVisible(focused,0,0);
    });
    auto filters=new QHBoxLayout;feedControls->addLayout(filters);
    filter_=new ChatFilter(this);filter_->setSourceModel(&engine.model);
    for(const auto &mode:QStringList{"All","Saved","Thanks"}) {
        auto b=new QPushButton(studioText(mode.toUtf8().constData()),this);b->setCheckable(true);b->setChecked(mode=="All");filters->addWidget(b);
        connect(b,&QPushButton::clicked,this,[this,b,mode,filters]{filter_->mode=mode.toLower();filter_->refresh();for(int i=0;i<filters->count();++i)if(auto other=qobject_cast<QPushButton*>(filters->itemAt(i)->widget()))other->setChecked(other==b);});
    }
    search_=new QLineEdit(this);search_->setPlaceholderText(studioText("Search"));search_->setAccessibleName(studioText("Search comments"));search_->setClearButtonEnabled(true);feedControls->addWidget(search_);
    connect(search_,&QLineEdit::textChanged,this,[this](QString text){filter_->query=text;filter_->refresh();});
    list_=new QListView(this);list_->setObjectName("chatMessages");list_->setAccessibleName(studioText("Comments"));list_->setModel(filter_);
    avatars_=new ChatAvatarCache(list_);avatars_->updated=[this]{list_->viewport()->update();};list_->setItemDelegate(new ChatDelegate(avatars_,list_));
    list_->setUniformItemSizes(true);list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list_->setMinimumHeight(56);list_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);
    auto listSlot=new QGridLayout;listSlot->setContentsMargins(0,0,0,0);listSlot->addWidget(list_,0,0);root->addLayout(listSlot,1);
    // A sibling remains accessible; Qt's list provider only exposes rows inside its viewport.
    empty_=plainLabel(this);empty_->setAlignment(Qt::AlignCenter);empty_->setAttribute(Qt::WA_TransparentForMouseEvents);empty_->setContentsMargins(16,16,16,16);listSlot->addWidget(empty_,0,0,Qt::AlignVCenter);
    watchChatSelection(list_->selectionModel(),this,[this](const QString &id){if(syncingSelection_)return;if(id.isEmpty())jump();else select(id);});
    // Layout and model insertions also move the scrollbar. Only a user's scroll
    // action may freeze the feed; otherwise deselection immediately pauses it again.
    connect(list_->verticalScrollBar(),&QScrollBar::actionTriggered,this,[this](int){if(!syncingSelection_&&list_->verticalScrollBar()->sliderPosition()<list_->verticalScrollBar()->maximum())engine.model.setPaused(true);});
    connect(list_->verticalScrollBar(),&QScrollBar::sliderPressed,this,[this]{engine.model.setPaused(true);});
    connect(&engine.model,&QAbstractItemModel::rowsInserted,this,[this]{if(!engine.model.paused())QTimer::singleShot(0,list_,&QListView::scrollToBottom);});
    new_=new QPushButton(this);root->addWidget(new_);connect(new_,&QPushButton::clicked,this,&ChatPanel::jump);
    airBand_=new QWidget(this);airBand_->setObjectName("chatAirBand");airBand_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);auto airLayout=new QVBoxLayout(airBand_);airLayout->setContentsMargins(10,8,10,8);airLayout->setSpacing(6);root->addWidget(airBand_);
    air_=plainLabel(airBand_);auto bold=air_->font();bold.setBold(true);air_->setFont(bold);airLayout->addWidget(air_);
    airComment_=plainLabel(airBand_);airComment_->setWordWrap(false);airLayout->addWidget(airComment_);
    auto airButtons=new QHBoxLayout;airLayout->addLayout(airButtons);
    keep_=new QPushButton(studioText("Keep up"),airBand_);hide_=new QPushButton(studioText("Hide"),airBand_);airButtons->addWidget(keep_);airButtons->addWidget(hide_);
    countdown_=new QProgressBar(airBand_);countdown_->setRange(0,10000);countdown_->setTextVisible(false);countdown_->setFixedHeight(4);airLayout->addWidget(countdown_);
    connect(hide_,&QPushButton::clicked,this,&ChatPanel::hideComment);
    connect(keep_,&QPushButton::clicked,this,[this]{pinned_=!pinned_;remainingMs_=10000;refresh();});
    tray_=new QWidget(this);tray_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);auto tray=new QVBoxLayout(tray_);tray->setContentsMargins(0,0,0,0);root->addWidget(tray_);
    auto privateLabel=plainLabel(tray_);privateLabel->setText(studioText("Only you can see this"));tray->addWidget(privateLabel);
    auto styleRow=new QHBoxLayout;tray->addLayout(styleRow);styles_=new QButtonGroup(this);styles_->setExclusive(true);
    for(const auto &name:QStringList{"Bar","Bubble","Big"}) {auto button=new QPushButton(studioText(name.toUtf8().constData()),tray_);button->setCheckable(true);button->setProperty("chatStyle",name.toLower());button->setAccessibleName(studioText(name.toUtf8().constData()));styles_->addButton(button);styleRow->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,button]{layer.style=button->property("chatStyle").toString();card_->style=layer.style;card_->update();if(changed)changed();});}
    previewScroll_=new QScrollArea(tray_);previewScroll_->setWidgetResizable(true);previewScroll_->setFrameShape(QFrame::NoFrame);previewScroll_->setFixedHeight(110);previewScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);previewScroll_->setFocusPolicy(Qt::StrongFocus);previewScroll_->setAccessibleName(studioText("Private comment preview"));tray->addWidget(previewScroll_);
    card_=new ChatCard(previewScroll_);previewScroll_->setWidget(card_);
    auto actions=new QHBoxLayout;tray->addLayout(actions);
    show_=new QPushButton(studioText("Show on screen"),tray_);show_->setObjectName("showComment");show_->setMinimumHeight(44);actions->addWidget(show_,1);
    show_->setIcon(studioIcon(StudioIcon::Play,QColor("#173a50")));
    save_=new QPushButton(studioText("Save"),tray_);save_->setMinimumHeight(44);actions->addWidget(save_);
    connect(show_,&QPushButton::clicked,this,[this]{auto r=showComment();if(r.contains("error"))hint_->setText(studioText(r["error"].toString().toUtf8().constData()));});
    connect(save_,&QPushButton::clicked,this,[this]{engine.model.toggleSaved(selected_);refresh();});
    practiceBanner_=plainLabel(this);practiceBanner_->setText(studioText("Practice mode. These comments are pretend."));root->addWidget(practiceBanner_);
    notice_=plainLabel(this);root->addWidget(notice_);
    practice_=new QPushButton(studioText("Try it with pretend chat"),this);practice_->setMinimumHeight(44);setup->addWidget(practice_);setup->addStretch(1);
    connect(practice_,&QPushButton::clicked,this,[this]{practice(!engine.settings.practice);});
    auto advanced=new QToolButton(this);advanced->setAccessibleName(studioText("Advanced"));advanced->setToolTip(studioText("Chat settings, practice and comment position"));advanced->setIcon(studioIcon(StudioIcon::Settings));advanced->setPopupMode(QToolButton::InstantPopup);auto menu=new QMenu(advanced);advanced->setMenu(menu);connections->addWidget(advanced);advanced->setAutoRaise(true);
    practiceAction_=menu->addAction(studioText("Try it with pretend chat"),this,[this]{practice(!engine.settings.practice);});
    menu->addAction(studioText("Connect an existing Social Stream setup"),this,[this]{if(this->advanced)this->advanced();});
    menu->addAction(studioText("Comments layer position"),this,[this]{
        QDialog dialog(this);dialog.setWindowTitle(studioText("Comments layer position"));QFormLayout form(&dialog);QDoubleSpinBox values[4];
        const double current[]{layer.rect.x(),layer.rect.y(),layer.rect.width(),layer.rect.height()};const char *labels[]{"Left","Top","Width","Height"};
        for(int i=0;i<4;++i){values[i].setRange(i<2?0:1,100);values[i].setValue(current[i]*100);values[i].setSuffix("%");form.addRow(studioText(labels[i]),&values[i]);}
        auto error=plainLabel(&dialog);error->hide();form.addRow(error);
        QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form.addRow(&buttons);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{auto data=layer.json();const char *keys[]{"x","y","w","h"};for(int i=0;i<4;++i)data[keys[i]]=values[i].value()/100;CommentsLayer candidate;if(!CommentsLayer::read(data,candidate)){error->setText(studioText("Keep the Comments layer inside the video."));error->show();return;}layer=candidate;if(changed)changed();dialog.accept();});
        dialog.exec();
    });
    auto enter=new QShortcut(QKeySequence(Qt::Key_Return),list_);enter->setContext(Qt::WidgetShortcut);connect(enter,&QShortcut::activated,show_,&QPushButton::click);
    auto escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);escape->setContext(Qt::WidgetWithChildrenShortcut);connect(escape,&QShortcut::activated,this,&ChatPanel::hideComment);
    clock_.start();practiceClock_.start();connect(&engine,&ChatEngine::updated,this,&ChatPanel::refresh);
    connect(&engine,&ChatEngine::commentDeleted,this,[this](const QJsonObject &deletion){
        if(chatMatchesDeletion(onAir_,deletion)||chatMatchesDeletion(requested_,deletion))hideComment();
        if(chatMatchesDeletion(engine.model.message(selected_),deletion))jump();
    });
    connect(&timer_,&QTimer::timeout,this,[this]{
        const int elapsed=int(clock_.restart());if(!onAir_.isEmpty()&&!pinned_&&!privateBreak_){remainingMs_-=elapsed;if(remainingMs_<=0)hideComment();}
        if(engine.settings.practice&&engine.health["ready"].toBool()&&practiceClock_.elapsed()>2500){
            practiceClock_.restart();const QStringList names{"Test Viewer","Sample Fan","New Friend"},texts{"Wow, this is cool","Thanks for the great show!","Where are you streaming from?"};const int i=practiceIndex_++%3;
            engine.send({{"op","inject"},{"message",QJsonObject{{"chatname",names[i]},{"chatmessage",texts[i]},{"type",i==0?"twitch":"youtube"},{"hasDonation",i==1?"$5.00":""}}}});
        }
        refresh();
    });timer_.start(100);refresh();
}
void ChatPanel::select(const QString &id) {
    if(id.isEmpty()){jump();return;}if(engine.model.message(id).isEmpty())return;
    QScopedValueRollback guard(syncingSelection_,true);selected_=id;engine.model.setPaused(true);
    for(int row=0;row<filter_->rowCount();++row){const auto index=filter_->index(row,0);if(index.data(Qt::UserRole).toJsonObject()["id"]==id){list_->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect);break;}}refresh();
}
void ChatPanel::jump(){QScopedValueRollback guard(syncingSelection_,true);selected_.clear();list_->clearSelection();list_->setCurrentIndex({});engine.model.setPaused(false);list_->scrollToBottom();refresh();}
QJsonObject ChatPanel::showComment() {
    auto m=engine.model.message(selected_);if(m.isEmpty())return {{"error","Select a comment first."}};
    if(!engine.health["ready"].toBool())return {{"error","Chat is getting ready."}};
    if(!layer.enabled)return {{"error","Enable the Comments layer first."}};
    showClock_.start();frameReceivedMs_=-1;composedMs_=-1;waitingToken_=QUuid::createUuid().toString(QUuid::Id128);requested_=m;
    engine.send({{"op","show"},{"token",waitingToken_},{"message",m},{"style",layer.style}});refresh();return {{"ok",true},{"pending",true},{"token",waitingToken_}};
}
void ChatPanel::hideComment(){onAir_={};requested_={};waitingToken_.clear();token_.clear();image_={};composed_=false;remainingMs_=0;engine.send({{"op","hide"}});refresh();}
void ChatPanel::setPrivateBreak(bool enabled){privateBreak_=enabled;if(enabled)composed_=false;}
void ChatPanel::compose(QImage &frame,bool privateBreak) {
    setPrivateBreak(privateBreak);composed_=false;
    if(privateBreak || !layer.enabled || onAir_.isEmpty() || image_.isNull())return;
    QPainter painter(&frame);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto &r=layer.rect;painter.drawImage(QRectF(r.x()*frame.width(),r.y()*frame.height(),r.width()*frame.width(),r.height()*frame.height()),image_);composed_=true;if(composedMs_<0&&showClock_.isValid())composedMs_=showClock_.elapsed();
}
bool ChatPanel::confirmPracticeOutput(){
    if(!engine.settings.practice||practiceAcknowledged)return true;
    const bool yes=QMessageBox::warning(this,studioText("Practice comments can reach your viewers"),studioText("You are practising with pretend chat. Comments you show will be included in streaming and recording. Continue?"),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Yes;
    practiceAcknowledged=yes;return yes;
}
bool ChatPanel::practice(bool enabled,bool acknowledged) {
    if(enabled&&outputActive&&outputActive()&&!acknowledged){
        if(QMessageBox::warning(this,studioText("Practice comments can reach your viewers"),studioText("Output is running. Pretend comments you show will reach your output. Continue?"),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return false;
    }
    if(!engine.setPractice(enabled))return false;
    practiceAcknowledged=acknowledged;practiceClock_.restart();
    if(!enabled){if(onAir_.value("practice").toBool()||requested_.value("practice").toBool())hideComment();engine.model.clearPractice();jump();}
    refresh();return true;
}
QJsonObject ChatPanel::state() const {
    QJsonArray channels;for(const auto &v:engine.settings.channels){auto c=v.toObject();const auto name=engine.health["names"].toObject()[c["id"].toString()].toString();if(!name.isEmpty())c["name"]=name;c["status"]=engine.health["channels"].toObject()[c["id"].toString()].toString("Connecting to chat");channels.append(c);}
    return {{"available",engine.packAvailable},{"ready",engine.health["ready"].toBool()},{"problem",engine.settingsError.isEmpty()?engine.health["error"]:QJsonValue(engine.settingsError)},
        {"channels",channels},{"account",engine.health["account"]},{"avatars",avatars_->status()},{"page_resources",engine.health["pageResources"]},{"practice",engine.settings.practice},{"count",engine.model.rowCount()},{"pending_comments",engine.model.pendingCount()},
        {"selected",selected_},{"pending_show",waitingToken_},{"on_screen",composed_&&!privateBreak_&&layer.enabled&&!onAir_.isEmpty()},
        {"hidden_by_private_break",privateBreak_&&!onAir_.isEmpty()},{"on_air",onAir_},{"remaining_ms",remainingMs_},{"kept",pinned_},{"layer",layer.json()},
        {"oversized_messages",engine.health["oversizedMessages"].toInteger()},{"browser_pid",engine.health["browserPid"]},{"page_count",engine.health["pageCount"]},{"diagnostic",engine.health["diagnostic"]},{"resources",engine.health["resources"]},{"timing",QJsonObject{{"startup",engine.health["startup_timing"]},{"worker",engine.health["timing"]},{"frame_received_ms",frameReceivedMs_},{"composed_ms",composedMs_}}}};
}
QJsonObject ChatPanel::command(const QJsonObject &c) {
    const auto op=c["command"].toString();auto fail=[](QString e){return QJsonObject{{"error",e}};};
    if(op=="chat.status")return state();
    if(op=="chat.channel.add")return engine.addChannel(c["input"].toString());
    if(op=="chat.signin") {
        if(!engine.health["ready"].toBool())return fail("Wait for chat to finish connecting, then try Accounts again.");
        QJsonObject channel;for(const auto &v:engine.settings.channels)if(v.toObject()["id"]==c["id"])channel=v.toObject();
        if(channel.isEmpty())return fail("Add a channel before signing in.");
        if(chatSignInUrl(channel["platform"].toString()).isEmpty())return fail("YouTube public chat works without sign-in. Google account sign-in is not supported in Studio yet.");
        engine.send({{"op","signin"},{"channel",channel["id"]}});return {{"ok",true},{"pending",true}};
    }
    if(op=="chat.signout") {
        if(!c["confirmed"].toBool())return fail("Confirm sign-out for all chat accounts. Channels and shows will be kept.");
        if(!engine.health["ready"].toBool()||engine.settings.channels.isEmpty())return fail("Connect a channel before clearing saved account sessions.");
        engine.send({{"op","signout"}});return {{"ok",true},{"pending",true}};
    }
    if(op=="chat.channel.remove") {if(!engine.removeChannel(c["id"].toString()))return fail("Channel could not be removed.");}
    else if(op=="chat.practice.set") {
        if(c["enabled"].toBool()&&outputActive&&outputActive()&&!c["acknowledgeOutput"].toBool())return fail("Output is running. Pretend comments will reach output. Set acknowledgeOutput to true to continue.");
        if(!practice(c["enabled"].toBool(),c["acknowledgeOutput"].toBool()))return fail("Practice chat is unavailable.");
    } else if(op=="chat.messages")return {{"messages",engine.model.messages()},{"pending",engine.model.pendingCount()}};
    else if(op=="chat.select"){if(!c["id"].toString().isEmpty()&&engine.model.message(c["id"].toString()).isEmpty())return fail("Comment not found.");select(c["id"].toString());}
    else if(op=="chat.show") {
        if(c.contains("id")){if(engine.model.message(c["id"].toString()).isEmpty())return fail("Comment not found.");select(c["id"].toString());}
        if(c.contains("style")){if(!QStringList{"bar","bubble","big"}.contains(c["style"].toString()))return fail("Choose bar, bubble or big.");layer.style=c["style"].toString();if(changed)changed();}
        return showComment();
    } else if(op=="chat.hide")hideComment();
    else if(op=="chat.keep"){if(onAir_.isEmpty())return fail("No comment is on screen.");pinned_=c["enabled"].toBool(true);remainingMs_=10000;}
    else if(op=="chat.inject") {
        if(!engine.settings.practice || !engine.health["ready"].toBool())return fail("Injection is only available when practice chat is ready.");
        const auto m=c["message"].toObject();if(!m["chatmessage"].isString()||m["chatmessage"].toString().size()>8000||m["chatname"].toString().size()>256)return fail("Invalid practice comment.");
        engine.send({{"op","inject"},{"message",m}});return {{"ok",true},{"pending",true}};
    } else return fail("Unknown chat command.");
    refresh();return {{"ok",true},{"state",state()}};
}
void ChatPanel::refresh() {
    const bool compact=window()->height()<850;
    if(property("compactChat").isNull()||property("compactChat").toBool()!=compact){
        setProperty("compactChat",compact);layout()->setSpacing(compact?4:8);layout()->setContentsMargins(12,compact?6:10,12,compact?6:10);
        tray_->layout()->setSpacing(compact?4:8);feedControls_->layout()->setSpacing(compact?4:8);previewScroll_->setFixedHeight(compact?64:110);
    }
    setup_->setVisible(setupOpen_||(engine.settings.channels.isEmpty()&&!engine.settings.practice));
    const bool firstRun=engine.settings.channels.isEmpty()&&!engine.settings.practice;
    feedControls_->setVisible(!firstRun);list_->setVisible(!firstRun);practice_->setVisible(engine.settings.channels.isEmpty());practiceAction_->setVisible(!firstRun);
    chipsScroll_->setVisible(!engine.settings.channels.isEmpty());
    empty_->setVisible(!firstRun&&filter_->rowCount()==0);
    empty_->setText(!filter_->query.isEmpty()?studioText("No matching comments. Clear Search to see comments again."):filter_->mode=="saved"?studioText("No saved comments yet. Select a comment, then choose Save."):filter_->mode=="thanks"?studioText("Gifts, donations and membership messages will appear here."):studioText("Waiting for comments."));
    practiceAction_->setText(studioText(engine.settings.practice?"Stop practising":"Try it with pretend chat"));
    addMore_->setVisible(!engine.settings.channels.isEmpty()||engine.settings.practice);
    practiceBanner_->setVisible(engine.settings.practice);
    if(!token_.isEmpty()&&token_==engine.renderedToken()&&!engine.image().isNull())image_=engine.image();
    if(!waitingToken_.isEmpty()&&engine.renderedToken()==waitingToken_&&!engine.image().isNull()) {
        frameReceivedMs_=showClock_.elapsed();token_=waitingToken_;waitingToken_.clear();onAir_=requested_;requested_={};image_=engine.image();pinned_=false;remainingMs_=10000;clock_.restart();jump();
    }
    if(!waitingToken_.isEmpty()&&!engine.health["error"].toString().isEmpty()){waitingToken_.clear();requested_={};}
    new_->setVisible(engine.model.paused());new_->setText(engine.model.pendingCount()>0?studioText("%1 new comments").arg(engine.model.pendingCount()):studioText("Back to live"));new_->setAccessibleName(studioText("Back to live"));
    const auto m=engine.model.message(selected_);tray_->setVisible(!m.isEmpty());const bool newPreview=card_->message["id"]!=m["id"];card_->message=m;card_->style=layer.style;card_->fitContent();card_->update();
    if(newPreview)previewScroll_->verticalScrollBar()->setValue(0);
    const auto support=chatSupportLabel(m);const auto previewText=m["chatname"].toString()+"\n"+(support.isEmpty()?QString():support+"\n")+m["chatmessage"].toString();previewScroll_->setAccessibleDescription(previewText);card_->setAccessibleName(previewText);card_->setToolTip(previewText);
    for(auto button:styles_->buttons()){QSignalBlocker block(button);button->setChecked(button->property("chatStyle").toString()==layer.style);}
    save_->setText(studioText(m["saved"].toBool()?"Saved":"Save"));
    show_->setEnabled(engine.health["ready"].toBool()&&waitingToken_.isEmpty()&&layer.enabled);
    show_->setToolTip(layer.enabled?QString():studioText("Enable the Comments layer first."));
    airBand_->setVisible(!onAir_.isEmpty()||!waitingToken_.isEmpty());keep_->setVisible(!onAir_.isEmpty());hide_->setVisible(!onAir_.isEmpty()||!waitingToken_.isEmpty());
    const QString bandStyle=privateBreak_?"QWidget#chatAirBand { background:palette(button); color:palette(button-text); } QWidget#chatAirBand QLabel { color:palette(button-text); background:transparent; }":"QWidget#chatAirBand { background:#ffb454; color:#10202e; border-radius:3px; } QWidget#chatAirBand QLabel { color:#10202e; background:transparent; } QWidget#chatAirBand QPushButton { background:#10202e; color:white; border:0; } QWidget#chatAirBand QProgressBar { background:#c78639; border:0; } QWidget#chatAirBand QProgressBar::chunk { background:#10202e; }";
    if(airBand_->styleSheet()!=bandStyle)airBand_->setStyleSheet(bandStyle);
    keep_->setText(studioText(pinned_?"Use timer":"Keep up"));
    air_->setText(!waitingToken_.isEmpty()?studioText("Showing comment..."):privateBreak_?studioText("HIDDEN BY PRIVATE BREAK"):!composed_?studioText("Waiting for output"):pinned_?studioText("ON SCREEN"):studioText("ON SCREEN - hides in %1s").arg((remainingMs_+999)/1000));
    const auto airText=onAir_.value("chatname").toString()+": "+onAir_.value("chatmessage").toString();airComment_->setText(airComment_->fontMetrics().elidedText(airText,Qt::ElideRight,std::max(0,width()-48)));airComment_->setToolTip(airText);
    countdown_->setVisible(!pinned_&&!privateBreak_&&!onAir_.isEmpty());countdown_->setValue(std::max(0,remainingMs_));
    const auto airId=onAir_.value("id").toString();if(list_->property("onAirId").toString()!=airId){list_->setProperty("onAirId",airId);list_->viewport()->update();}
    QString notice;
    if(!engine.packAvailable)notice=studioText("Chat is unavailable. Reinstall Studio to restore its chat files.");
    else if(!engine.settingsError.isEmpty())notice=studioText(engine.settingsError.toUtf8().constData());
    else if(!engine.health["error"].toString().isEmpty())notice=studioText(engine.health["error"].toString().toUtf8().constData());
    else if(engine.health["oversizedMessages"].toInteger()>0)notice=studioText("Some chat data was too large to display. Comments may be missing.");
    else if(engine.model.rowCount()==0&&!engine.settings.channels.isEmpty())notice=studioText("Comments will appear here as soon as they arrive.");
    const auto connectionStates=engine.health["channels"].toObject();
    for(auto it=connectionStates.begin();it!=connectionStates.end();++it)if(it.value().toString().contains("sign-in")||it.value().toString().contains("could not")||it.value().toString().contains("unavailable"))notice=studioText(it.value().toString().toUtf8().constData());
    if(notice==studioText("Comments will appear here as soon as they arrive."))for(auto it=connectionStates.begin();it!=connectionStates.end();++it)
        if(it.value()=="Waiting for your stream")notice=studioText("Waiting for your stream");
    const auto account=engine.health["account"].toObject();
    if(account["open"].toBool()||account["clearing"].toBool()||notice.isEmpty())if(!account["notice"].toString().isEmpty())notice=account["notice"].toString();
    notice_->setText(notice);notice_->setVisible(!notice.isEmpty());practice_->setText(studioText(engine.settings.practice?"Stop practising":"Try it with pretend chat"));
    auto channels=state()["channels"].toArray();
    if(channels!=chipsState_) {
        // Connection status updates must not destroy a focused channel button.
        bool sameChannels=channels.size()==chipsState_.size();
        for(int i=0;sameChannels&&i<channels.size();++i)sameChannels=channels[i].toObject()["id"]==chipsState_[i].toObject()["id"];
        if(!sameChannels){while(auto item=chips_->takeAt(0)){delete item->widget();delete item;}}
        chipsScroll_->setFixedHeight(std::min(2,int(channels.size()))*36);
        for(int i=0;i<channels.size();++i){const auto ch=channels[i].toObject();const auto id=ch["id"].toString();const auto status=ch["status"].toString();
            ChannelChip *chip;QToolButton *remove;
            if(sameChannels){const auto row=chips_->itemAt(i)->widget();chip=static_cast<ChannelChip*>(row->findChild<QToolButton*>("channelFilter"));remove=row->findChild<QToolButton*>("removeChannel");}
            else {
                auto row=new QWidget(this);auto layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
                chip=new ChannelChip(row);chip->setObjectName("channelFilter");chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);chip->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);chip->setFixedHeight(32);chip->setCheckable(true);layout->addWidget(chip,1);
                remove=new QToolButton(row);remove->setObjectName("removeChannel");remove->setText(QString::fromUtf8("×"));remove->setFixedSize(28,32);layout->addWidget(remove);chips_->addWidget(row);
                connect(chip,&QToolButton::toggled,this,[this,id](bool on){if(on)filter_->hidden.remove(id);else filter_->hidden.insert(id);filter_->refresh();});
                connect(remove,&QToolButton::clicked,this,[this,id]{engine.removeChannel(id);refresh();});
                chip->setContextMenuPolicy(Qt::CustomContextMenu);connect(chip,&QWidget::customContextMenuRequested,this,[this,id,chip](QPoint p){QMenu menu;menu.addAction(studioText("Remove channel"),this,[this,id]{engine.removeChannel(id);refresh();});menu.exec(chip->mapToGlobal(p));});
            }
            const QString indicator=status=="Reading comments"?"● ":status.contains("paused")||status.contains("sign-in")?"! ":"◌ ";
            const auto platform=ch["platform"].toString(),label=platform+" · "+ch["name"].toString();chip->label=indicator+label;chip->updateLabel();
            {const QSignalBlocker blocker(chip);chip->setChecked(!filter_->hidden.contains(id));}
            const QColor accent=platform=="YouTube"?QColor("#ffb3a8"):platform=="Twitch"?QColor("#d3bcff"):QColor("#8fe8e6");chip->setIcon(studioIcon(StudioIcon::Chat,accent));
            chip->setToolTip(label+"\n"+studioText(status.toUtf8().constData())+"\n"+studioText("Toggle this channel's comments in the list. Capture continues."));chip->setAccessibleName(label);chip->setAccessibleDescription(chip->toolTip());
            remove->setAccessibleName(studioText("Remove channel")+": "+label);remove->setToolTip(remove->accessibleName());
        }
        chipsState_=channels;
    }
}
}
