#include "versus/video/camera_capture.h"
#include "versus/mixer/isolated_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <future>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <dshow.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#endif

namespace versus::video {

#ifdef _WIN32

namespace {

using Microsoft::WRL::ComPtr;

std::string hresultText(HRESULT hr) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    return stream.str();
}

bool isLikelyCameraBusyError(HRESULT hr) {
    return hr == MF_E_HW_MFT_FAILED_START_STREAMING ||
           hr == MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED ||
           hr == MF_E_VIDEO_DEVICE_LOCKED ||
           hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
           hr == HRESULT_FROM_WIN32(ERROR_BUSY);
}

std::string cameraReadError(HRESULT hr, bool starting) {
    const std::string code = hresultText(hr);
    if (isLikelyCameraBusyError(hr)) {
        return (starting
                    ? "Could not start the selected camera"
                    : "Camera capture stopped") +
            std::string(" (") + code +
            "). The camera may already be in use by another app. Close the other app or choose a different camera, then try again.";
    }
    return (starting
                ? "Could not get a first frame from the selected camera"
                : "Camera frame capture failed") +
        std::string(" (") + code +
        "). Check that the camera is connected, allowed in Windows privacy settings, and not in use by another app.";
}

std::string wideToUtf8(const wchar_t *value) {
    if (!value || !*value) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    result.resize(static_cast<size_t>(required - 1));
    return result;
}

std::wstring utf8ToWide(const std::string &value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

class ScopedComApartment {
  public:
    ScopedComApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = SUCCEEDED(hr);
        usable_ = initialized_ || hr == RPC_E_CHANGED_MODE;
        result_ = hr;
    }

    ~ScopedComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    bool usable() const { return usable_; }
    HRESULT result() const { return result_; }

  private:
    bool initialized_ = false;
    bool usable_ = false;
    HRESULT result_ = E_FAIL;
};

class ScopedMediaFoundation {
  public:
    ScopedMediaFoundation() {
        result_ = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        started_ = SUCCEEDED(result_);
    }

    ~ScopedMediaFoundation() {
        if (started_) {
            MFShutdown();
        }
    }

    bool started() const { return started_; }
    HRESULT result() const { return result_; }

  private:
    bool started_ = false;
    HRESULT result_ = E_FAIL;
};

struct NativeMediaTypeChoice {
    ComPtr<IMFMediaType> type;
    GUID subtype = GUID_NULL;
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 fpsNumerator = 0;
    UINT32 fpsDenominator = 1;
    int64_t score = std::numeric_limits<int64_t>::max();
};

struct ControlDefinition { const char *id; const char *name; bool camera; long property; };
constexpr ControlDefinition controlDefinitions[] = {
    {"zoom", "Zoom", true, CameraControl_Zoom},
    {"exposure", "Exposure", true, CameraControl_Exposure},
    {"focus", "Focus", true, CameraControl_Focus},
    {"brightness", "Brightness", false, VideoProcAmp_Brightness},
    {"contrast", "Contrast", false, VideoProcAmp_Contrast},
    {"white_balance", "White balance", false, VideoProcAmp_WhiteBalance},
    {"saturation", "Saturation", false, VideoProcAmp_Saturation},
    {"sharpness", "Sharpness", false, VideoProcAmp_Sharpness},
    {"gain", "Gain", false, VideoProcAmp_Gain},
    {"backlight", "Backlight compensation", false, VideoProcAmp_BacklightCompensation},
    {"gamma", "Gamma", false, VideoProcAmp_Gamma},
    {"hue", "Hue", false, VideoProcAmp_Hue},
    {"pan", "Pan", true, CameraControl_Pan},
    {"tilt", "Tilt", true, CameraControl_Tilt}
};

bool readControl(IMFMediaSource *source, const ControlDefinition &definition, CameraControl &control) {
    long minimum = 0, maximum = 0, step = 1, defaultValue = 0, capabilities = 0, value = 0, flags = 0;
    HRESULT range = E_NOINTERFACE, current = E_NOINTERFACE;
    if (definition.camera) {
        ComPtr<IAMCameraControl> camera;
        if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&camera)))) {
            range = camera->GetRange(definition.property, &minimum, &maximum, &step, &defaultValue, &capabilities);
            current = camera->Get(definition.property, &value, &flags);
        }
    } else {
        ComPtr<IAMVideoProcAmp> image;
        if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&image)))) {
            range = image->GetRange(definition.property, &minimum, &maximum, &step, &defaultValue, &capabilities);
            current = image->Get(definition.property, &value, &flags);
        }
    }
    if (FAILED(range) || FAILED(current) || maximum < minimum) return false;
    // Both Windows interfaces use the same Auto=1 and Manual=2 flag values.
    control = {definition.id, definition.name, minimum, maximum, std::max(1L, step), defaultValue, value,
        (capabilities & 1) != 0, (capabilities & 2) != 0, (flags & 1) != 0};
    return true;
}

struct SourceReaderSampleEvent {
    HRESULT status = E_FAIL;
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
};

enum class SourceReaderWaitResult {
    Sample,
    Timeout,
    Cancelled
};

class SourceReaderCallback final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          IMFSourceReaderCallback> {
  public:
    STDMETHODIMP OnReadSample(HRESULT status,
                              DWORD streamIndex,
                              DWORD flags,
                              LONGLONG timestamp,
                              IMFSample *sample) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancelled_) {
                return S_OK;
            }
            SourceReaderSampleEvent event;
            event.status = status;
            event.streamIndex = streamIndex;
            event.flags = flags;
            event.timestamp = timestamp;
            event.sample = sample;
            events_.push_back(std::move(event));
        }
        condition_.notify_all();
        return S_OK;
    }

    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *) override {
        return S_OK;
    }

    STDMETHODIMP OnFlush(DWORD) override {
        condition_.notify_all();
        return S_OK;
    }

    SourceReaderWaitResult waitForSample(SourceReaderSampleEvent &event,
                                         std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, timeout, [this]() {
                return cancelled_ || !events_.empty();
            })) {
            return SourceReaderWaitResult::Timeout;
        }
        if (cancelled_) {
            return SourceReaderWaitResult::Cancelled;
        }
        event = std::move(events_.front());
        events_.pop_front();
        return SourceReaderWaitResult::Sample;
    }

    void cancel() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
            events_.clear();
        }
        condition_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<SourceReaderSampleEvent> events_;
    bool cancelled_ = false;
};

int subtypePenalty(const GUID &subtype) {
    if (IsEqualGUID(subtype, MFVideoFormat_RGB32) ||
        IsEqualGUID(subtype, MFVideoFormat_ARGB32)) {
        return 0;
    }
    if (IsEqualGUID(subtype, MFVideoFormat_NV12)) {
        return 10;
    }
    if (IsEqualGUID(subtype, MFVideoFormat_YUY2) ||
        IsEqualGUID(subtype, MFVideoFormat_UYVY)) {
        return 20;
    }
    if (IsEqualGUID(subtype, MFVideoFormat_MJPG)) {
        return 30;
    }
    if (IsEqualGUID(subtype, MFVideoFormat_I420) ||
        IsEqualGUID(subtype, MFVideoFormat_IYUV) ||
        IsEqualGUID(subtype, MFVideoFormat_YV12)) {
        return 40;
    }
    return 100;
}

int64_t mediaTypeScore(UINT32 width,
                       UINT32 height,
                       UINT32 fpsNumerator,
                       UINT32 fpsDenominator,
                       int requestedWidth,
                       int requestedHeight,
                       double requestedFps,
                       const GUID &subtype) {
    const int safeWidth = std::max(1, requestedWidth);
    const int safeHeight = std::max(1, requestedHeight);
    const double actualFps = fpsDenominator > 0
        ? static_cast<double>(fpsNumerator) / static_cast<double>(fpsDenominator)
        : 0.0;
    const double targetFps = std::max(1.0, requestedFps);

    const int64_t widthDelta = std::llabs(static_cast<int64_t>(width) - safeWidth);
    const int64_t heightDelta = std::llabs(static_cast<int64_t>(height) - safeHeight);
    const double requestedAspect = static_cast<double>(safeWidth) / static_cast<double>(safeHeight);
    const double actualAspect = height > 0
        ? static_cast<double>(width) / static_cast<double>(height)
        : requestedAspect;
    const int64_t aspectPenalty = static_cast<int64_t>(
        std::llround(std::abs(actualAspect - requestedAspect) * 2000.0));
    const int64_t fpsPenalty = static_cast<int64_t>(
        std::llround(std::abs(actualFps - targetFps) * 40.0));
    const int64_t belowTargetFpsPenalty = actualFps + 0.5 < targetFps
        ? static_cast<int64_t>(std::llround((targetFps - actualFps) * 20.0))
        : 0;

    return (widthDelta * 3) +
           (heightDelta * 3) +
           aspectPenalty +
           fpsPenalty +
           belowTargetFpsPenalty +
           subtypePenalty(subtype);
}

std::vector<NativeMediaTypeChoice> enumerateNativeTypes(IMFSourceReader *reader,
                                                         int requestedWidth,
                                                         int requestedHeight,
                                                         double requestedFps) {
    std::vector<NativeMediaTypeChoice> choices;
    if (!reader) {
        return choices;
    }

    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader->GetNativeMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, type.GetAddressOf());
        if (hr == MF_E_NO_MORE_TYPES) {
            break;
        }
        if (FAILED(hr) || !type) {
            continue;
        }

        GUID majorType = GUID_NULL;
        GUID subtype = GUID_NULL;
        UINT32 width = 0;
        UINT32 height = 0;
        UINT32 fpsNumerator = 0;
        UINT32 fpsDenominator = 1;
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &majorType)) ||
            !IsEqualGUID(majorType, MFMediaType_Video) ||
            FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
            width == 0 ||
            height == 0) {
            continue;
        }
        if (FAILED(MFGetAttributeRatio(
                type.Get(), MF_MT_FRAME_RATE, &fpsNumerator, &fpsDenominator)) ||
            fpsNumerator == 0 ||
            fpsDenominator == 0) {
            fpsNumerator = 30;
            fpsDenominator = 1;
        }

        NativeMediaTypeChoice choice;
        choice.type = type;
        choice.subtype = subtype;
        choice.width = width;
        choice.height = height;
        choice.fpsNumerator = fpsNumerator;
        choice.fpsDenominator = fpsDenominator;
        choice.score = mediaTypeScore(
            width,
            height,
            fpsNumerator,
            fpsDenominator,
            requestedWidth,
            requestedHeight,
            requestedFps,
            subtype);
        choices.push_back(std::move(choice));
    }

    std::stable_sort(choices.begin(), choices.end(), [](const auto &left, const auto &right) {
        return left.score < right.score;
    });
    return choices;
}

HRESULT configureBgraOutput(IMFSourceReader *reader,
                            const NativeMediaTypeChoice &choice,
                            ComPtr<IMFMediaType> &actualOutputType) {
    if (!reader || !choice.type) {
        return E_INVALIDARG;
    }

    HRESULT hr = reader->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, choice.type.Get());
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IMFMediaType> outputType;
    hr = MFCreateMediaType(outputType.GetAddressOf());
    if (FAILED(hr)) {
        return hr;
    }
    if (FAILED(hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(hr = MFSetAttributeSize(
            outputType.Get(), MF_MT_FRAME_SIZE, choice.width, choice.height)) ||
        FAILED(hr = MFSetAttributeRatio(
            outputType.Get(),
            MF_MT_FRAME_RATE,
            choice.fpsNumerator,
            choice.fpsDenominator)) ||
        FAILED(hr = MFSetAttributeRatio(
            outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(hr = outputType->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(hr = outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE)) ||
        FAILED(hr = outputType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE)) ||
        FAILED(hr = outputType->SetUINT32(
            MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(choice.width * 4)))) {
        return hr;
    }

    hr = reader->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.Get());
    if (FAILED(hr)) {
        return hr;
    }

    actualOutputType.Reset();
    return reader->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, actualOutputType.GetAddressOf());
}

int64_t steadyTimestamp100ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
               .count() /
           100;
}

}  // namespace

class CameraCapture::Impl {
  public:
    ~Impl() {
        stopCapture();
    }

    std::vector<WindowInfo> enumerateCameras() {
        std::vector<WindowInfo> cameras;
        ScopedComApartment apartment;
        if (!apartment.usable()) {
            setLastError("COM initialization failed (" + hresultText(apartment.result()) + ")");
            return cameras;
        }
        ScopedMediaFoundation mediaFoundation;
        if (!mediaFoundation.started()) {
            setLastError("Media Foundation startup failed (" +
                         hresultText(mediaFoundation.result()) + ")");
            return cameras;
        }

        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(attributes.GetAddressOf(), 1);
        if (SUCCEEDED(hr)) {
            hr = attributes->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        }
        if (FAILED(hr)) {
            setLastError("Camera enumeration setup failed (" + hresultText(hr) + ")");
            return cameras;
        }

        IMFActivate **devices = nullptr;
        UINT32 deviceCount = 0;
        hr = MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount);
        if (FAILED(hr)) {
            setLastError("Camera enumeration failed (" + hresultText(hr) + ")");
            return cameras;
        }

        cameras.reserve(deviceCount);
        for (UINT32 index = 0; index < deviceCount; ++index) {
            IMFActivate *device = devices[index];
            if (!device) {
                continue;
            }

            wchar_t *friendlyName = nullptr;
            wchar_t *symbolicLink = nullptr;
            UINT32 friendlyNameLength = 0;
            UINT32 symbolicLinkLength = 0;
            const HRESULT nameHr = device->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                &friendlyName,
                &friendlyNameLength);
            const HRESULT linkHr = device->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                &symbolicLink,
                &symbolicLinkLength);

            if (SUCCEEDED(nameHr) && SUCCEEDED(linkHr) && symbolicLink && *symbolicLink) {
                WindowInfo info;
                info.id = wideToUtf8(symbolicLink);
                info.name = wideToUtf8(friendlyName);
                if (info.name.empty()) {
                    info.name = "Camera";
                }
                info.executableName = "Video input device";
                cameras.push_back(std::move(info));
            }

            CoTaskMemFree(friendlyName);
            CoTaskMemFree(symbolicLink);
        }

        for (UINT32 index = 0; index < deviceCount; ++index) {
            if (devices[index]) {
                devices[index]->Release();
            }
        }
        CoTaskMemFree(devices);

        std::stable_sort(cameras.begin(), cameras.end(), [](const auto &left, const auto &right) {
            return left.name < right.name;
        });
        setLastError({});
        return cameras;
    }

    bool startCapture(const std::string &deviceId,
                      int requestedWidth,
                      int requestedHeight,
                      double requestedFps,
                      FrameCallback callback) {
        stopCapture();
        { std::lock_guard lock(settingsMutex_); settings_ = {}; }
        if (deviceId.empty()) {
            setLastError("No camera was selected");
            return false;
        }

        std::promise<bool> initializedPromise;
        auto initializedFuture = initializedPromise.get_future();
        captureThread_ = std::thread(
            [this,
             deviceId,
             requestedWidth,
             requestedHeight,
             requestedFps,
             callback = std::move(callback),
             initializedPromise = std::move(initializedPromise)]() mutable {
                captureLoop(
                    deviceId,
                    requestedWidth,
                    requestedHeight,
                    requestedFps,
                    std::move(callback),
                    std::move(initializedPromise));
            });

        constexpr auto kInitializationTimeout = std::chrono::seconds(10);
        if (initializedFuture.wait_for(kInitializationTimeout) != std::future_status::ready) {
            setLastError(
                "Camera startup did not complete within 10 seconds. The source may be inactive, disconnected, or blocked by its driver. Start the camera source or choose a different camera, then try again.");
            stopCapture();
            return false;
        }

        bool initialized = false;
        try {
            initialized = initializedFuture.get();
        } catch (const std::future_error &error) {
            setLastError(
                std::string("Camera startup did not complete: ") + error.what());
        }
        if (!initialized && captureThread_.joinable()) {
            captureThread_.join();
        }
        return initialized;
    }

    void stopCapture() {
        capturing_.store(false, std::memory_order_release);
        cancelControls();

        ComPtr<IMFSourceReader> reader;
        ComPtr<SourceReaderCallback> callback;
        {
            std::lock_guard<std::mutex> lock(activeSourceMutex_);
            reader = activeReader_;
            callback = activeCallback_;
        }
        if (callback) {
            callback->cancel();
        }
        if (reader) {
            reader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        }

        if (captureThread_.joinable()) {
            if (captureThread_.get_id() == std::this_thread::get_id()) {
                captureThread_.detach();
            } else {
                captureThread_.join();
            }
        }

        std::lock_guard<std::mutex> lock(activeSourceMutex_);
        activeReader_.Reset();
        activeSource_.Reset();
        activeCallback_.Reset();
    }

    bool isCapturing() const {
        return capturing_.load(std::memory_order_acquire);
    }

    std::string lastError() const {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastError_;
    }

    CameraSettings settings() const {
        std::lock_guard lock(settingsMutex_);
        return settings_;
    }

    std::future<CameraControlResult> setControl(const std::string &id, int value, bool automatic) {
        ControlRequest request{id, value, automatic, {}};
        auto future = request.result.get_future();
        std::lock_guard lock(settingsMutex_);
        if (!capturing_ || requests_.size() >= 64) {
            request.result.set_value({false, {}, "Camera unavailable or still applying settings. Reconnect and try again."});
        } else requests_.push_back(std::move(request));
        return future;
    }

  private:
    struct ControlRequest {
        std::string id;
        int value;
        bool automatic;
        std::promise<CameraControlResult> result;
    };

    void cancelControls() {
        std::lock_guard lock(settingsMutex_);
        for (auto &request : requests_) request.result.set_value({false, {}, "Camera stopped before the setting was applied."});
        requests_.clear();
    }

    void processControls(IMFMediaSource *source) {
        std::deque<ControlRequest> requests;
        { std::lock_guard lock(settingsMutex_); requests.swap(requests_); }
        for (auto &request : requests) {
            CameraControlResult result;
            const auto definition = std::find_if(std::begin(controlDefinitions), std::end(controlDefinitions),
                [&](const auto &entry) { return request.id == entry.id; });
            if (!capturing_ || definition == std::end(controlDefinitions) || !readControl(source, *definition, result.control)) {
                result.error = "This camera does not expose that setting. Reconnect if it was disconnected.";
            } else if ((request.automatic && !result.control.supportsAuto) || (!request.automatic && !result.control.supportsManual)) {
                result.error = "The camera does not support the selected automatic/manual mode.";
            } else if (!request.automatic && (request.value < result.control.minimum || request.value > result.control.maximum ||
                (static_cast<int64_t>(request.value) - result.control.minimum) % result.control.step != 0)) {
                result.error = "Choose a value within the camera's range and step size.";
            } else {
                HRESULT hr = E_NOINTERFACE;
                const long flags = request.automatic ? 1 : 2;
                if (definition->camera) {
                    ComPtr<IAMCameraControl> camera;
                    if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&camera)))) hr = camera->Set(definition->property, request.value, flags);
                } else {
                    ComPtr<IAMVideoProcAmp> image;
                    if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&image)))) hr = image->Set(definition->property, request.value, flags);
                }
                const bool readBack = readControl(source, *definition, result.control);
                if (FAILED(hr)) result.error = "Camera rejected the setting (" + hresultText(hr) + ").";
                else if (!readBack) result.error = "Camera accepted the setting but its current value could not be read.";
                else result.success = true;
                if (readBack) {
                    std::lock_guard lock(settingsMutex_);
                    for (auto &control : settings_.controls) if (control.id == request.id) control = result.control;
                }
            }
            request.result.set_value(std::move(result));
        }
    }

    void setLastError(std::string error) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = std::move(error);
    }

    bool copySampleToFrame(IMFSample *sample,
                           int width,
                           int height,
                           LONG defaultStride,
                           int64_t timestamp,
                           CapturedFrame &frame) {
        if (!sample || width <= 0 || height <= 0) {
            return false;
        }

        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
        if (FAILED(hr) || !buffer) {
            return false;
        }

        frame = CapturedFrame{};
        frame.width = width;
        frame.height = height;
        frame.stride = width * 4;
        frame.timestamp = timestamp > 0 ? timestamp : steadyTimestamp100ns();
        frame.format = CapturedFrame::Format::BGRA;
        frame.data.resize(
            static_cast<size_t>(frame.stride) * static_cast<size_t>(frame.height));

        BYTE *topScanline = nullptr;
        LONG sourceStride = 0;
        ComPtr<IMF2DBuffer> buffer2d;
        if (SUCCEEDED(buffer.As(&buffer2d)) && buffer2d &&
            SUCCEEDED(buffer2d->Lock2D(&topScanline, &sourceStride))) {
            const bool validStride = topScanline &&
                std::llabs(static_cast<int64_t>(sourceStride)) >= frame.stride;
            if (validStride) {
                for (int y = 0; y < frame.height; ++y) {
                    const BYTE *sourceRow =
                        topScanline + static_cast<ptrdiff_t>(y) * sourceStride;
                    BYTE *destinationRow =
                        frame.data.data() + static_cast<size_t>(y) * frame.stride;
                    std::memcpy(destinationRow, sourceRow, static_cast<size_t>(frame.stride));
                }
            }
            buffer2d->Unlock2D();
            if (!validStride) {
                return false;
            }
        } else {
            BYTE *data = nullptr;
            DWORD maximumLength = 0;
            DWORD currentLength = 0;
            hr = buffer->Lock(&data, &maximumLength, &currentLength);
            if (FAILED(hr) || !data) {
                return false;
            }

            sourceStride = defaultStride != 0 ? defaultStride : frame.stride;
            const size_t absoluteStride = static_cast<size_t>(
                std::llabs(static_cast<int64_t>(sourceStride)));
            const size_t requiredLength =
                absoluteStride * static_cast<size_t>(std::max(0, frame.height - 1)) +
                static_cast<size_t>(frame.stride);
            const bool validBuffer =
                absoluteStride >= static_cast<size_t>(frame.stride) &&
                currentLength >= requiredLength;
            if (validBuffer) {
                const BYTE *topRow = sourceStride >= 0
                    ? data
                    : data + absoluteStride * static_cast<size_t>(frame.height - 1);
                for (int y = 0; y < frame.height; ++y) {
                    const BYTE *sourceRow =
                        topRow + static_cast<ptrdiff_t>(y) * sourceStride;
                    BYTE *destinationRow =
                        frame.data.data() + static_cast<size_t>(y) * frame.stride;
                    std::memcpy(destinationRow, sourceRow, static_cast<size_t>(frame.stride));
                }
            }
            buffer->Unlock();
            if (!validBuffer) {
                return false;
            }
        }

        for (size_t pixel = 3; pixel < frame.data.size(); pixel += 4) {
            frame.data[pixel] = 255;
        }
        frame.opaque = true;
        return true;
    }

    void captureLoop(const std::string &deviceId,
                     int requestedWidth,
                     int requestedHeight,
                     double requestedFps,
                     FrameCallback callback,
                     std::promise<bool> initializedPromise) {
        bool initializationReported = false;
        auto reportInitialization = [&](bool success) {
            if (!initializationReported) {
                initializedPromise.set_value(success);
                initializationReported = true;
            }
        };
        auto failInitialization = [&](const std::string &message) {
            setLastError(message);
            spdlog::warn("[CameraCapture] {}", message);
            reportInitialization(false);
        };

        ScopedComApartment apartment;
        if (!apartment.usable()) {
            failInitialization(
                "COM initialization failed (" + hresultText(apartment.result()) + ")");
            return;
        }
        ScopedMediaFoundation mediaFoundation;
        if (!mediaFoundation.started()) {
            failInitialization(
                "Media Foundation startup failed (" +
                hresultText(mediaFoundation.result()) + ")");
            return;
        }

        const std::wstring deviceIdWide = utf8ToWide(deviceId);
        if (deviceIdWide.empty()) {
            failInitialization("The selected camera identifier is invalid");
            return;
        }

        ComPtr<IMFAttributes> sourceAttributes;
        HRESULT hr = MFCreateAttributes(sourceAttributes.GetAddressOf(), 2);
        if (SUCCEEDED(hr)) {
            hr = sourceAttributes->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        }
        if (SUCCEEDED(hr)) {
            hr = sourceAttributes->SetString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                deviceIdWide.c_str());
        }
        if (FAILED(hr)) {
            failInitialization("Camera source setup failed (" + hresultText(hr) + ")");
            return;
        }

        ComPtr<IMFMediaSource> source;
        hr = MFCreateDeviceSource(sourceAttributes.Get(), source.GetAddressOf());
        if (FAILED(hr) || !source) {
            failInitialization(
                "Could not open the selected camera (" + hresultText(hr) +
                "). Check Windows camera privacy settings and whether another app owns the device.");
            return;
        }

        ComPtr<SourceReaderCallback> sampleCallback =
            Microsoft::WRL::Make<SourceReaderCallback>();
        if (!sampleCallback) {
            source->Shutdown();
            failInitialization("Could not allocate the camera reader callback");
            return;
        }

        ComPtr<IMFAttributes> readerAttributes;
        hr = MFCreateAttributes(readerAttributes.GetAddressOf(), 4);
        if (SUCCEEDED(hr)) {
            hr = readerAttributes->SetUINT32(
                MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        }
        if (SUCCEEDED(hr)) {
            hr = readerAttributes->SetUINT32(
                MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE);
        }
        if (SUCCEEDED(hr)) {
            hr = readerAttributes->SetUnknown(
                MF_SOURCE_READER_ASYNC_CALLBACK,
                sampleCallback.Get());
        }
        if (FAILED(hr)) {
            source->Shutdown();
            failInitialization("Camera reader setup failed (" + hresultText(hr) + ")");
            return;
        }

        ComPtr<IMFSourceReader> reader;
        hr = MFCreateSourceReaderFromMediaSource(
            source.Get(), readerAttributes.Get(), reader.GetAddressOf());
        if (FAILED(hr) || !reader) {
            source->Shutdown();
            failInitialization("Could not create a camera reader (" + hresultText(hr) + ")");
            return;
        }

        reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (FAILED(hr)) {
            source->Shutdown();
            failInitialization("The camera has no readable video stream (" + hresultText(hr) + ")");
            return;
        }

        const auto choices = enumerateNativeTypes(
            reader.Get(),
            std::max(1, requestedWidth),
            std::max(1, requestedHeight),
            std::max(1.0, requestedFps));
        if (choices.empty()) {
            source->Shutdown();
            failInitialization("The camera reported no supported video formats");
            return;
        }

        ComPtr<IMFMediaType> outputType;
        const NativeMediaTypeChoice *selectedChoice = nullptr;
        HRESULT lastTypeError = MF_E_INVALIDMEDIATYPE;
        for (const auto &choice : choices) {
            outputType.Reset();
            lastTypeError = configureBgraOutput(reader.Get(), choice, outputType);
            if (SUCCEEDED(lastTypeError) && outputType) {
                selectedChoice = &choice;
                break;
            }
        }
        if (!selectedChoice || !outputType) {
            source->Shutdown();
            failInitialization(
                "The camera could not provide a BGRA video format (" +
                hresultText(lastTypeError) + ")");
            return;
        }

        UINT32 activeWidth = selectedChoice->width;
        UINT32 activeHeight = selectedChoice->height;
        MFGetAttributeSize(
            outputType.Get(), MF_MT_FRAME_SIZE, &activeWidth, &activeHeight);
        UINT32 storedStride = activeWidth * 4;
        outputType->GetUINT32(MF_MT_DEFAULT_STRIDE, &storedStride);
        LONG activeStride = static_cast<LONG>(storedStride);

        {
            std::lock_guard<std::mutex> lock(activeSourceMutex_);
            activeSource_ = source;
            activeReader_ = reader;
            activeCallback_ = sampleCallback;
        }
        setLastError({});
        capturing_.store(true, std::memory_order_release);

        const double activeFps = selectedChoice->fpsDenominator > 0
            ? static_cast<double>(selectedChoice->fpsNumerator) /
                static_cast<double>(selectedChoice->fpsDenominator)
            : 0.0;
        CameraSettings settings;
        settings.activeFormat = {static_cast<int>(activeWidth), static_cast<int>(activeHeight), activeFps};
        for (const auto &choice : choices) {
            const CameraFormat format{static_cast<int>(choice.width), static_cast<int>(choice.height),
                static_cast<double>(choice.fpsNumerator) / choice.fpsDenominator};
            if (std::none_of(settings.formats.begin(), settings.formats.end(), [&](const auto &entry) {
                return entry.width == format.width && entry.height == format.height && std::abs(entry.fps-format.fps) < .001;
            })) settings.formats.push_back(format);
        }
        std::sort(settings.formats.begin(), settings.formats.end(), [](const auto &a, const auto &b) {
            if (a.width != b.width) return a.width > b.width;
            if (a.height != b.height) return a.height > b.height;
            return a.fps > b.fps;
        });
        for (const auto &definition : controlDefinitions) {
            CameraControl control;
            if (readControl(source.Get(), definition, control)) settings.controls.push_back(std::move(control));
        }
        { std::lock_guard lock(settingsMutex_); settings_ = std::move(settings); }
        spdlog::info(
            "[CameraCapture] Opening {}x{} at {:.2f} fps; waiting for first frame",
            activeWidth,
            activeHeight,
            activeFps);

        const auto firstFrameDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(8);
        hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (FAILED(hr)) {
            failInitialization(cameraReadError(hr, true));
            capturing_.store(false, std::memory_order_release);
        }

        while (capturing_.load(std::memory_order_acquire)) {
            processControls(source.Get());
            SourceReaderSampleEvent event;
            const SourceReaderWaitResult waitResult =
                sampleCallback->waitForSample(event, std::chrono::milliseconds(200));
            if (waitResult == SourceReaderWaitResult::Cancelled) {
                break;
            }
            if (waitResult == SourceReaderWaitResult::Timeout) {
                if (!initializationReported &&
                    std::chrono::steady_clock::now() >= firstFrameDeadline) {
                    failInitialization(
                        "The camera did not deliver a frame within 8 seconds. It may be installed but inactive, disconnected, or already in use by another app. Start the camera source, close the other app, or choose a different camera, then try again.");
                    break;
                }
                continue;
            }

            hr = event.status;
            if (FAILED(hr)) {
                if (capturing_.load(std::memory_order_acquire)) {
                    const std::string message =
                        cameraReadError(hr, !initializationReported);
                    if (!initializationReported) {
                        failInitialization(message);
                    } else {
                        setLastError(message);
                        spdlog::warn("[CameraCapture] {}", message);
                    }
                }
                break;
            }
            if ((event.flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
                if (capturing_.load(std::memory_order_acquire)) {
                    const std::string message = initializationReported
                        ? "The camera video stream ended"
                        : "The selected camera ended its video stream before delivering a frame";
                    if (!initializationReported) {
                        failInitialization(message);
                    } else {
                        setLastError(message);
                        spdlog::warn("[CameraCapture] {}", message);
                    }
                }
                break;
            }
            if ((event.flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
                ComPtr<IMFMediaType> changedType;
                if (SUCCEEDED(reader->GetCurrentMediaType(
                        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                        changedType.GetAddressOf())) &&
                    changedType) {
                    MFGetAttributeSize(
                        changedType.Get(), MF_MT_FRAME_SIZE, &activeWidth, &activeHeight);
                    storedStride = activeWidth * 4;
                    changedType->GetUINT32(MF_MT_DEFAULT_STRIDE, &storedStride);
                    activeStride = static_cast<LONG>(storedStride);
                }
            }
            if (event.sample) {
                CapturedFrame frame;
                if (!copySampleToFrame(
                        event.sample.Get(),
                        static_cast<int>(activeWidth),
                        static_cast<int>(activeHeight),
                        activeStride,
                        event.timestamp,
                        frame)) {
                    spdlog::warn("[CameraCapture] Could not copy a camera frame");
                } else {
                    if (!initializationReported) {
                        setLastError({});
                        reportInitialization(true);
                        spdlog::info(
                            "[CameraCapture] Started {}x{} at {:.2f} fps after receiving the first frame",
                            activeWidth,
                            activeHeight,
                            activeFps);
                    }

                    if (callback) {
                        try {
                            callback(std::move(frame));
                        } catch (const std::exception &error) {
                            spdlog::warn("[CameraCapture] Frame callback failed: {}", error.what());
                        } catch (...) {
                            spdlog::warn("[CameraCapture] Frame callback failed with an unknown exception");
                        }
                    }
                }
            }

            if (capturing_.load(std::memory_order_acquire)) {
                hr = reader->ReadSample(
                    MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                    0,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr);
                if (FAILED(hr)) {
                    const std::string message =
                        cameraReadError(hr, !initializationReported);
                    if (!initializationReported) {
                        failInitialization(message);
                    } else {
                        setLastError(message);
                        spdlog::warn("[CameraCapture] {}", message);
                    }
                    break;
                }
            }
        }

        if (!initializationReported) {
            if (lastError().empty()) {
                setLastError("The selected camera stopped before delivering a usable frame");
            }
            reportInitialization(false);
        }
        capturing_.store(false, std::memory_order_release);
        cancelControls();
        sampleCallback->cancel();
        reader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        source->Shutdown();
        {
            std::lock_guard<std::mutex> lock(activeSourceMutex_);
            activeReader_.Reset();
            activeSource_.Reset();
            activeCallback_.Reset();
        }
        spdlog::info("[CameraCapture] Stopped");
    }

    std::atomic<bool> capturing_{false};
    std::thread captureThread_;
    mutable std::mutex activeSourceMutex_;
    ComPtr<IMFMediaSource> activeSource_;
    ComPtr<IMFSourceReader> activeReader_;
    ComPtr<SourceReaderCallback> activeCallback_;
    mutable std::mutex errorMutex_;
    std::string lastError_;
    mutable std::mutex settingsMutex_;
    CameraSettings settings_;
    std::deque<ControlRequest> requests_;
};

#else

class CameraCapture::Impl {
  public:
    std::vector<WindowInfo> enumerateCameras() { return {}; }
    bool startCapture(const std::string &, int, int, double, FrameCallback) { return false; }
    void stopCapture() {}
    bool isCapturing() const { return false; }
    std::string lastError() const { return "Camera capture is only available on Windows"; }
    CameraSettings settings() const { return {}; }
    std::future<CameraControlResult> setControl(const std::string &, int, bool) {
        std::promise<CameraControlResult> result; auto future = result.get_future();
        result.set_value({false, {}, lastError()}); return future;
    }
};

#endif

CameraCapture::CameraCapture()
    : impl_(std::make_unique<Impl>()) {}

CameraCapture::~CameraCapture() {
    stopCapture();
}

std::vector<WindowInfo> CameraCapture::getCameras() {
    if (!mixer::ProcessWorker::inWorker()) return mixer::IsolatedSource::cameras();
    return impl_->enumerateCameras();
}

bool CameraCapture::startCapture(const std::string &deviceId, int width, int height, double fps) {
    if (!mixer::ProcessWorker::inWorker()) {
        worker_ = std::make_unique<mixer::IsolatedSource>("camera"); worker_->setFrameCallback(frameCallback_);
        return worker_->startCapture(deviceId, width, height, fps);
    }
    return impl_->startCapture(deviceId, width, height, fps, frameCallback_);
}

void CameraCapture::stopCapture() {
    if (worker_) worker_->stopCapture();
    impl_->stopCapture();
}

bool CameraCapture::isCapturing() const {
    if (worker_) return worker_->isCapturing();
    return impl_->isCapturing();
}

std::string CameraCapture::lastError() const {
    if (worker_) return worker_->lastError();
    return impl_->lastError();
}

CameraSettings CameraCapture::settings() const { return worker_ ? worker_->settings() : impl_->settings(); }
std::future<CameraControlResult> CameraCapture::setControl(const std::string &id, int value, bool automatic) {
    if (worker_) return worker_->setControl(id, value, automatic);
    return impl_->setControl(id, value, automatic);
}

void CameraCapture::setFrameCallback(FrameCallback cb) {
    frameCallback_ = std::move(cb);
}

}  // namespace versus::video
