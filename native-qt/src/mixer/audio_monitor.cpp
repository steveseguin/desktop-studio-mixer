#include "versus/app/performance_trace.h"
#include "versus/mixer/audio_monitor.h"
#include "versus/mixer/process_worker.h"
#include <algorithm>
#include <array>
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QJsonArray>
#include <map>
#include <condition_variable>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <set>
using Microsoft::WRL::ComPtr;
#endif

namespace versus::mixer {
struct AudioMonitor::Impl {
#ifdef _WIN32
    HWAVEOUT output = nullptr;
    struct Buffer { WAVEHDR header{}; std::array<short, 960> samples{}; };
    std::array<Buffer, 8> buffers;
    size_t next = 0;
    ULONGLONG lastWrite = 0;
#endif
};
AudioMonitor::AudioMonitor() : impl_(std::make_unique<Impl>()) {}
AudioMonitor::~AudioMonitor() { close(); }
std::vector<MonitorDevice> AudioMonitor::devices() {
    std::vector<MonitorDevice> result{{-1, "Default Windows playback device", "default"}};
    if (!ProcessWorker::inWorker()) {
        ProcessWorker worker;
        if (!worker.start("devices")) return result;
        const auto reply = worker.call({{"op", "outputs"}});
        if (!reply["ok"].toBool()) return result;
        result.clear();
        for (const auto &value : reply["devices"].toArray()) {
            const auto d = value.toObject(); result.push_back({d["index"].toInt(), d["name"].toString(), d["id"].toString()});
        }
        return result;
    }
#ifdef _WIN32
    ComPtr<IMMDeviceEnumerator> enumerator;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    for (UINT i = 0; i < waveOutGetNumDevs(); ++i) {
        WAVEOUTCAPSW info{};
        if (waveOutGetDevCapsW(i, &info, sizeof(info)) != MMSYSERR_NOERROR) continue;
        // Endpoint IDs survive device enumeration order changes. See Microsoft's
        // "Device Roles for Legacy Windows Multimedia Applications" example.
        ULONG bytes = 0;
        const auto handle = reinterpret_cast<HWAVEOUT>(static_cast<UINT_PTR>(i));
        if (waveOutMessage(handle, DRV_QUERYFUNCTIONINSTANCEIDSIZE, reinterpret_cast<DWORD_PTR>(&bytes), 0) != MMSYSERR_NOERROR || bytes < sizeof(wchar_t) || bytes > 65536) continue;
        std::vector<wchar_t> key((bytes+sizeof(wchar_t)-1)/sizeof(wchar_t)+1, 0);
        if (waveOutMessage(handle, DRV_QUERYFUNCTIONINSTANCEID, reinterpret_cast<DWORD_PTR>(key.data()), bytes) != MMSYSERR_NOERROR) continue;
        QString name = QString::fromWCharArray(info.szPname);
        ComPtr<IMMDevice> device; ComPtr<IPropertyStore> properties;
        if (enumerator && SUCCEEDED(enumerator->GetDevice(key.data(), &device)) && SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT value{};
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal) name = QString::fromWCharArray(value.pwszVal);
            PropVariantClear(&value);
        }
        result.push_back({static_cast<int>(i), name, QString::fromWCharArray(key.data())});
    }
#endif
    return result;
}
QString AudioMonitor::open(int device) {
    close();
#ifdef _WIN32
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    auto result = waveOutOpen(&impl_->output, device < 0 ? WAVE_MAPPER : static_cast<UINT>(device), &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) { impl_->output = nullptr; return "Cannot open this monitoring device. Select another output."; }
    for (auto &buffer : impl_->buffers) {
        buffer.header = {}; buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
        buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size()*sizeof(short));
        if (waveOutPrepareHeader(impl_->output, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            close(); return "Cannot prepare audio monitoring.";
        }
    }
    impl_->next = 0; impl_->lastWrite = GetTickCount64(); return {};
#else
    return "Audio monitoring requires Windows.";
#endif
}
void AudioMonitor::close() {
#ifdef _WIN32
    if (!impl_->output) return;
    waveOutReset(impl_->output);
    for (auto &buffer : impl_->buffers) {
        if (buffer.header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(impl_->output, &buffer.header, sizeof(WAVEHDR));
        buffer.header = {};
    }
    waveOutClose(impl_->output); impl_->output = nullptr;
#endif
}
bool AudioMonitor::write(const std::vector<float> &samples) {
#ifdef _WIN32
    if (!impl_->output || samples.size() != 960) return false;
    auto &buffer = impl_->buffers[impl_->next];
    if ((buffer.header.dwFlags & WHDR_INQUEUE) != 0) return GetTickCount64()-impl_->lastWrite < 1000;
    for (size_t i = 0; i < samples.size(); ++i) buffer.samples[i] = static_cast<short>(std::clamp(samples[i], -1.0f, 1.0f)*32767);
    if (waveOutWrite(impl_->output, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
    impl_->next = (impl_->next+1)%impl_->buffers.size();
    impl_->lastWrite = GetTickCount64();
    return true;
#else
    return false;
#endif
}

struct IsolatedAudioMonitor::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool running = true, enabled = false;
    QString device = "default";
    unsigned revision = 0;
    QString error;
    std::array<std::array<float,960>,4> queue{};
    size_t read = 0, count = 0;
    std::thread thread;
    Impl() : thread([this] { run(); }) {}
    void run() {
        std::unique_ptr<ProcessWorker> worker;
        unsigned opened = 0;
        std::array<float,960> samples;
        while (true) {
            std::unique_lock lock(mutex);
            wake.wait(lock, [this, opened] { return !running || revision != opened || count; });
            if (!running) break;
            const auto generation = revision;
            if (generation != opened) {
                const bool active = enabled; const auto target = device;
                opened = generation; count = 0; lock.unlock(); worker.reset();
                QString failure;
                if (active) {
                    worker = std::make_unique<ProcessWorker>();
                    if (!worker->start("monitor")) failure = worker->error();
                    else {
                        const auto reply = worker->call({{"op", "start"}, {"device", target}}, nullptr, 0, nullptr, 2000);
                        if (!reply["ok"].toBool()) failure = reply["error"].toString("Monitoring device did not respond.");
                    }
                }
                if (!failure.isEmpty()) {
                    worker.reset(); lock.lock();
                    if (revision == generation) { error = failure; count = 0; }
                }
                continue;
            }
            samples = queue[read]; read = (read+1)%queue.size(); --count; lock.unlock();
            if (worker) {
                const auto reply = worker->call({{"op", "audio"}}, samples.data(), sizeof(samples), nullptr, 250);
                if (!reply["ok"].toBool()) {
                    worker->terminate(); worker.reset(); lock.lock();
                    if (revision == generation) { error = "Audio monitoring stopped. Select an output and enable monitoring to retry."; count = 0; }
                }
            }
        }
    }
};
IsolatedAudioMonitor::IsolatedAudioMonitor() : impl_(std::make_unique<Impl>()) {}
IsolatedAudioMonitor::~IsolatedAudioMonitor() {
    { std::lock_guard lock(impl_->mutex); impl_->running = false; }
    impl_->wake.notify_one(); impl_->thread.join();
}
void IsolatedAudioMonitor::open(const QString &device) {
    { std::lock_guard lock(impl_->mutex); impl_->device = device; impl_->enabled = true; impl_->error.clear(); ++impl_->revision; }
    impl_->wake.notify_one();
}
void IsolatedAudioMonitor::close() {
    { std::lock_guard lock(impl_->mutex); impl_->enabled = false; impl_->count = 0; ++impl_->revision; }
    impl_->wake.notify_one();
}
void IsolatedAudioMonitor::write(const std::vector<float> &samples) {
    if (samples.size() != 960) return;
    { std::lock_guard lock(impl_->mutex);
      if (!impl_->enabled || !impl_->error.isEmpty()) return;
      if (impl_->count == impl_->queue.size()) { impl_->read = (impl_->read+1)%impl_->queue.size(); --impl_->count; }
      std::copy(samples.begin(), samples.end(), impl_->queue[(impl_->read+impl_->count)%impl_->queue.size()].begin()); ++impl_->count; }
    impl_->wake.notify_one();
}
QString IsolatedAudioMonitor::error() const { std::lock_guard lock(impl_->mutex); return impl_->error; }
#ifdef _WIN32
namespace {
void silenceBrowserPlayback(std::set<DWORD> owned) {
    app::detail::PerformanceTrace::Scope profile(app::detail::PerformanceTrace::BrowserMute);
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    std::vector<std::pair<DWORD, DWORD>> processes;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) do { processes.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID); } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    bool added = true;
    while (added) { added = false; for (const auto &[pid, parent] : processes) if (owned.contains(parent)) added |= owned.insert(pid).second; }
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return;
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) return;
    UINT count = 0; devices->GetCount(&count);
    for (UINT d = 0; d < count; ++d) {
        ComPtr<IMMDevice> device; devices->Item(d, &device); if (!device) continue;
        ComPtr<IAudioSessionManager2> manager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.GetAddressOf())))) continue;
        ComPtr<IAudioSessionEnumerator> sessions; if (FAILED(manager->GetSessionEnumerator(&sessions))) continue;
        int size = 0; sessions->GetCount(&size);
        for (int s = 0; s < size; ++s) {
            ComPtr<IAudioSessionControl> control; sessions->GetSession(s, &control); if (!control) continue;
            ComPtr<IAudioSessionControl2> control2; if (FAILED(control.As(&control2))) continue;
            DWORD pid = 0; control2->GetProcessId(&pid); if (!owned.contains(pid)) continue;
            ComPtr<ISimpleAudioVolume> volume;
            if (SUCCEEDED(control.As(&volume))) {
                BOOL muted = FALSE;
                if (FAILED(volume->GetMute(&muted)) || !muted) volume->SetMute(TRUE, nullptr);
            }
        }
    }
}

class BrowserPlaybackSilencer : public QObject {
public:
    explicit BrowserPlaybackSilencer(QObject *parent) : QObject(parent) {
        QObject::connect(&timer, &QTimer::timeout, this, [this] {
            std::set<DWORD> owned;
            for (const auto &[pid, references] : roots) owned.insert(pid);
            if (!owned.empty()) silenceBrowserPlayback(std::move(owned));
        });
    }
    void add(DWORD pid) {
        ++roots[pid];
        if (!timer.isActive()) timer.start(1000);
    }
    void remove(DWORD pid) {
        const auto it = roots.find(pid);
        if (it != roots.end() && --it->second == 0) roots.erase(it);
        if (roots.empty()) timer.stop();
    }
private:
    QTimer timer;
    std::map<DWORD, unsigned> roots;
};

BrowserPlaybackSilencer *browserSilencer(bool create) {
    static QPointer<BrowserPlaybackSilencer> silencer;
    if (!silencer && create && QCoreApplication::instance())
        silencer = new BrowserPlaybackSilencer(QCoreApplication::instance());
    return silencer;
}
} // namespace
#endif

void registerBrowserPlayback(unsigned long browserPid) {
#ifdef _WIN32
    if (browserPid) if (auto *silencer = browserSilencer(true)) silencer->add(browserPid);
#else
    (void)browserPid;
#endif
}
void unregisterBrowserPlayback(unsigned long browserPid) {
#ifdef _WIN32
    if (browserPid) if (auto *silencer = browserSilencer(false)) silencer->remove(browserPid);
#else
    (void)browserPid;
#endif
}
}
