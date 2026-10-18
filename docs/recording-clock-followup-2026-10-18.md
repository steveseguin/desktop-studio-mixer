# Recording clock correction — October 18, 2026

The compositor rounded each frame interval to 100 ns, while the recorder and publishing pacers used nanoseconds. At 30 fps that creates 28.512 ms of relative phase error over eight hours; at 60 fps it creates 114.048 ms. Render deadlines now use the same `outputFrameInterval()` as the output pacers. Rounding occurs only at the Qt timer and media timestamp boundaries. Long scheduling stalls still reset the render deadline without a catch-up burst.

This removes a demonstrated clock mismatch. It is a plausible contributor to the recording repeats observed after four hours in the previous soak, **not proof that the entire long-run problem is resolved**.

The standalone `render_clock_gate` advances eight simulated hours at 24, 25, 30, 50, 60 and 120 fps, including millisecond timer rounding, bounded wake-up jitter and a stall/reset. It passed with zero relative phase accumulation. This is a deterministic gate, not an eight-hour application test. It is registered with CTest when `VERSUS_BUILD_TESTS=ON`; it can also be compiled directly with C++20 and the `native-qt/include` directory.

Actual application testing used separate, unsigned candidate packages based on the unchanged signed 0.9.3 portable package. Each case ran for 60 seconds with captured audio, a transparent browser overlay, 3 Mbps private UDP streaming and independent recording:

| Final candidate workflow | Received / recorded fps | App CPU cores | Recording drops, start → end | Repeats |
|---|---:|---:|---:|---:|
| Browser video, GPU composition, 720p30 | 30.000 / 30.000 | 0.761 | 11 → 11 | 0 |
| Browser video, CPU composition, 720p30 | 30.000 / 30.000 | 0.802 | 13 → 13 | 0 |
| File video, GPU composition, 720p60 | 59.994 / 60.000 | 1.047 | 22 → 23 | 1 |

Both files from every case passed full decode and monotonic-timestamp checks. Audio and flash/beep sync were confirmed in six sampled windows per case. The reference is 30 fps, including the 60 fps output case; this does not qualify native 60 fps motion. Startup replacements contribute to the drop counter.

A separate browser-frame polling experiment failed to improve distinct-picture cadence and was removed. Browser + CPU composition still delivered approximately 25 distinct pictures/s despite 30 fps encoded timestamps. The earlier WebView resource growth remains unresolved. One initial API screenshot omitted the overlay label, although a later recorded frame and all 129 half-second recording samples contained its background; this discrepancy also remains open.

No new signed release or multi-hour soak was made for this correction. No GitHub Actions ran. [Measurements, limitations and verified raw evidence](https://github.com/steveseguin/video-encoder-testing/tree/main/knowledge/2026-10-18-studio-clock-fix) are in the private research repository.
