#pragma once
#include "versus/app/versus_app.h"
#include <QImage>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace versus::mixer {
// A private receiver-specific return carries Program video and mix-minus audio.
// It is independent of the public streaming/recording destinations.
class GuestReturn {
public:
    GuestReturn(const QString &stream, const QString &password, const QString &room, int width, int height);
    ~GuestReturn();
    void stop();
    void video(const QImage &frame, int64_t timestamp);
    void audio(std::vector<float> samples, int64_t timestamp);
    QString status() const;
private:
    std::atomic<bool> running_{true};
    std::atomic<std::shared_ptr<app::VersusApp>> core_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    QString status_ = "Connecting guest return...";
    int64_t lastVideo_ = 0;
};
}
