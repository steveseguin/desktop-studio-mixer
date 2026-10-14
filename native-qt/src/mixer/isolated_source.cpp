#include "versus/mixer/isolated_source.h"
#include <QJsonArray>
#include <utility>
namespace versus::mixer {
namespace {
video::CameraControl control(const QJsonObject &c) {
    return {c["id"].toString().toStdString(), c["name"].toString().toStdString(), c["minimum"].toInt(), c["maximum"].toInt(), c["step"].toInt(), c["default"].toInt(), c["value"].toInt(), c["supportsAuto"].toBool(), c["supportsManual"].toBool(), c["automatic"].toBool()};
}
}
bool IsolatedSource::begin(const QJsonObject &command) {
    if (!process_->start(kind_)) { std::lock_guard lock(mutex_); status_ = {{"error", process_->error()}}; return false; }
    const auto result = process_->call(command, nullptr, 0, nullptr, 15000);
    { std::lock_guard lock(mutex_); status_ = result["status"].toObject(); if (!result["ok"].toBool()) status_["error"] = result["error"].toString(status_["error"].toString()); }
    return result["ok"].toBool();
}
bool IsolatedSource::startCapture(const std::string &locator, int width, int height, double fps, bool alpha) {
    const bool ok = begin({{"op", "start"}, {"locator", QString::fromStdString(locator)}, {"width", width}, {"height", height}, {"fps", fps}, {"alpha", alpha}});
    if (ok) { running_ = true; reader_ = std::thread([this] { consume(); }); } return ok;
}
void IsolatedSource::consume() {
    auto statusAt = std::chrono::steady_clock::now(), heartbeatAt = statusAt;
    qint64 heartbeat = -1;
    while (running_ && process_->alive()) {
        QJsonObject command;
        { std::lock_guard lock(mutex_); command = std::exchange(pending_, {}); }
        if (!command.isEmpty()) {
            process_->call(command);
            if (discontinuity_ && (command.contains("seekMs") || command["restart"].toBool())) discontinuity_();
        }
        // Audio has a 10 ms consumption deadline. Drain it before copying a
        // potentially large picture, especially when only a few CPUs are free.
        audio::StreamChunk audio; if (audio_ && process_->audio(audio)) audio_(std::move(audio));
        if (image_) { auto image = process_->image(); if (!image.isNull()) image_(std::move(image)); }
        else { video::CapturedFrame frame; if (frame_ && process_->frame(frame)) frame_(std::move(frame)); }
        const auto now = std::chrono::steady_clock::now();
        if (now-statusAt >= std::chrono::milliseconds(100)) {
            statusAt = now;
            const auto current = process_->status();
            if (!current.isEmpty()) {
                if (current["heartbeat"].toInteger() != heartbeat) { heartbeat = current["heartbeat"].toInteger(); heartbeatAt = now; }
                std::lock_guard lock(mutex_); status_ = current;
            }
            if (now-heartbeatAt > std::chrono::seconds(10)) { process_->terminate(); break; }
        }
        process_->waitForMedia(50);
    }
    if (running_) { failed_ = true; std::lock_guard lock(mutex_); status_ = {{"active", false}, {"error", "Isolated source stopped. Reconnecting..."}}; }
}
void IsolatedSource::stopCapture() { running_ = false; if (reader_.joinable()) reader_.join(); process_->stop(); }
QJsonObject IsolatedSource::snapshot() const { std::lock_guard lock(mutex_); return status_; }
bool IsolatedSource::isCapturing() const { return running_ && !failed_ && snapshot()["active"].toBool(); }
QString IsolatedSource::error() const { return snapshot()["error"].toString(); }
std::vector<video::WindowInfo> IsolatedSource::getWindows() { video::WindowCapture enumerator; return enumerator.getWindows(); }
std::vector<video::WindowInfo> IsolatedSource::cameras() {
    ProcessWorker worker; std::vector<video::WindowInfo> result;
    if (!worker.start("devices")) return result;
    for (const auto &value : worker.call({{"op", "cameras"}})["devices"].toArray()) {
        const auto c = value.toObject(); video::WindowInfo item;
        item.id = c["id"].toString().toStdString(); item.name = c["name"].toString().toStdString();
        result.push_back(std::move(item));
    }
    return result;
}
std::vector<audio::AudioDeviceInfo> IsolatedSource::inputs() {
    ProcessWorker worker; std::vector<audio::AudioDeviceInfo> result;
    if (!worker.start("devices")) return result;
    for (const auto &value : worker.call({{"op", "inputs"}})["devices"].toArray()) {
        const auto c = value.toObject(); audio::AudioDeviceInfo item;
        item.id = c["id"].toString().toStdString(); item.name = c["name"].toString().toStdString();
        item.sampleRate = c["rate"].toInt(); item.channels = c["channels"].toInt(); item.bitsPerSample = c["bits"].toInt();
        item.validBitsPerSample = c["validBits"].toInt(); item.floatingPoint = c["float"].toBool(); item.isDefault = c["default"].toBool();
        result.push_back(std::move(item));
    }
    return result;
}
video::CameraSettings IsolatedSource::settings() const {
    const auto s = snapshot()["settings"].toObject(); video::CameraSettings result;
    auto format = [](QJsonObject f) { return video::CameraFormat{f["width"].toInt(), f["height"].toInt(), f["fps"].toDouble()}; };
    result.activeFormat = format(s["activeFormat"].toObject());
    for (const auto &f : s["formats"].toArray()) result.formats.push_back(format(f.toObject()));
    for (const auto &c : s["controls"].toArray()) result.controls.push_back(control(c.toObject()));
    return result;
}
std::future<video::CameraControlResult> IsolatedSource::setControl(const std::string &id, int value, bool automatic) {
    return std::async(std::launch::async, [process = process_, id, value, automatic] {
        const auto r = process->call({{"op", "control"}, {"id", QString::fromStdString(id)}, {"value", value}, {"automatic", automatic}});
        return video::CameraControlResult{r["ok"].toBool(), control(r["control"].toObject()), r["error"].toString().toStdString()};
    });
}
audio::CaptureResult IsolatedSource::StartStreamCapture(uint32_t pid, audio::WindowAudioCaptureCore::StreamCallback callback, bool exclude) {
    audio_ = std::move(callback); const bool ok = begin({{"op", "start"}, {"pid", qint64(pid)}, {"exclude", exclude}});
    if (ok) { running_ = true; reader_ = std::thread([this] { consume(); }); }
    return {ok, true, 48000, 2, lastError()};
}
audio::CaptureResult IsolatedSource::StartInputDeviceStreamCapture(const std::string &locator, audio::WindowAudioCaptureCore::StreamCallback callback) {
    audio_ = std::move(callback); const bool ok = begin({{"op", "start"}, {"locator", QString::fromStdString(locator)}});
    if (ok) { running_ = true; reader_ = std::thread([this] { consume(); }); }
    return {ok, false, 48000, 2, lastError()};
}
void IsolatedSource::start(const QString &path, std::function<void(video::CapturedFrame)> frame, audio::WindowAudioCaptureCore::StreamCallback audio) {
    frame_ = std::move(frame); audio_ = std::move(audio); running_ = true;
    const QJsonObject command{{"op", "start"}, {"locator", path}, {"paused", paused_}, {"loop", loop_}};
    reader_ = std::thread([this, command] { if (begin(command)) consume(); else failed_ = true; });
}
void IsolatedSource::mediaCommand(const QJsonObject &command) {
    std::lock_guard lock(mutex_); pending_["op"] = "media";
    for (auto it=command.begin(); it!=command.end(); ++it) pending_[it.key()] = it.value();
}
void IsolatedSource::setPaused(bool paused) { paused_ = paused; mediaCommand({{"paused", paused}}); }
void IsolatedSource::setLoop(bool loop) { loop_ = loop; mediaCommand({{"loop", loop}}); }
void IsolatedSource::restart() { paused_ = false; mediaCommand({{"restart", true}, {"paused", false}}); }
void IsolatedSource::seek(int64_t milliseconds) { mediaCommand({{"seekMs", qint64(milliseconds)}}); }
bool IsolatedSource::ended() const { return snapshot()["ended"].toBool(); }
bool IsolatedSource::hasVideo() const { return snapshot()["hasVideo"].toBool(true); }
int64_t IsolatedSource::positionMs() const { return snapshot()["positionMs"].toInteger(); }
int64_t IsolatedSource::durationMs() const { return snapshot()["durationMs"].toInteger(); }
}
