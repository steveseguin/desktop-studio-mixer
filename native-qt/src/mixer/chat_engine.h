#pragma once
#include "chat_data.h"
#include <QObject>
#include <QImage>
#include <QTimer>
#include <functional>
#include <memory>

namespace versus::mixer {
// Constructed only in the inherited-handle chat worker.
class ChatWorker : public QObject {
public:
    explicit ChatWorker(std::function<void(QImage,QString)> frame);
    ~ChatWorker();
    QJsonObject command(const QJsonObject &command);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class ChatEngine : public QObject {
    Q_OBJECT
public:
    explicit ChatEngine(QObject *parent = nullptr);
    ~ChatEngine();
    ChatSettings settings;
    ChatMessageModel model;
    QJsonObject manifest, health;
    QString settingsError;
    bool packAvailable = false;
    void configure();
    QJsonObject addChannel(const QString &input);
    bool removeChannel(const QString &id);
    bool setPractice(bool enabled);
    void send(QJsonObject command);
    QImage image() const { return image_; }
    QString renderedToken() const { return renderedToken_; }
signals:
    void updated();
    void commentDeleted(const QJsonObject &deletion);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    QTimer poll_;
    QImage image_;
    QString renderedToken_;
    bool writable_ = true;
};
}
