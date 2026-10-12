#pragma once
#include "versus/mixer/process_worker.h"
#include "versus/video/camera_capture.h"
#include <atomic>
#include <thread>
#include <future>
namespace versus::mixer {
// Native device APIs and decoders live exclusively in the disposable worker.
// UI/audio/render paths consume bounded snapshots and never call a driver.
class IsolatedSource {
public:
    explicit IsolatedSource(QString kind) : kind_(std::move(kind)) {}
    ~IsolatedSource() { stopCapture(); }
    void setFrameCallback(std::function<void(video::CapturedFrame)> callback) { frame_ = std::move(callback); }
    void setImageCallback(std::function<void(QImage)> callback) { image_ = std::move(callback); }
    bool startCapture(const std::string &locator, int width, int height, double fps, bool alpha = false);
    void stopCapture();
    void StopCapture() { stopCapture(); }
    bool isCapturing() const;
    bool IsCapturing() const { return isCapturing(); }
    std::string lastError() const { return error().toStdString(); }
    QString error() const;
    std::vector<video::WindowInfo> getWindows();
    static std::vector<video::WindowInfo> cameras();
    static std::vector<audio::AudioDeviceInfo> inputs();
    video::CameraSettings settings() const;
    std::future<video::CameraControlResult> setControl(const std::string &id, int value, bool automatic);
    audio::CaptureResult StartStreamCapture(uint32_t pid, audio::WindowAudioCaptureCore::StreamCallback callback, bool exclude = false);
    audio::CaptureResult StartInputDeviceStreamCapture(const std::string &locator, audio::WindowAudioCaptureCore::StreamCallback callback);
    void start(const QString &path, std::function<void(video::CapturedFrame)> frame, audio::WindowAudioCaptureCore::StreamCallback audio);
    void setPaused(bool paused);
    void setLoop(bool loop);
    void restart();
    bool ended() const;
    bool hasVideo() const;
    bool workerFailed() const { return failed_; }
private:
    bool begin(const QJsonObject &command);
    void consume();
    QJsonObject snapshot() const;
    void mediaCommand(const QJsonObject &command);
    QString kind_;
    std::shared_ptr<ProcessWorker> process_ = std::make_shared<ProcessWorker>();
    std::function<void(video::CapturedFrame)> frame_;
    std::function<void(QImage)> image_;
    audio::WindowAudioCaptureCore::StreamCallback audio_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    mutable std::mutex mutex_;
    QJsonObject status_, pending_;
    bool paused_ = false, loop_ = true;
    std::thread reader_;
};
}
