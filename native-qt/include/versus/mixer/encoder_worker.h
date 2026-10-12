#pragma once
#include "versus/mixer/process_worker.h"
#include "versus/video/video_encoder.h"
#include <chrono>
#include <deque>
namespace versus::mixer {
class EncoderWorker {
public:
    bool initialize(const video::EncoderConfig &config, const video::CapturedFrame *handover);
    bool encode(const video::CapturedFrame &frame, video::EncodedPacket &packet, int64_t timestamp, std::span<const uint8_t> pixels = {});
    void keyframe() { keyframe_ = true; }
    void bitrate(int kbps) { bitrate_ = kbps; config_.bitrate = kbps; }
    QJsonObject info;
    ProcessWorker process;
private:
    video::EncoderConfig config_;
    std::deque<std::chrono::steady_clock::time_point> restarts_;
    bool restartNeeded_ = false;
    bool keyframe_ = false;
    int bitrate_ = 0;
};
}
