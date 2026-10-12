#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "versus/output/output_session.h"

namespace versus::mixer {
// Independent recording session. The render/audio callbacks never wait for an
// encoder, disk, or muxer. Video backlog is bounded to the newest complete frame.
class ProgramRecorder {
public:
    ~ProgramRecorder();
    bool start(const QString &path, int width, int height, int fps);
    void stop();
    void requestStop();
    bool busy() const { return busy_; }
    void submit(const QImage &image, int64_t timestamp);
    void audio(const std::vector<float> &samples, int64_t timestamp);
    bool active() const { return running_; }
    QString status() const;
    QString path() const { std::lock_guard lock(mutex_); return path_; }
    int64_t remainingSeconds() const { return remainingSeconds_; }
    uint64_t dropped() const { return dropped_; }
private:
    std::atomic<bool> running_{false}, busy_{false};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<int64_t> remainingSeconds_{-1};
    std::atomic<std::shared_ptr<output::Session>> session_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    QImage pending_;
    int64_t timestamp_ = 0;
    QString status_;
    QString path_;
    std::thread worker_;
};
}
