// Standalone deterministic gate, not a substitute for recording in the app.
#include "versus/app/encoder_recovery_policy.h"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace std::chrono;
    using namespace versus::app;
    const auto start = steady_clock::time_point(hours(1));
    auto require = [](bool ok) { if (!ok) throw std::runtime_error("Render clock gate failed"); };
    for (const int fps : {24, 25, 30, 50, 60, 120}) {
        const auto interval = outputFrameInterval(fps);
        auto render = nextRenderFrameDeadline({}, start, interval);
        auto recorder = start + interval;
        int64_t lastTimestamp = outputFrameTimestamp100ns(start);
        const int64_t frames = int64_t(fps)*8*3600;
        for (int64_t frame = 1; frame <= frames; ++frame) {
            // Qt's millisecond rounding and bounded scheduler jitter must not
            // feed back into the accumulated render clock.
            const auto wake = steady_clock::time_point(ceil<milliseconds>(render.time_since_epoch()))
                + microseconds((frame%5)*200);
            require(render == recorder);
            const auto stamp = outputFrameTimestamp100ns(render);
            require(stamp > lastTimestamp);
            lastTimestamp = stamp;
            render = nextRenderFrameDeadline(render, wake, interval);
            recorder += interval;
        }
        require(render == start + interval*(frames+1));
        const auto stalled = render + seconds(2);
        require(nextRenderFrameDeadline(render, stalled, interval) == stalled + interval);
        require(nextRenderFrameDeadline({}, stalled, interval) == stalled + interval);
        const auto oldPeriod = nanoseconds((10000000LL/fps)*100);
        const auto oldDrift = (interval-oldPeriod)*frames;
        if (fps == 30 || fps == 60) require(oldDrift > interval/2);
        std::cout << fps << " fps: eight-hour phase error 0 ns; old error "
                  << duration_cast<microseconds>(oldDrift).count() << " us; stall/reset passed\n";
    }
}
