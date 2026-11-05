#pragma once
#include "chat_engine.h"
#include <QWidget>
#include <QElapsedTimer>
#include <functional>
class QLabel;
class QListView;
class QLineEdit;
class QPushButton;
class QButtonGroup;
class QProgressBar;
class QAction;
class QHBoxLayout;
namespace versus::mixer {
class ChatFilter;
class ChatCard;
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget *parent = nullptr);
    ChatEngine engine;
    CommentsLayer layer;
    std::function<bool()> outputActive;
    std::function<void()> advanced, changed;
    QJsonObject command(const QJsonObject &command);
    QJsonObject state() const;
    void compose(QImage &frame, bool privateBreak);
    void setPrivateBreak(bool enabled);
    void hideComment();
    void refresh();
    bool confirmPracticeOutput();
    bool practiceAcknowledged = false;
private:
    ChatFilter *filter_;
    ChatCard *card_;
    QListView *list_;
    QLineEdit *input_, *search_;
    QButtonGroup *styles_;
    QProgressBar *countdown_;
    QAction *practiceAction_;
    QWidget *airBand_, *feedControls_;
    QLabel *airComment_;
    QLabel *notice_, *air_, *hint_;
    QLabel *practiceBanner_;
    QPushButton *new_, *practice_, *save_, *show_, *keep_, *hide_;
    QWidget *tray_;
    QWidget *setup_;
    QPushButton *addMore_;
    bool setupOpen_ = false;
    bool syncingSelection_ = false;
    QHBoxLayout *chips_;
    QJsonArray chipsState_;
    QString selected_, token_, waitingToken_;
    QJsonObject onAir_, requested_;
    QImage image_;
    bool privateBreak_ = false, composed_ = false, pinned_ = false;
    int remainingMs_ = 0, practiceIndex_ = 0;
    QElapsedTimer clock_, practiceClock_;
    QElapsedTimer showClock_;
    qint64 frameReceivedMs_=-1, composedMs_=-1;
    QTimer timer_;
    void select(const QString &id);
    void jump();
    QJsonObject showComment();
    bool practice(bool enabled, bool acknowledged = false);
};
}
