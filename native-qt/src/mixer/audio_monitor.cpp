#include "versus/app/performance_trace.h"
#include "versus/mixer/audio_monitor.h"
#include <algorithm>
#include <array>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
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
#endif
};
AudioMonitor::AudioMonitor() : impl_(std::make_unique<Impl>()) {}
AudioMonitor::~AudioMonitor() { close(); }
std::vector<std::pair<int, QString>> AudioMonitor::devices() {
    std::vector<std::pair<int, QString>> result{{-1, "Default Windows playback device"}};
#ifdef _WIN32
    for (UINT i = 0; i < waveOutGetNumDevs(); ++i) {
        WAVEOUTCAPSW info{};
        if (waveOutGetDevCapsW(i, &info, sizeof(info)) == MMSYSERR_NOERROR)
            result.emplace_back(static_cast<int>(i), QString::fromWCharArray(info.szPname));
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
    impl_->next = 0; return {};
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
void AudioMonitor::write(const std::vector<float> &samples) {
#ifdef _WIN32
    if (!impl_->output || samples.size() != 960) return;
    auto &buffer = impl_->buffers[impl_->next];
    if ((buffer.header.dwFlags & WHDR_INQUEUE) != 0) return; // bounded latency, never block the program bus
    for (size_t i = 0; i < samples.size(); ++i) buffer.samples[i] = static_cast<short>(std::clamp(samples[i], -1.0f, 1.0f)*32767);
    if (waveOutWrite(impl_->output, &buffer.header, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
        impl_->next = (impl_->next+1)%impl_->buffers.size();
#endif
}
void silenceBrowserPlayback(unsigned long browserPid) {
    app::detail::PerformanceTrace::Scope profile(app::detail::PerformanceTrace::BrowserMute);
#ifdef _WIN32
    if (!browserPid) return;
    std::set<DWORD> owned{browserPid};
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
            if (SUCCEEDED(control.As(&volume))) volume->SetMute(TRUE, nullptr);
        }
    }
#else
    (void)browserPid;
#endif
}
}
