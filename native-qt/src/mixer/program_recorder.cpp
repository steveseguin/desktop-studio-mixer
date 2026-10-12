#include "versus/mixer/program_recorder.h"
#include "versus/video/video_encoder.h"
#include "versus/video/window_capture.h"
#include <QFileInfo>
#include <QDir>
#include <windows.h>
#include <objbase.h>

namespace versus::mixer {
ProgramRecorder::~ProgramRecorder() { stop(); }
QString ProgramRecorder::status() const {
    if (auto session = session_.load()) {
        const auto state = session->status();
        if (state.state == output::State::Error) return QString::fromStdString(state.message);
    }
    std::lock_guard lock(mutex_); return status_;
}
bool ProgramRecorder::start(const QString &path, int width, int height, int fps) {
    if (busy_) return false;
    if (worker_.joinable()) worker_.join();
    output::Config config; config.protocol = output::Protocol::Recording; config.url = path.toStdString();
    const auto error = output::validateConfig(config);
    { std::lock_guard lock(mutex_);
      if (!error.empty()) { status_ = QString::fromStdString(error); return false; }
      status_ = "Starting recording..."; path_ = path; pending_ = {}; }
    remainingSeconds_ = -1;
    running_ = true; busy_ = true; dropped_ = 0;
    worker_ = std::thread([this, config, width, height, fps, path] {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto setStatus = [this](const QString &text) { std::lock_guard lock(mutex_); status_ = text; };
        video::VideoEncoder encoder;
        video::EncoderConfig settings; settings.width = width; settings.height = height; settings.frameRate = fps;
        // Keep the inherited recording bitrate floor; scale for 60 fps and
        // portrait as well as landscape canvases instead of trading quality
        // for a lower CPU reading.
        settings.bitrate = width*height >= 1920*1080 && fps > 30 ? 24000 : 12000;
        settings.minBitrate = settings.bitrate; settings.maxBitrate = settings.bitrate;
        settings.gopSize = fps*2;
        if (!encoder.initialize(settings)) {
            setStatus("Recording encoder could not start."); running_ = false;
        } else {
            output::MediaConfig media; media.width = width; media.height = height; media.fps = fps;
            media.ffmpegPath = video::VideoEncoder::resolveFfmpegPath("");
            std::atomic<bool> keyframe{true};
            auto session = std::make_shared<output::Session>(config, media, [&keyframe] { keyframe = true; });
            if (!session->start()) { setStatus(QString::fromStdString(session->status().message)); running_ = false; }
            else {
                session_.store(session); setStatus("Recording: "+QFileInfo(path).fileName());
                auto diskCheck = std::chrono::steady_clock::now();
                auto lastEncoded = diskCheck;
                bool wroteVideo = false;
                while (running_) {
                    QImage image; int64_t timestamp;
                    { std::unique_lock lock(mutex_);
                      wake_.wait_for(lock, std::chrono::milliseconds(250), [this] { return !running_ || !pending_.isNull(); });
                      if (!running_) break;
                      image = std::move(pending_); pending_ = {}; timestamp = timestamp_; }
                    if (session->status().state == output::State::Error) { setStatus(QString::fromStdString(session->status().message)); running_ = false; break; }
                    if (std::chrono::steady_clock::now()-diskCheck > std::chrono::seconds(2)) {
                        diskCheck = std::chrono::steady_clock::now();
                        // Query the actual destination directory. QStorageInfo
                        // can resolve a UNC submount to its share's parent volume
                        // and miss a full/quota-limited recording destination.
                        auto directory = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
                        if (!directory.endsWith('\\')) directory += '\\';
                        ULARGE_INTEGER available{};
                        const bool haveSpace = GetDiskFreeSpaceExW(directory.toStdWString().c_str(), &available, nullptr, nullptr);
                        remainingSeconds_ = haveSpace ? int64_t(available.QuadPart > 128*1024*1024 ? (available.QuadPart-128*1024*1024)*8/(uint64_t(settings.bitrate+256)*1000) : 0) : -1;
                        if (haveSpace && available.QuadPart < 128*1024*1024) {
                            setStatus("Recording stopped: less than 128 MB disk space remains."); running_ = false; break;
                        }
                    }
                    if (image.isNull()) continue;
                    video::CapturedFrame frame; frame.width = image.width(); frame.height = image.height();
                    frame.stride = image.bytesPerLine(); frame.timestamp = timestamp; frame.opaque = !image.hasAlphaChannel();
                    if (keyframe.exchange(false)) encoder.requestKeyframe();
                    video::EncodedPacket packet;
                    if (encoder.encodeView(frame, {image.constBits(), size_t(image.sizeInBytes())}, packet)) {
                        lastEncoded = std::chrono::steady_clock::now(); wroteVideo = true;
                        webrtc::EncodedVideoPacket out; out.data = std::move(packet.data);
                        out.pts = packet.sourceTimestamp; out.isKeyframe = packet.isKeyframe;
                        if (!session->sendVideo(out, width, height)) ++dropped_;
                    } else if (std::chrono::steady_clock::now()-lastEncoded > std::chrono::seconds(10)) {
                        setStatus("Recording stopped: video encoder stopped producing frames."); running_ = false;
                    }
                }
                session_.store({}); session->stop();
                running_ = false;
                if (session->status().state == output::State::Error) setStatus(QString::fromStdString(session->status().message));
                else if (!wroteVideo) setStatus("Recording stopped before any video was captured.");
                { std::lock_guard lock(mutex_); if (status_.startsWith("Recording:") || status_ == "Finalizing recording...") status_ = "Recording saved: "+QFileInfo(path).fileName(); }
            }
            encoder.shutdown();
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
        busy_ = false;
    });
    return true;
}
void ProgramRecorder::requestStop() {
    if (running_.exchange(false)) { std::lock_guard lock(mutex_); status_ = "Finalizing recording..."; }
    wake_.notify_all();
}
void ProgramRecorder::stop() {
    requestStop();
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_); pending_ = {};
    if (status_.startsWith("Recording:")) status_ = "Recording saved.";
}
void ProgramRecorder::submit(const QImage &image, int64_t timestamp) {
    if (!running_) return;
    { std::lock_guard lock(mutex_); if (!pending_.isNull()) ++dropped_; pending_ = image; timestamp_ = timestamp; }
    wake_.notify_one();
}
void ProgramRecorder::audio(const std::vector<float> &samples, int64_t timestamp) {
    if (auto session = session_.load(); running_ && session && session->wantsAudio()) session->sendPcm(samples, timestamp);
}
}
