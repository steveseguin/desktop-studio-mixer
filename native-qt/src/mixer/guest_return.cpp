#include "versus/mixer/guest_return.h"
#include <objbase.h>
namespace versus::mixer {
GuestReturn::GuestReturn(const QString &stream, const QString &password, const QString &room, int width, int height) {
    worker_ = std::thread([this, stream, password, room, width, height] {
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto core = std::make_shared<app::VersusApp>();
        core->onRuntimeEvent([this](const std::string &text, bool) { std::lock_guard lock(mutex_); status_ = QString::fromStdString(text); });
        video::EncoderConfig config; config.width = width; config.height = height; config.frameRate = 30;
        config.bitrate = 2500; config.minBitrate = 1000; config.maxBitrate = config.bitrate;
        core->setVideoConfig(config); core->setAudioSourceMode(app::AudioSourceMode::None); core->setIncludeMicrophone(false);
        app::StartOptions options; options.streamId = stream.toStdString(); options.password = password.toStdString();
        options.room = room.toStdString(); options.label = "Desktop Studio return"; options.maxViewers = 1; options.roomModeLqEnabled = false;
        if (core->initialize() && running_ && core->startCapture(app::VideoSourceMode::Mixer, "mixer") && core->goLive(options)) {
            core_.store(core);
            { std::unique_lock lock(mutex_); status_ = "Guest return ready"; wake_.wait(lock, [this] { return !running_; }); }
        } else { std::lock_guard lock(mutex_); status_ = "Guest return could not connect. Toggle return off and on to retry."; }
        core_.store({}); core->shutdown(); core.reset();
        if (SUCCEEDED(apartment)) CoUninitialize();
    });
}
GuestReturn::~GuestReturn() { stop(); }
void GuestReturn::stop() { running_ = false; wake_.notify_all(); if (worker_.joinable()) worker_.join(); }
QString GuestReturn::status() const { std::lock_guard lock(mutex_); return status_; }
void GuestReturn::video(const QImage &frame, int64_t timestamp) {
    if (timestamp-lastVideo_ < 320000) return;
    auto core = core_.load(); if (!core || !core->isLive() || core->getStreamMetrics().activeVideoPeers == 0) return;
    lastVideo_ = timestamp;
    video::CapturedFrame captured; captured.width = frame.width(); captured.height = frame.height(); captured.stride = frame.bytesPerLine();
    captured.opaque = !frame.hasAlphaChannel(); captured.timestamp = timestamp;
    captured.data.assign(frame.constBits(), frame.constBits()+frame.sizeInBytes()); core->submitMixerVideo(std::move(captured));
}
void GuestReturn::audio(std::vector<float> samples, int64_t timestamp) {
    if (auto core = core_.load(); core && core->isLive()) {
        audio::StreamChunk chunk; chunk.samples = std::move(samples); chunk.captureTime100ns = timestamp; core->submitMixerAudio(std::move(chunk));
    }
}
}
