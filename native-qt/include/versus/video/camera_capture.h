#pragma once

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "versus/video/window_capture.h"

namespace versus::video {

struct CameraFormat {
    int width = 0, height = 0;
    double fps = 0;
};

struct CameraControl {
    std::string id, name;
    int minimum = 0, maximum = 0, step = 1, defaultValue = 0, value = 0;
    bool supportsAuto = false, supportsManual = false, automatic = false;
};

struct CameraSettings {
    CameraFormat activeFormat;
    std::vector<CameraFormat> formats;
    std::vector<CameraControl> controls;
};

struct CameraControlResult {
    bool success = false;
    CameraControl control;
    std::string error;
};

class CameraCapture {
  public:
    using FrameCallback = std::function<void(CapturedFrame)>;

    CameraCapture();
    ~CameraCapture();

    std::vector<WindowInfo> getCameras();

    bool startCapture(const std::string &deviceId, int width, int height, double fps);
    void stopCapture();
    bool isCapturing() const;
    std::string lastError() const;

    // Snapshots and queued writes keep driver calls on the capture COM thread.
    CameraSettings settings() const;
    std::future<CameraControlResult> setControl(const std::string &id, int value, bool automatic);

    void setFrameCallback(FrameCallback cb);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    FrameCallback frameCallback_;
};

}  // namespace versus::video
