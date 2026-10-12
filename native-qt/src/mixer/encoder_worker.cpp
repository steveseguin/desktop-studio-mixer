#include "versus/mixer/encoder_worker.h"
#include <utility>
#include <spdlog/spdlog.h>
namespace versus::mixer {
bool EncoderWorker::initialize(const video::EncoderConfig &c, const video::CapturedFrame *handover) {
    config_ = c;
    if (!process.start("encoder")) return false;
    QJsonObject command{{"op", "start"}, {"codec", int(c.codec)}, {"hardware", int(c.preferredHardware)},
        {"explicit", c.explicitEncoderSelection}, {"forceFfmpeg", c.forceFfmpegNvenc},
        {"ffmpeg", QString::fromStdString(c.ffmpegPath)}, {"options", QString::fromStdString(c.ffmpegOptions)},
        {"everyKeyframe", c.requireEveryFrameKeyframe}, {"alpha", c.enableAlpha}, {"background", int(c.alphaBackgroundMode)},
        {"red", c.alphaBackgroundRed}, {"green", c.alphaBackgroundGreen}, {"blue", c.alphaBackgroundBlue},
        {"width", c.width}, {"height", c.height}, {"fps", c.frameRate}, {"bitrate", c.bitrate},
        {"minBitrate", c.minBitrate}, {"maxBitrate", c.maxBitrate}, {"gop", c.gopSize}, {"bFrames", c.bFrames}, {"lowLatency", c.lowLatency}};
    if (handover) {
        command["frameWidth"] = handover->width; command["frameHeight"] = handover->height; command["stride"] = handover->stride;
        command["format"] = int(handover->format); command["opaque"] = handover->opaque; command["timestamp"] = qint64(handover->timestamp);
    }
    info = process.call(command, handover ? handover->data.data() : nullptr, handover ? handover->data.size() : 0, nullptr, 30000);
    return info["ok"].toBool();
}
bool EncoderWorker::encode(const video::CapturedFrame &f, video::EncodedPacket &packet, int64_t timestamp, std::span<const uint8_t> pixels) {
    if (!process.alive() || restartNeeded_) {
        const auto now = std::chrono::steady_clock::now();
        while (!restarts_.empty() && now-restarts_.front() > std::chrono::minutes(1)) restarts_.pop_front();
        if (restarts_.size() >= 3 || (!restarts_.empty() && now-restarts_.back() < std::chrono::seconds(3))) return false;
        restarts_.push_back(now);
        // Recreate the same configuration, including strict hardware selection.
        // A fresh keyframe keeps the existing destination/recording usable.
        const auto config = config_;
        restartNeeded_ = !initialize(config, nullptr);
        if (restartNeeded_) return false;
        keyframe_ = true;
        spdlog::warn("[Encoder] Restarted isolated encoder after a worker failure");
    }
    QJsonObject command{{"op", "encode"}, {"width", f.width}, {"height", f.height}, {"stride", f.stride},
        {"format", int(f.format)}, {"opaque", f.opaque}, {"timestamp", qint64(f.timestamp)}, {"sourceTimestamp", qint64(timestamp)},
        {"keyframe", std::exchange(keyframe_, false)}, {"bitrate", std::exchange(bitrate_, 0)}};
    QByteArray bytes;
    if (pixels.empty()) pixels = f.data;
    const auto result = process.call(command, pixels.data(), pixels.size(), &bytes, 2000);
    // Transport failures have no encoder metadata. Retain its identity so UI
    // and recovery policy cannot mistake a crashed hardware worker for software.
    for (auto it = result.begin(); it != result.end(); ++it) info[it.key()] = it.value();
    if (!result["ok"].toBool()) {
        const auto failure = video::EncodeFailureKind(result["failure"].toInt());
        restartNeeded_ = !process.alive() || failure == video::EncodeFailureKind::ProcessExited ||
            failure == video::EncodeFailureKind::OutputStalled || failure == video::EncodeFailureKind::IoFailure;
        return false;
    }
    packet.data.assign(bytes.begin(), bytes.end()); packet.pts = info["pts"].toInteger(); packet.dts = info["dts"].toInteger();
    packet.sourceTimestamp = info["sourceTimestamp"].toInteger(); packet.isKeyframe = info["keyframe"].toBool(); packet.codec = video::VideoCodec(info["codec"].toInt());
    return true;
}
}
