#include "versus/mixer/process_worker.h"
#include "versus/app/performance_trace.h"
#include "versus/mixer/browser_source.h"
#include "versus/mixer/media_source.h"
#include "versus/mixer/audio_monitor.h"
#include "versus/video/camera_capture.h"
#include "versus/video/spout_capture.h"
#include "versus/video/video_encoder.h"
#include "versus/output/output_session.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QWinEventNotifier>
#include <QtEndian>
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <cmath>
#include <spdlog/spdlog.h>

namespace versus::mixer {
namespace {
constexpr size_t BlobCapacity = 8192ull * 8192 * 4;
constexpr size_t JsonCapacity = 65536;
constexpr size_t AudioCapacity = 19200; // 200 ms of stereo 48 kHz
bool workerProcess = false;
struct Shared {
    uint32_t magic, commandSize, replySize, inputSize, outputSize;
    char command[JsonCapacity], reply[JsonCapacity];
    uint64_t frameSerial, contentSerial;
    int width, height, stride, format, opaque;
    int64_t timestamp;
    uint32_t frameSize, audioSize, sampleRate, channels;
    int64_t audioTimestamp;
    float audio[AudioCapacity];
    uint32_t statusSize;
    char status[JsonCapacity];
    uint32_t blobCapacity, pixelCapacity;
};
unsigned char *blobData(Shared *s) { return reinterpret_cast<unsigned char*>(s)+sizeof(Shared); }
unsigned char *pixelData(Shared *s) { return blobData(s)+s->blobCapacity; }
constexpr uint32_t Magic = 0x44535431;
void close(HANDLE &handle) { if (handle) CloseHandle(handle); handle = nullptr; }
bool lock(HANDLE mutex, DWORD timeout = 0) {
    const auto result = WaitForSingleObject(mutex, timeout);
    return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
}
QJsonObject parse(const char *data, uint32_t size) {
    if (size >= JsonCapacity) return {};
    return QJsonDocument::fromJson(QByteArray::fromRawData(data, size)).object();
}
bool json(char *target, uint32_t &size, const QJsonObject &value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (bytes.size() >= JsonCapacity) return false;
    memcpy(target, bytes.constData(), bytes.size()); size = uint32_t(bytes.size()); return true;
}
QJsonObject controlJson(const video::CameraControl &c) {
    return {{"id", QString::fromStdString(c.id)}, {"name", QString::fromStdString(c.name)},
        {"minimum", c.minimum}, {"maximum", c.maximum}, {"step", c.step}, {"default", c.defaultValue},
        {"value", c.value}, {"supportsAuto", c.supportsAuto}, {"supportsManual", c.supportsManual}, {"automatic", c.automatic}};
}
QJsonObject settingsJson(const video::CameraSettings &s) {
    auto format = [](const video::CameraFormat &f) { return QJsonObject{{"width", f.width}, {"height", f.height}, {"fps", f.fps}}; };
    QJsonArray formats, controls;
    for (const auto &f : s.formats) formats.append(format(f));
    for (const auto &c : s.controls) controls.append(controlJson(c));
    return {{"activeFormat", format(s.activeFormat)}, {"formats", formats}, {"controls", controls}};
}
}

struct ProcessWorker::Impl {
    HANDLE mapping = nullptr, request = nullptr, response = nullptr, mutex = nullptr, mediaReady = nullptr;
    HANDLE process = nullptr, job = nullptr;
    Shared *shared = nullptr;
    uint64_t serial = 0;
    uint64_t imageContentSerial = 0;
    QImage cachedImage;
    std::array<QImage, 3> imagePool;
    size_t blobCapacity = 0, pixelCapacity = 0;
    size_t committedBlob = 0;
    QString error;
    QString kind;
    ~Impl() {
        // Closing the job also removes orphaned FFmpeg/WebView2 descendants.
        close(job); close(process);
        if (shared) UnmapViewOfFile(shared);
        close(mapping); close(request); close(response); close(mutex); close(mediaReady);
    }
};
ProcessWorker::ProcessWorker() : impl_(std::make_unique<Impl>()) {}
ProcessWorker::~ProcessWorker() { stop(); }
bool ProcessWorker::inWorker() { return workerProcess; }
bool ProcessWorker::alive() const { return impl_->process && WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT; }
void ProcessWorker::terminate() { if (impl_->job) TerminateJobObject(impl_->job, 1); }
QString ProcessWorker::error() const { return impl_->error; }
bool ProcessWorker::start(const QString &kind) {
    stop(); impl_ = std::make_unique<Impl>(); auto &s = *impl_;
    s.kind = kind;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    s.blobCapacity = kind == "encoder" ? BlobCapacity : kind == "output" ? 8*1024*1024 : kind == "monitor" ? 960*sizeof(float) : 0;
    s.pixelCapacity = (kind == "camera" || kind == "window" || kind == "spout" || kind == "media" || kind == "browser") ? BlobCapacity : 0;
    const auto mappingSize = sizeof(Shared)+s.blobCapacity+s.pixelCapacity;
    // Reserve an address range; commit only the media bytes actually used.
    // This also preserves large source support without charging every 720p
    // source for an 8K buffer. Pagefile-backed views stay coherent across helpers.
    s.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE | SEC_RESERVE, 0, DWORD(mappingSize), nullptr);
    s.request = CreateEventW(&attributes, FALSE, FALSE, nullptr);
    s.response = CreateEventW(&attributes, FALSE, FALSE, nullptr);
    s.mutex = CreateMutexW(&attributes, FALSE, nullptr);
    s.mediaReady = CreateEventW(&attributes, FALSE, FALSE, nullptr);
    s.job = CreateJobObjectW(nullptr, nullptr);
    if (!s.mapping || !s.request || !s.response || !s.mutex || !s.mediaReady || !s.job) { s.error = "Could not allocate worker handles."; return false; }
    s.shared = static_cast<Shared*>(MapViewOfFile(s.mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!s.shared) { s.error = "Could not allocate worker memory."; return false; }
    if (!VirtualAlloc(s.shared, sizeof(Shared), MEM_COMMIT, PAGE_READWRITE)) { s.error = "Could not commit worker metadata."; return false; }
    s.shared->magic = Magic; s.shared->blobCapacity = uint32_t(s.blobCapacity); s.shared->pixelCapacity = uint32_t(s.pixelCapacity);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (kind == "browser") {
        // Bound the complete source tree, including Chromium workers/GPU/audio
        // children. Keep at least half the machine available to the mixer and
        // other sources; a single page never gets more than four logical CPUs.
        const DWORD processors = std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
        cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        cpu.CpuRate = std::max<DWORD>(1, std::min<DWORD>(5000, 40000 / processors));
        if (!SetInformationJobObject(s.job, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu))) {
            s.error = "Could not apply browser CPU protection."; return false;
        }
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (!GlobalMemoryStatusEx(&memory)) { s.error = "Could not determine the browser memory limit."; return false; }
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
        limits.JobMemoryLimit = SIZE_T(std::clamp<ULONGLONG>(memory.ullTotalPhys / 4, 1ull << 30, 4ull << 30));
    }
    if (!SetInformationJobObject(s.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        s.error = "Could not apply worker resource protection."; return false;
    }
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) return false;
    HANDLE handles[]{s.mapping, s.request, s.response, s.mutex, s.mediaReady};
    const bool updated = UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        handles, sizeof(handles), nullptr, nullptr);
    // Native gates/benchmarks use the same built helper as the shipped app.
    const auto program = QDir(QCoreApplication::applicationDirPath()).filePath("desktop-studio.exe");
    const auto executable = program.toStdWString();
    auto command = ("\"" + program + "\" --studio-worker " + kind + " " +
        QString::number(quintptr(s.mapping)) + " " + QString::number(quintptr(s.request)) + " " +
        QString::number(quintptr(s.response)) + " " + QString::number(quintptr(s.mutex)) + " " + QString::number(quintptr(s.mediaReady))).toStdWString();
    PROCESS_INFORMATION process{};
    const bool created = updated && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    // Do not leave inherited handles available to unrelated later children.
    for (auto handle : handles) SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);
    if (!created) { s.error = "Could not launch isolated worker."; return false; }
    s.process = process.hProcess;
    if (!AssignProcessToJobObject(s.job, s.process)) { TerminateProcess(s.process, 1); CloseHandle(process.hThread); s.error = "Could not isolate worker process."; return false; }
    ResumeThread(process.hThread); CloseHandle(process.hThread);
    const HANDLE waits[]{s.response, s.process};
    if (WaitForMultipleObjects(2, waits, FALSE, 15000) != WAIT_OBJECT_0) { s.error = "Worker did not start."; TerminateJobObject(s.job, 1); return false; }
    return true;
}
QJsonObject ProcessWorker::call(const QJsonObject &command, const void *data, size_t size, QByteArray *reply, int timeoutMs) {
    std::lock_guard guard(calls_); auto &s = *impl_;
    if (!alive() || size > s.blobCapacity || (size && !data)) return {{"ok", false}, {"error", "Isolated worker stopped."}};
    if (!json(s.shared->command, s.shared->commandSize, command)) return {{"ok", false}};
    if (size > s.committedBlob) {
        if (!VirtualAlloc(blobData(s.shared), size, MEM_COMMIT, PAGE_READWRITE)) { s.error = "Not enough memory for the worker frame."; return {{"ok", false}, {"error", s.error}}; }
        s.committedBlob = size;
    }
    if (size) memcpy(blobData(s.shared), data, size);
    s.shared->inputSize = uint32_t(size); s.shared->outputSize = 0;
    SetEvent(s.request);
    const HANDLE waits[]{s.response, s.process};
    if (WaitForMultipleObjects(2, waits, FALSE, DWORD(timeoutMs)) != WAIT_OBJECT_0) {
        s.error = "Isolated worker stopped or became unresponsive.";
        TerminateJobObject(s.job, 1); return {{"ok", false}, {"error", s.error}};
    }
    const auto outputSize = s.shared->outputSize;
    if (outputSize > s.blobCapacity) { TerminateJobObject(s.job, 1); return {{"ok", false}}; }
    if (reply) *reply = QByteArray(reinterpret_cast<const char*>(blobData(s.shared)), outputSize);
    return parse(s.shared->reply, s.shared->replySize);
}
QJsonObject ProcessWorker::stop() {
    if (alive()) {
        const auto result = call({{"op", "stop"}}, nullptr, 0, nullptr, impl_->kind == "output" ? 12000 : 2000);
        if (WaitForSingleObject(impl_->process, 3000) == WAIT_TIMEOUT) TerminateJobObject(impl_->job, 0);
        return result;
    }
    return {{"ok", false}};
}
bool ProcessWorker::frame(video::CapturedFrame &frame) {
    auto &s = *impl_; if (!alive() || !lock(s.mutex)) return false;
    auto &v = *s.shared;
    const auto size = v.frameSize; const auto serial = v.frameSerial;
    const int width = v.width, height = v.height, stride = v.stride, format = v.format, opaque = v.opaque;
    const auto timestamp = v.timestamp;
    const bool valid = serial != s.serial && size > 0 && size <= s.pixelCapacity &&
        width > 0 && width <= 8192 && height > 0 && height <= 8192 && stride >= width * 4 &&
        uint64_t(stride) * height == size && format == int(video::CapturedFrame::Format::BGRA);
    if (valid) {
        frame.width = width; frame.height = height; frame.stride = stride; frame.timestamp = timestamp;
        frame.format = video::CapturedFrame::Format(format); frame.opaque = opaque != 0;
        const auto *pixels = blobData(&v)+s.blobCapacity;
        frame.data.assign(pixels, pixels + size); s.serial = serial;
    }
    ReleaseMutex(s.mutex); return valid;
}
QImage ProcessWorker::image(bool premultiplied) {
    auto &s = *impl_; if (!alive() || !lock(s.mutex)) return {};
    const auto &v = *s.shared; const auto serial = v.frameSerial, content = v.contentSerial;
    const auto size = v.frameSize; const int width = v.width, height = v.height, stride = v.stride, opaque = v.opaque;
    QImage result;
    if (serial != s.serial && width > 0 && width <= 8192 && height > 0 && height <= 8192 && stride >= width*4 &&
        uint64_t(stride)*height == size && size <= s.pixelCapacity && v.format == int(video::CapturedFrame::Format::BGRA)) {
        if (content != s.imageContentSerial) {
            app::detail::PerformanceTrace::Scope profile(app::detail::PerformanceTrace::IpcCopy);
            const auto format = opaque ? QImage::Format_RGB32 : premultiplied ? QImage::Format_ARGB32_Premultiplied : QImage::Format_ARGB32;
            QImage *available = nullptr;
            for (auto &candidate : s.imagePool) if (candidate.isNull() || candidate.isDetached()) { available = &candidate; break; }
            if (available) {
                if (available->size() != QSize(width, height) || available->format() != format) *available = QImage(width, height, format);
                if (available->isNull()) { ReleaseMutex(s.mutex); return {}; }
                const auto *pixels = blobData(s.shared)+s.blobCapacity;
                auto *destination = available->bits();
                const auto destinationStride = available->bytesPerLine();
                if (stride == destinationStride) memcpy(destination, pixels, size_t(stride)*height);
                else for (int y=0; y<height; ++y) memcpy(destination+size_t(y)*destinationStride, pixels+size_t(y)*stride, size_t(width)*4);
                s.cachedImage = *available;
            } else s.cachedImage = QImage(blobData(s.shared)+s.blobCapacity, width, height, stride, format).copy();
            s.imageContentSerial = content;
        }
        s.serial = serial; result = s.cachedImage;
    }
    ReleaseMutex(s.mutex); return result;
}
bool ProcessWorker::audio(audio::StreamChunk &chunk) {
    auto &s = *impl_; if (!alive() || !lock(s.mutex)) return false;
    auto &v = *s.shared;
    const auto count = v.audioSize, rate = v.sampleRate, channels = v.channels;
    const bool valid = count && count <= AudioCapacity && rate >= 8000 && rate <= 192000 && channels > 0 && channels <= 8;
    if (valid) {
        chunk.samples.assign(v.audio, v.audio + count); chunk.sampleRate = rate; chunk.channels = channels; chunk.captureTime100ns = v.audioTimestamp;
        for (auto &sample : chunk.samples) if (!std::isfinite(sample)) sample = 0;
    }
    v.audioSize = 0; ReleaseMutex(s.mutex); return valid;
}
void ProcessWorker::waitForMedia(int timeoutMs) {
    if (!alive()) return;
    const HANDLE waits[]{impl_->mediaReady, impl_->process};
    WaitForMultipleObjects(2, waits, FALSE, DWORD(timeoutMs));
}
QJsonObject ProcessWorker::status() const {
    auto &s = *impl_;
    if (!alive()) return {{"active", false}, {"error", "Isolated worker stopped. Reconnecting..."}};
    if (!lock(s.mutex)) return {};
    auto result = parse(s.shared->status, s.shared->statusSize); ReleaseMutex(s.mutex); return result;
}

int ProcessWorker::run(int argc, char **argv) {
    if (argc != 8) return 2;
    workerProcess = true;
    if (const auto *profile = std::getenv("VIDEO_MIXER_PROFILE"); profile && *profile) {
        const auto file = std::string(profile)+".worker-"+argv[2]+"-"+std::to_string(GetCurrentProcessId())+".csv";
        _putenv_s("VIDEO_MIXER_PROFILE", file.c_str());
    }
    // Worker processes never open the show's settings, control server, or log.
    // Handles are inherited only by this process, never by browser/FFmpeg children.
    std::array<HANDLE, 5> handles{};
    for (int i = 0; i < 5; ++i) {
        bool valid = false; const auto value = QString::fromLocal8Bit(argv[i+3]).toULongLong(&valid);
        if (!valid || !value) return 2;
        handles[i] = reinterpret_cast<HANDLE>(quintptr(value));
        SetHandleInformation(handles[i], HANDLE_FLAG_INHERIT, 0);
    }
    auto *shared = static_cast<Shared*>(MapViewOfFile(handles[0], FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!shared || shared->magic != Magic || shared->blobCapacity > BlobCapacity || shared->pixelCapacity > BlobCapacity) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // Windows 10 2004+ scopes timer resolution to each process. The main
    // canvas's precise timer does not cover helpers: FFmpeg's 1 ms pipe waits
    // otherwise round up and can consume an entire 60 fps frame interval.
    const bool preciseClock = timeBeginPeriod(1) == TIMERR_NOERROR;
    // These hidden processes produce the visible, real-time Program. Do not
    // let window-occlusion heuristics treat them as deferrable background work.
    PROCESS_POWER_THROTTLING_STATE power{};
    power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &power, sizeof(power))) {
        power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &power, sizeof(power));
    }
    spdlog::set_level(spdlog::level::off);
    const QString kind = QString::fromLocal8Bit(argv[2]);
    const HRESULT apartment = CoInitializeEx(nullptr, kind == "browser" ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    std::unique_ptr<QCoreApplication> app;
    if (kind == "browser") { auto gui = std::make_unique<QApplication>(argc, argv); gui->setQuitOnLastWindowClosed(false); app = std::move(gui); }
    else app = std::make_unique<QCoreApplication>(argc, argv);
    auto &application = *app;
    // Dispatch is deliberately a small fixed vocabulary, not a general native bridge.
    std::unique_ptr<video::CameraCapture> camera;
    std::unique_ptr<video::WindowCapture> window;
    std::unique_ptr<video::SpoutCapture> spout;
    std::unique_ptr<audio::WindowAudioCaptureCore> captureAudio;
    std::unique_ptr<MediaSource> media;
    std::unique_ptr<AudioMonitor> monitor;
    std::vector<float> monitorSamples(960);
    std::unique_ptr<BrowserSource> browser;
    std::unique_ptr<video::VideoEncoder> encoder;
    video::CapturedFrame encoderFrame; // Reuse capacity across frames.
    std::unique_ptr<output::Session> output;
    std::atomic<qint64> keyframes{0};
    QJsonObject state{{"active", false}};
    QElapsedTimer browserCpuAt;
    qint64 browserCpuTicks = 0;
    int browserCpuBusySamples = 0;
    double browserCpuCores = 0;
    if (kind == "browser") {
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        if (QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu), nullptr) &&
            QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr)) {
            state["cpuLimitPercent"] = cpu.CpuRate / 100.0;
            state["memoryLimitMiB"] = qint64(limits.JobMemoryLimit / (1024 * 1024));
            browserCpuCores = cpu.CpuRate / 10000.0 * GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            browserCpuAt.start();
        }
    }
    qint64 browserImageKey = 0;
    size_t committedPixels = 0;
    auto publishState = [&] { if (lock(handles[3])) { json(shared->status, shared->statusSize, state); ReleaseMutex(handles[3]); } };
    auto frameBytes = [&](const video::CapturedFrame &f, const unsigned char *data, size_t size) {
        app::detail::PerformanceTrace::Scope profile(app::detail::PerformanceTrace::IpcCopy);
        if (size > shared->pixelCapacity) return false;
        if (size > committedPixels) {
            if (!VirtualAlloc(pixelData(shared), size, MEM_COMMIT, PAGE_READWRITE)) return false;
            committedPixels = size;
        }
        if (size > shared->pixelCapacity || f.format != video::CapturedFrame::Format::BGRA || !lock(handles[3])) return false;
        shared->width = f.width; shared->height = f.height; shared->stride = f.stride;
        shared->format = int(f.format); shared->opaque = f.opaque; shared->timestamp = f.timestamp;
        shared->frameSize = uint32_t(size); memcpy(pixelData(shared), data, size); ++shared->frameSerial; ++shared->contentSerial;
        ReleaseMutex(handles[3]); SetEvent(handles[4]); return true;
    };
    auto frame = [&](video::CapturedFrame f) { frameBytes(f, f.data.data(), f.data.size()); };
    auto audio = [&](audio::StreamChunk &&chunk) {
        if (chunk.samples.empty() || !lock(handles[3], 10)) return;
        auto &s = *shared;
        if (s.sampleRate != chunk.sampleRate || s.channels != chunk.channels || s.audioSize > AudioCapacity) s.audioSize = 0;
        s.sampleRate = chunk.sampleRate; s.channels = chunk.channels;
        const size_t count = std::min(chunk.samples.size(), AudioCapacity);
        if (s.audioSize + count > AudioCapacity) s.audioSize = 0;
        if (!s.audioSize) s.audioTimestamp = chunk.captureTime100ns;
        memcpy(s.audio+s.audioSize, chunk.samples.data()+chunk.samples.size()-count, count*sizeof(float)); s.audioSize += uint32_t(count);
        ReleaseMutex(handles[3]); SetEvent(handles[4]);
    };
    QWinEventNotifier request(handles[1]);
    QObject::connect(&request, &QWinEventNotifier::activated, &application, [&] {
        QJsonObject result{{"ok", false}};
        const auto c = parse(shared->command, shared->commandSize); const auto op = c["op"].toString();
        const auto locator = c["locator"].toString().toStdString();
        try {
            if (op == "stop") {
                result["ok"] = true; QTimer::singleShot(0, &application, &QCoreApplication::quit);
                if (output) {
                    output->stop(); const auto final = output->status();
                    result["state"] = int(final.state); result["message"] = QString::fromStdString(final.message);
                }
            }
            else if (kind == "monitor") {
                if (op == "start") {
                    monitor = std::make_unique<AudioMonitor>();
                    const auto key = c["device"].toString("default");
                    const auto devices = AudioMonitor::devices();
                    const auto found = std::find_if(devices.begin(), devices.end(), [&](const auto &d) { return d.key == key; });
                    const auto error = found == devices.end() ? QString("Selected monitoring device is unavailable. Connect it and click Refresh, or select another output.") : monitor->open(found->id);
                    result = {{"ok", error.isEmpty()}, {"error", error}};
                } else if (op == "audio" && monitor && shared->inputSize == 960*sizeof(float)) {
                    memcpy(monitorSamples.data(), blobData(shared), 960*sizeof(float));
                    result["ok"] = monitor->write(monitorSamples);
                }
            }
            else if (kind == "devices") {
                QJsonArray devices;
                if (op == "outputs") {
                    for (const auto &d : AudioMonitor::devices()) devices.append(QJsonObject{{"index", d.id}, {"id", d.key}, {"name", d.name}});
                } else if (op == "cameras") {
                    video::CameraCapture enumerator;
                    for (const auto &c : enumerator.getCameras()) devices.append(QJsonObject{{"id", QString::fromStdString(c.id)}, {"name", QString::fromStdString(c.name)}});
                } else if (op == "inputs") {
                    audio::WindowAudioCaptureCore enumerator;
                    for (const auto &c : enumerator.GetInputDevices()) devices.append(QJsonObject{{"id", QString::fromStdString(c.id)}, {"name", QString::fromStdString(c.name)},
                        {"rate", int(c.sampleRate)}, {"channels", int(c.channels)}, {"bits", int(c.bitsPerSample)}, {"validBits", int(c.validBitsPerSample)}, {"float", c.floatingPoint}, {"default", c.isDefault}});
                }
                result = {{"ok", true}, {"devices", devices}};
            }
            else if (op == "start" && kind == "camera") {
                camera = std::make_unique<video::CameraCapture>(); camera->setFrameCallback(frame);
                result["ok"] = camera->startCapture(locator, c["width"].toInt(), c["height"].toInt(), c["fps"].toDouble());
                state["settings"] = settingsJson(camera->settings()); state["error"] = QString::fromStdString(camera->lastError()); state["active"] = camera->isCapturing();
            } else if (op == "control" && camera) {
                auto future = camera->setControl(c["id"].toString().toStdString(), c["value"].toInt(), c["automatic"].toBool());
                if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
                    const auto r = future.get(); result["ok"] = r.success; result["control"] = controlJson(r.control); result["error"] = QString::fromStdString(r.error);
                    state["settings"] = settingsJson(camera->settings());
                }
            } else if (op == "start" && kind == "window") {
                window = std::make_unique<video::WindowCapture>(); window->setFrameCallback(frame);
                result["ok"] = window->startCapture(locator, c["width"].toInt(), c["height"].toInt(), c["fps"].toInt(), c["alpha"].toBool()); state["active"] = result["ok"];
            } else if (op == "start" && kind == "spout") {
                spout = std::make_unique<video::SpoutCapture>(); spout->setFrameCallback(frame);
                result["ok"] = spout->startCapture(locator, c["width"].toInt(), c["height"].toInt(), c["fps"].toInt()); state["active"] = result["ok"];
            } else if (op == "start" && kind == "audio") {
                captureAudio = std::make_unique<audio::WindowAudioCaptureCore>();
                const auto r = c.contains("pid") ? captureAudio->StartStreamCapture(uint32_t(c["pid"].toInteger()), audio, c["exclude"].toBool()) : captureAudio->StartInputDeviceStreamCapture(locator, audio);
                result["ok"] = r.success; result["error"] = QString::fromStdString(r.error); state["active"] = r.success;
            } else if (op == "start" && kind == "media") {
                media = std::make_unique<MediaSource>(); media->setLoop(c["loop"].toBool()); media->setPaused(c["paused"].toBool());
                media->start(c["locator"].toString(), [&](const video::CapturedFrame &f) {
                    frameBytes(f, f.data.data(), f.data.size());
                }, audio); result["ok"] = true; state["active"] = true;
            } else if (op == "media" && media) {
                if (c.contains("paused")) media->setPaused(c["paused"].toBool());
                if (c.contains("loop")) media->setLoop(c["loop"].toBool());
                if (c["restart"].toBool()) media->restart(); result["ok"] = true;
            } else if (op == "start" && kind == "browser") {
                browser = std::make_unique<BrowserSource>(c["id"].toString(), QUrl(c["locator"].toString()), c["render"].toBool(), c["transparent"].toBool(), c["talkback"].toBool(), c["preserveColorMetadata"].toBool());
                QObject::connect(browser.get(), &BrowserSource::frameReady, &application, [&](const QImage &image) {
                    app::detail::PerformanceTrace::Scope profile(app::detail::PerformanceTrace::BrowserDelivery);
                    // A static PNG overlay keeps its identity through IPC. Do
                    // not recopy/unpremultiply it or invalidate compositor caches
                    // simply because WebView2 completed another snapshot.
                    if (image.cacheKey() == browserImageKey && lock(handles[3])) {
                        ++shared->frameSerial; ReleaseMutex(handles[3]); SetEvent(handles[4]); return;
                    }
                    const auto pixels = image.hasAlphaChannel() ? image.convertToFormat(QImage::Format_ARGB32_Premultiplied) : image;
                    video::CapturedFrame f; f.width = pixels.width(); f.height = pixels.height(); f.stride = pixels.bytesPerLine(); f.opaque = !pixels.hasAlphaChannel();
                    f.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;
                    // Preserve premultiplied alpha at the browser boundary.
                    if (frameBytes(f, pixels.constBits(), size_t(pixels.sizeInBytes()))) browserImageKey = image.cacheKey();
                });
                QObject::connect(browser.get(), &BrowserSource::audioReady, &application, [&](const QByteArray &pcm, int rate) {
                    audio::StreamChunk chunk; chunk.sampleRate = rate; chunk.samples.resize(pcm.size()/2);
                    for (qsizetype i=0; i<pcm.size()/2; ++i) chunk.samples[i] = qFromLittleEndian<qint16>(pcm.constData()+i*2)/32768.f;
                    audio(std::move(chunk));
                });
                QObject::connect(browser.get(), &BrowserSource::errorOccurred, &application, [&](const QString &error) { state["error"] = error; publishState(); });
                QObject::connect(browser.get(), &BrowserSource::recovered, &application, [&] { state["error"] = ""; publishState(); });
                QObject::connect(browser.get(), &BrowserSource::restartRequired, &application, [&] { application.exit(3); });
                state["active"] = true; result["ok"] = true;
            } else if (op == "interact" && browser) {
                if (!c["title"].toString().isEmpty()) browser->setWindowTitle(c["title"].toString());
                browser->interact(); result["ok"] = true;
            }
            else if (op == "park" && browser) { browser->park(); result["ok"] = true; }
            else if (kind == "output") {
                if (op == "start") {
                    output::Config config; output::MediaConfig media;
                    config.protocol = output::Protocol(c["protocol"].toInt()); config.url = c["url"].toString().toStdString();
                    config.bearerToken = c["bearer"].toString().toStdString(); config.streamKey = c["key"].toString().toStdString();
                    config.streamId = c["streamId"].toString().toStdString(); config.passphrase = c["passphrase"].toString().toStdString();
                    config.latencyMs = c["latency"].toInt(); config.aacBitrateKbps = c["aac"].toInt();
                    media.width = c["width"].toInt(); media.height = c["height"].toInt(); media.fps = c["fps"].toInt();
                    media.audioChannels = c["channels"].toInt(); media.audioEnabled = c["audio"].toBool();
                    media.iceMode = webrtc::IceMode(c["ice"].toInt()); media.ffmpegPath = c["ffmpeg"].toString().toStdString();
                    output = std::make_unique<output::Session>(config, media, [&] { ++keyframes; }); result["ok"] = output->start();
                    // Worker startup uses anonymous memory rather than placing
                    // settings or credentials on the worker's command line.
                    SecureZeroMemory(shared->command, shared->commandSize);
                } else if (op == "packets" && output && shared->inputSize <= shared->blobCapacity) {
                    const auto packets = c["packets"].toArray(); result["ok"] = packets.size() <= 64;
                    if (packets.size() <= 64) for (const auto &value : packets) {
                        const auto p = value.toObject(); const auto pts = p["pts"].toInteger();
                        const auto offset = p["offset"].toInteger(-1), size = p["size"].toInteger(-1);
                        if (offset < 0 || size < 0 || offset+size > shared->inputSize) { result["ok"] = false; break; }
                        const auto *data = blobData(shared)+offset;
                        if (p["kind"].toInt() == 0) {
                            webrtc::EncodedVideoPacket packet; packet.data.assign(data, data+size);
                            packet.pts = pts; packet.isKeyframe = p["keyframe"].toBool();
                            if (!output->sendVideo(packet, p["width"].toInt(), p["height"].toInt())) result["ok"] = false;
                        } else if (p["kind"].toInt() == 1) {
                            webrtc::EncodedAudioPacket packet; packet.data.assign(data, data+size); packet.pts = pts;
                            if (!output->sendOpus(packet)) result["ok"] = false;
                        } else if (size % sizeof(float) == 0 && size <= AudioCapacity*sizeof(float)) {
                            std::vector<float> samples(size/sizeof(float)); memcpy(samples.data(), data, size);
                            if (!output->sendPcm(samples, pts)) result["ok"] = false;
                        }
                    }
                }
            }
            // Encoder dispatch is below, sharing the same bounded request slot.
            else if (kind == "encoder") {
                if (op == "start") {
                    video::EncoderConfig config;
                    config.codec = video::VideoCodec(c["codec"].toInt()); config.preferredHardware = video::HardwareEncoder(c["hardware"].toInt());
                    config.explicitEncoderSelection = c["explicit"].toBool(); config.forceFfmpegNvenc = c["forceFfmpeg"].toBool();
                    config.ffmpegPath = c["ffmpeg"].toString().toStdString(); config.ffmpegOptions = c["options"].toString().toStdString();
                    config.requireEveryFrameKeyframe = c["everyKeyframe"].toBool(); config.enableAlpha = c["alpha"].toBool();
                    config.alphaBackgroundMode = video::AlphaBackgroundMode(c["background"].toInt());
                    config.alphaBackgroundRed = c["red"].toInt(); config.alphaBackgroundGreen = c["green"].toInt(); config.alphaBackgroundBlue = c["blue"].toInt();
                    config.width = c["width"].toInt(); config.height = c["height"].toInt(); config.frameRate = c["fps"].toInt();
                    config.bitrate = c["bitrate"].toInt(); config.minBitrate = c["minBitrate"].toInt(); config.maxBitrate = c["maxBitrate"].toInt();
                    config.gopSize = c["gop"].toInt(); config.bFrames = c["bFrames"].toInt(); config.lowLatency = c["lowLatency"].toBool();
                    video::CapturedFrame handover;
                    if (shared->inputSize && shared->inputSize <= shared->blobCapacity) {
                        handover.width = c["frameWidth"].toInt(); handover.height = c["frameHeight"].toInt(); handover.stride = c["stride"].toInt();
                        handover.format = video::CapturedFrame::Format(c["format"].toInt()); handover.opaque = c["opaque"].toBool(); handover.timestamp = c["timestamp"].toInteger();
                        handover.data.assign(blobData(shared), blobData(shared)+shared->inputSize);
                    }
                    encoder = std::make_unique<video::VideoEncoder>();
                    result["ok"] = encoder->initialize(config, handover.data.empty() ? nullptr : &handover);
                } else if (op == "encode" && encoder && shared->inputSize <= shared->blobCapacity) {
                    if (c["keyframe"].toBool()) encoder->requestKeyframe();
                    if (c["bitrate"].toInt() > 0) encoder->setBitrate(c["bitrate"].toInt());
                    auto &f = encoderFrame; f.width = c["width"].toInt(); f.height = c["height"].toInt(); f.stride = c["stride"].toInt();
                    f.format = video::CapturedFrame::Format(c["format"].toInt()); f.opaque = c["opaque"].toBool(); f.timestamp = c["timestamp"].toInteger();
                    f.data.assign(blobData(shared), blobData(shared)+shared->inputSize);
                    video::EncodedPacket packet;
                    if (encoder->encodeWithSourceTimestamp(f, packet, c["sourceTimestamp"].toInteger()) && packet.data.size() <= shared->blobCapacity) {
                        shared->outputSize = uint32_t(packet.data.size()); memcpy(blobData(shared), packet.data.data(), packet.data.size());
                        result = {{"ok", true}, {"pts", qint64(packet.pts)}, {"dts", qint64(packet.dts)}, {"sourceTimestamp", qint64(packet.sourceTimestamp)}, {"keyframe", packet.isKeyframe}, {"codec", int(packet.codec)}};
                    }
                } else if (op == "keyframe" && encoder) { encoder->requestKeyframe(); result["ok"] = true; }
                else if (op == "bitrate" && encoder) { encoder->setBitrate(c["value"].toInt()); result["ok"] = true; }
                if (encoder) {
                    result["encoder"] = QString::fromStdString(encoder->activeEncoderName()); result["codecName"] = QString::fromStdString(encoder->activeCodecName());
                    result["activeCodec"] = int(encoder->activeCodec()); result["category"] = QString::fromStdString(encoder->activeEncoderCategory());
                    result["inputFormat"] = QString::fromStdString(encoder->activeInputFormatName()); result["fallback"] = QString::fromStdString(encoder->encoderFallbackReason());
                    result["hardware"] = encoder->isHardwareEncoderActive(); result["failure"] = int(encoder->lastEncodeFailureKind()); result["everyKeyframe"] = encoder->guaranteesEveryFrameKeyframe();
                }
            }
        } catch (...) { result = {{"ok", false}, {"error", "Worker operation failed."}}; }
        // A frame callback can hold the snapshot mutex as startup completes.
        // Return the initial state in the synchronized reply so the parent
        // never installs an empty camera-settings snapshot after a mode change.
        if (op == "start") result["status"] = state;
        publishState(); json(shared->reply, shared->replySize, result); SetEvent(handles[2]);
    });
    QTimer health;
    QObject::connect(&health, &QTimer::timeout, &application, [&] {
        app::detail::PerformanceTrace::instance().flush();
        state["heartbeat"] = state["heartbeat"].toInteger()+1;
        if (camera) { state["active"] = camera->isCapturing(); state["error"] = QString::fromStdString(camera->lastError()); }
        if (window) state["active"] = window->isCapturing();
        if (spout) state["active"] = spout->isCapturing();
        if (captureAudio) state["active"] = captureAudio->IsCapturing();
        if (media) { state["error"] = media->error(); state["ended"] = media->ended(); state["hasVideo"] = media->hasVideo(); }
        if (browser) {
            if (browserCpuAt.isValid() && browserCpuAt.elapsed() >= 1000) {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION usage{};
                if (QueryInformationJobObject(nullptr, JobObjectBasicAccountingInformation, &usage, sizeof(usage), nullptr)) {
                    const auto ticks = usage.TotalKernelTime.QuadPart + usage.TotalUserTime.QuadPart;
                    const double cores = (ticks-browserCpuTicks) / (browserCpuAt.elapsed()*10000.0);
                    browserCpuBusySamples = cores > browserCpuCores*.8 ? std::min(3, browserCpuBusySamples+1) : 0;
                    state["cpuLimitActive"] = browserCpuBusySamples >= 3;
                    browserCpuTicks = ticks;
                }
                browserCpuAt.restart();
            }
            state["description"] = browser->captureDescription() + (state["cpuLimitActive"].toBool() ? " / CPU limit active" : "");
        }
        if (output) {
            const auto s = output->status();
            state["state"] = int(s.state); state["message"] = QString::fromStdString(s.message);
            state["reconnects"] = qint64(s.reconnects); state["videoPackets"] = qint64(s.videoPackets); state["audioPackets"] = qint64(s.audioPackets);
            state["videoBytes"] = qint64(s.videoBytes); state["audioBytes"] = qint64(s.audioBytes); state["queuedBytes"] = qint64(s.queuedBytes);
            state["processId"] = qint64(s.processId); state["rrAge"] = qint64(s.receiverReportAgeMs); state["rrTimeout"] = qint64(s.receiverReportTimeoutMs);
            state["rrCount"] = qint64(s.receiverReports); state["icePath"] = QString::fromStdString(s.icePath); state["keyframes"] = keyframes.load();
        }
        publishState();
    });
    health.start(250); SetEvent(handles[2]);
    const int result = application.exec();
    monitor.reset(); output.reset(); browser.reset(); media.reset(); captureAudio.reset(); spout.reset(); window.reset(); camera.reset(); encoder.reset();
    UnmapViewOfFile(shared); for (auto &handle : handles) close(handle);
    if (SUCCEEDED(apartment)) CoUninitialize();
    if (preciseClock) timeEndPeriod(1);
    return result;
}
} // namespace versus::mixer
