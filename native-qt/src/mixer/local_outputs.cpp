#include "versus/mixer/local_outputs.h"
#include "versus/mixer/process_worker.h"
#include "versus/video/bgra_nv12.h"
#include <QCoreApplication>
#include <QLibrary>
#include <QSettings>
#include <QStandardPaths>
#include <QSaveFile>
#include <QDir>
#ifdef VERSUS_USE_SPOUT
#include <SpoutLibrary.h>
#endif
#include <windows.h>
#include <shared-memory-queue.h>
#include <vector>
#include <deque>
namespace versus::mixer {
struct LocalOutputs::IsolatedOutput {
    const QString kind, label;
    std::atomic<bool> running{true}, enabled{false};
    std::atomic<int> fps{30};
    std::atomic<uint64_t> generation{0};
    mutable std::mutex mutex;
    std::condition_variable wake;
    QImage pending;
    int64_t timestamp = 0;
    QString message;
    std::thread thread;
    explicit IsolatedOutput(bool camera) : kind(camera ? "virtual-output" : "spout-output"), label(camera ? "Virtual camera" : "Spout output") {
        thread = std::thread([this] { run(); });
    }
    void requestStop() { running = false; wake.notify_all(); }
    ~IsolatedOutput() { requestStop(); if (thread.joinable()) thread.join(); }
    void configure(bool on, int rate) {
        const bool changed = enabled.exchange(on) != on;
        const bool rateChanged = fps.exchange(rate) != rate;
        if (changed || rateChanged) ++generation;
        wake.notify_all();
    }
    void submit(const QImage &image, int64_t time) {
        if (!enabled) return;
        { std::lock_guard lock(mutex); pending = image; timestamp = time; } wake.notify_one();
    }
    QString status() const { std::lock_guard lock(mutex); return message; }
    void setStatus(const QString &text) { std::lock_guard lock(mutex); message = text; }
    void run() {
        SetThreadDescription(GetCurrentThread(),L"Desktop Studio local-output supervisor");
        using Clock = std::chrono::steady_clock;
        ProcessWorker process;
        uint64_t seenGeneration = 0;
        qint64 delivered = -1;
        qint64 imageKey = 0;
        auto progressAt = Clock::now(), retryAt = Clock::time_point{};
        std::deque<Clock::time_point> failures;
        bool paused = false, started = false;
        while (running) {
            QImage image; int64_t time;
            { std::unique_lock lock(mutex); wake.wait_for(lock,std::chrono::milliseconds(100),[&] { return !running || !pending.isNull() || generation != seenGeneration; });
              image = std::move(pending); pending = {}; time = timestamp; }
            if (!running) break;
            if (seenGeneration != generation) {
                seenGeneration = generation; failures.clear(); paused = false; retryAt = {};
                process.stop(); started = false; setStatus({});
            }
            if (!enabled) continue;
            if (paused || Clock::now() < retryAt || image.isNull()) continue;
            bool ok = !started || process.alive();
            if (ok && !started) {
                ok = process.start(kind) && process.call({{"op","start"},{"fps",fps.load()}})["ok"].toBool();
                started = ok;
                delivered = -1; imageKey = 0; progressAt = Clock::now();
            }
            if (ok) {
                if (imageKey == image.cacheKey()) {
                    // Slates/static compositions still get fresh timestamps,
                    // without recopying their pixels across the process boundary.
                    ok = process.call({{"op","repeat"},{"timestamp",qint64(time)}},nullptr,0,nullptr,1000)["ok"].toBool();
                } else {
                    ok = process.call({{"op","frame"},{"width",image.width()},{"height",image.height()},
                        {"stride",image.bytesPerLine()},{"timestamp",qint64(time)}},image.constBits(),size_t(image.sizeInBytes()),nullptr,1000)["ok"].toBool();
                    if (ok) imageKey = image.cacheKey();
                }
                const auto state = process.status();
                const auto count = state["completedFrames"].toInteger();
                if (count != delivered) { delivered = count; progressAt = Clock::now(); }
                ok &= Clock::now()-progressAt < std::chrono::seconds(10);
                if (ok) setStatus(state["error"].toString());
            }
            if (!ok) {
                process.terminate(); process.stop(); started = false;
                const auto now = Clock::now(); failures.push_back(now);
                while (!failures.empty() && now-failures.front()>std::chrono::minutes(1)) failures.pop_front();
                paused = failures.size()>3; retryAt = now+std::chrono::seconds(2);
                setStatus(label+(paused ? " paused after repeated failures. Turn it off and on to retry." : " stopped responding. Reconnecting..."));
            }
        }
    }
};
LocalOutputs::LocalOutputs() = default;
LocalOutputs::~LocalOutputs() {
    if (isolatedSpout_) isolatedSpout_->requestStop();
    if (isolatedCamera_) isolatedCamera_->requestStop();
    running_ = false; wake_.notify_all(); if (worker_.joinable()) worker_.join();
}
QString LocalOutputs::registerCamera(bool install) {
    if (!ProcessWorker::inWorker()) {
        ProcessWorker process;
        if (!process.start("local-registration")) return "Could not start isolated virtual-camera registration.";
        const auto result = process.call({{"op","register"},{"install",install}});
        return result["ok"].toBool() ? QString() : result["error"].toString("Virtual-camera registration failed.");
    }
    QLibrary library(QCoreApplication::applicationDirPath()+"/desktop-studio-camera.dll");
    if (!library.load()) return "Virtual-camera component is missing: reinstall Desktop Studio.";
    const auto entry = reinterpret_cast<HRESULT (__stdcall*)()>(library.resolve(install ? "DllRegisterServer" : "DllUnregisterServer"));
    if (!entry || FAILED(entry())) return "Could not register the virtual camera for this Windows account.";
    return {};
}
void LocalOutputs::configure(bool spout, bool camera, int fps) {
    if (!ProcessWorker::inWorker()) {
        if (spout && !isolatedSpout_) isolatedSpout_ = std::make_unique<IsolatedOutput>(false);
        if (camera && !isolatedCamera_) isolatedCamera_ = std::make_unique<IsolatedOutput>(true);
        if (isolatedSpout_) isolatedSpout_->configure(spout,fps);
        if (isolatedCamera_) isolatedCamera_->configure(camera,fps);
        return;
    }
    spout_ = spout; camera_ = camera; fps_ = fps;
    if (!worker_.joinable()) { running_ = true; worker_ = std::thread([this] { run(); }); }
    wake_.notify_all();
}
QString LocalOutputs::status() const {
    if (!ProcessWorker::inWorker()) {
        QStringList errors;
        for (const auto *output : {isolatedSpout_.get(),isolatedCamera_.get()}) if (output) {
            const auto error = output->status(); if (!error.isEmpty()) errors.append(error);
        }
        return errors.join(" | ");
    }
    std::lock_guard lock(mutex_); return status_;
}
void LocalOutputs::submit(const QImage &frame, int64_t timestamp) {
    if (!ProcessWorker::inWorker()) {
        if (isolatedSpout_) isolatedSpout_->submit(frame,timestamp);
        if (isolatedCamera_) isolatedCamera_->submit(frame,timestamp);
        return;
    }
    if (!spout_ && !camera_) return;
    { std::lock_guard lock(mutex_); pending_ = frame; timestamp_ = timestamp; } wake_.notify_one();
}
void LocalOutputs::run() {
    SetThreadDescription(GetCurrentThread(),L"Desktop Studio native local output");
    #ifdef VERSUS_USE_SPOUT
    SPOUTLIBRARY *sender = nullptr;
    #endif
    video_queue_t *camera = nullptr; QSize cameraSize; int cameraFps = 0;
    std::vector<uint8_t> nv12;
    while (running_) {
        QImage frame; int64_t timestamp;
        { std::unique_lock lock(mutex_); wake_.wait_for(lock, std::chrono::milliseconds(100), [this] { return !running_ || !pending_.isNull(); });
          frame = std::move(pending_); pending_ = {}; timestamp = timestamp_; }
        #ifdef VERSUS_USE_SPOUT
        if (!spout_ && sender) { sender->ReleaseSender(); sender->CloseOpenGL(); sender->Release(); sender = nullptr; }
        #endif
        if (!camera_ && camera) { video_queue_close(camera); camera = nullptr; }
        if (frame.isNull() || !running_) continue;
        QString message;
        #ifdef VERSUS_USE_SPOUT
        if (spout_) {
            if (!sender) {
                sender = GetSpout();
                if (sender && !sender->CreateOpenGL(nullptr)) { sender->Release(); sender = nullptr; }
                if (sender) sender->SetSenderName("Desktop Studio Program");
            }
            if (!sender || !sender->SendImage(frame.constBits(), frame.width(), frame.height(), GL_BGRA, false)) message = "Spout output unavailable. ";
        }
        #else
        if (spout_) message = "This build does not include Spout output. ";
        #endif
        if (camera_) {
            if (camera && (cameraSize != frame.size() || cameraFps != fps_)) { video_queue_close(camera); camera = nullptr; }
            if (!camera) {
                cameraSize = frame.size(); cameraFps = fps_;
                camera = video_queue_create(frame.width(), frame.height(), 10000000/cameraFps);
                QSaveFile settings(QDir(qEnvironmentVariable("APPDATA")).filePath("desktop-studio-virtualcam.txt"));
                // The filter also reads the live shared-memory format. Defaults
                // remain available when this optional preference cannot be saved.
                if (settings.open(QIODevice::WriteOnly)) { settings.write(QString("%1x%2x%3").arg(frame.width()).arg(frame.height()).arg(10000000/cameraFps).toUtf8()); settings.commit(); }
            }
            if (camera) {
                nv12.resize(size_t(frame.width())*frame.height()*3/2);
                video::detail::bgraToNv12(frame.constBits(), frame.width(), frame.height(), frame.bytesPerLine(), nv12.data());
                uint8_t *planes[]{nv12.data(), nv12.data()+size_t(frame.width())*frame.height()};
                uint32_t stride[]{uint32_t(frame.width()), uint32_t(frame.width())};
                video_queue_write(camera, planes, stride, uint64_t(timestamp)*100);
            } else message += "Virtual camera is in use by another Desktop Studio instance.";
        }
        { std::lock_guard lock(mutex_); status_ = message; }
        ++completedFrames_;
    }
    if (camera) video_queue_close(camera);
    #ifdef VERSUS_USE_SPOUT
    if (sender) { sender->ReleaseSender(); sender->CloseOpenGL(); sender->Release(); }
    #endif
}
}
