#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace versus::app::detail {
// Opt-in aggregate diagnostics; no frames, audio, URLs or source names.
// Wall time includes waits. Thread cycles help distinguish work from waiting.
class PerformanceTrace {
public:
    enum Stage { Compose, CanvasPaint, MixerSubmit, EncoderPrepare, EncoderSubmit,
                 BrowserDecode, BrowserMute, Count };
    static PerformanceTrace &instance() { static PerformanceTrace trace; return trace; }
    bool enabled() const { return enabled_; }
    class Scope {
    public:
        explicit Scope(Stage stage) : stage_(stage), active_(instance().enabled()) {
            if (active_) { start_ = Clock::now(); cycles_ = threadCycles(); }
        }
        ~Scope() {
            if (!active_) return;
            const auto cycles = threadCycles() - cycles_;
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count();
            auto &entry = instance().entries_[stage_];
            entry.ns.fetch_add(ns, std::memory_order_relaxed);
            entry.cycles.fetch_add(cycles, std::memory_order_relaxed);
            entry.calls.fetch_add(1, std::memory_order_relaxed);
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;
    private:
        using Clock = std::chrono::steady_clock;
        static unsigned long long threadCycles() {
#ifdef _WIN32
            ULONG64 value = 0;
            QueryThreadCycleTime(GetCurrentThread(), &value);
            return value;
#else
            return 0;
#endif
        }
        Stage stage_;
        bool active_;
        Clock::time_point start_;
        unsigned long long cycles_ = 0;
    };
    // Called only by the UI timer. Cumulative counters avoid cross-thread resets.
    void flush() {
        if (!enabled_) return;
        const auto now = std::chrono::steady_clock::now();
        if (now-lastFlush_ < std::chrono::seconds(5)) return;
        lastFlush_ = now;
        const double seconds = std::chrono::duration<double>(now-start_).count();
        constexpr const char *names[] = {"compose", "canvas_paint", "mixer_submit", "encoder_prepare",
                                        "encoder_submit", "browser_png_decode", "browser_mute"};
        for (int i = 0; i < Count; ++i) {
            auto &entry = entries_[i];
            file_ << seconds << ',' << names[i] << ',' << entry.calls.load(std::memory_order_relaxed)
                  << ',' << entry.ns.load(std::memory_order_relaxed) << ',' << entry.cycles.load(std::memory_order_relaxed) << '\n';
        }
        file_.flush();
    }
private:
    PerformanceTrace() {
        const char *path = std::getenv("VIDEO_MIXER_PROFILE");
        if (path && *path) {
            file_.open(path, std::ios::out | std::ios::trunc);
            enabled_ = file_.is_open();
            if (enabled_) file_ << "seconds,stage,calls,wall_ns,thread_cycles\n";
        }
    }
    struct Entry { std::atomic<unsigned long long> calls{0}, ns{0}, cycles{0}; };
    std::array<Entry, Count> entries_;
    bool enabled_ = false;
    std::ofstream file_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now(), lastFlush_ = start_;
};
}
