#pragma once
#include <QString>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include "versus/video/window_capture.h"
#include "versus/audio/window_audio_capture_core.h"
namespace versus::mixer {
class MediaSource {
public:
    ~MediaSource();
    // The synchronous callback borrows a reusable decode buffer until it returns.
    void start(const QString &path, std::function<void(const video::CapturedFrame &)> frame,
               std::function<void(audio::StreamChunk&&)> audio);
    void stop();
    void setPaused(bool paused) { if (!paused && ended_) seek(0); paused_ = paused; wake_.notify_all(); }
    void setLoop(bool loop) { loop_ = loop; }
    void restart() { paused_ = false; seek(0); }
    void seek(int64_t milliseconds) { seekMs_ = milliseconds; seek_ = true; wake_.notify_all(); }
    QString error() const { std::lock_guard lock(mutex_); return error_; }
    bool ended() const { return ended_; }
    bool hasVideo() const { return hasVideo_; }
    int64_t positionMs() const { return positionMs_; }
    int64_t durationMs() const { return durationMs_; }
private:
    std::atomic<bool> running_{false}, paused_{false}, loop_{true}, seek_{false}, ended_{false};
    std::atomic<bool> hasVideo_{false};
    std::atomic<int64_t> positionMs_{0}, durationMs_{0}, seekMs_{0};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    QString error_;
    std::thread worker_;
};
}
