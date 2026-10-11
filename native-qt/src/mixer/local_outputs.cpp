#include "versus/mixer/local_outputs.h"
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
namespace versus::mixer {
LocalOutputs::~LocalOutputs() { running_ = false; wake_.notify_all(); if (worker_.joinable()) worker_.join(); }
QString LocalOutputs::registerCamera(bool install) {
    QLibrary library(QCoreApplication::applicationDirPath()+"/desktop-studio-camera.dll");
    if (!library.load()) return "Virtual-camera component is missing: reinstall Desktop Studio.";
    const auto entry = reinterpret_cast<HRESULT (__stdcall*)()>(library.resolve(install ? "DllRegisterServer" : "DllUnregisterServer"));
    if (!entry || FAILED(entry())) return "Could not register the virtual camera for this Windows account.";
    return {};
}
void LocalOutputs::configure(bool spout, bool camera, int fps) {
    spout_ = spout; camera_ = camera; fps_ = fps;
    if (!worker_.joinable()) { running_ = true; worker_ = std::thread([this] { run(); }); }
    wake_.notify_all();
}
QString LocalOutputs::status() const { std::lock_guard lock(mutex_); return status_; }
void LocalOutputs::submit(const QImage &frame, int64_t timestamp) {
    if (!spout_ && !camera_) return;
    { std::lock_guard lock(mutex_); pending_ = frame; timestamp_ = timestamp; } wake_.notify_one();
}
void LocalOutputs::run() {
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
    }
    if (camera) video_queue_close(camera);
    #ifdef VERSUS_USE_SPOUT
    if (sender) { sender->ReleaseSender(); sender->CloseOpenGL(); sender->Release(); }
    #endif
}
}
