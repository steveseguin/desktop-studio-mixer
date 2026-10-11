# Video Mixer 0.4.0: single canvas and CPU review

Validated on Windows x64, 2026-10-11, with the packaged Release application, a Logitech HD Pro Webcam C920 and its microphone, and private loopback browser fixtures. The original Game Capture repository was not changed.

## User-facing changes

- Single canvas is the default. Selecting scenes, editing geometry/order/visibility, and adding/removing sources changes output immediately. Audio with **Scene** enabled follows those changes.
- The audio mixer sits beside the canvas, keeping track/master VU meters visible and giving the picture more height. Scroll areas retain their contrasting backgrounds and borders.
- **Studio mode** restores separate Preview/Program canvases and Cut/Fade. Turning it off takes the selected Preview scene to output. The tooltip explains that behavior; the selected mode is saved across launches.
- Selection outlines belong to the editor, not the clean Program window or encoded output.

## Performance changes and attribution

The previous roughly one-core measurement included native capture, browser processes, audio, encoding and recording. It was not a measurement of the compositor alone.

- BGRA-to-NV12 conversion now uses SSE2 on x64, preserving the previous BT.601 limited-range coefficients and rounded 2x2 chroma averaging. Native-size frames skip an intermediate BGRA copy. Other architectures retain scalar conversion.
- Transparent browser snapshots reuse decoded pixels when the PNG bytes are unchanged. WebView2 is still sampled at its normal cadence; changed overlays decode immediately. This does not eliminate WebView2's own snapshot/PNG work.
- Single canvas composes and paints one view. Studio mode retains the existing shared composition caches.

Opt-in aggregate instrumentation measured the following in seven approximately five-second recording intervals. These are **scoped wall times**, including waits, not CPU percentages. Encoder submission includes input preparation, so those rows must not be summed. Thread-cycle counters were also collected; work on other threads/browser processes is outside these scopes.

| Stage | Before optimization, Studio | 0.4.0 single canvas |
| --- | ---: | ---: |
| Composition per output frame, both views combined | 1.616 ms | 1.340 ms |
| Canvas painting per output frame | 0.126 ms | 0.124 ms |
| Mixer frame copy/submission | 1.002 ms | 0.992 ms |
| Encoder input preparation | 1.665 ms | 0.610 ms |
| Encoder submission including preparation | 3.944 ms | 2.765 ms |
| Transparent PNG decoding | 449 decodes, 2.898 ms each | 0 decodes during these intervals |

The optimized static overlay decoded once during startup, then reused that image. Input preparation was about **63% faster**. A dynamic transparent-overlay workflow separately verified that changed content still appeared within one second and retained transparency.

### Whole-process measurements

Five-second warmup, then about 30 seconds per scenario. **100% means one logical core**. This machine has 20 logical cores; divide by 20 to compare with machine-wide percentages. Tree totals include WebView2 and FFmpeg children.

| Scenario | 0.3.2 app | 0.4.0 app | 0.3.2 tree | 0.4.0 tree |
| --- | ---: | ---: | ---: | ---: |
| Static title | 0.62% | 0.62% | 0.62% | 0.62% |
| C920 camera | 12.85% | 13.14% | 12.85% | 13.14% |
| Camera + animated browser | 25.47% | 24.02% | 36.83% | 36.52% |
| Camera + browser + transparent overlay | 31.28% | 24.72% | 69.30% | 56.25% |
| Mixed recording + C920 microphone | 72.51% | 59.07% | 114.29% | 93.42% |
| Break, sources still active off air | 21.66% | 20.17% | 60.64% | 48.24% |

The final recording run therefore used about 2.95% of the whole machine in the app, or 4.67% including children. These are single-run observations, not guaranteed savings. Browser-process load varied: an intermediate instrumented pre-optimization run measured 65.89% app / 91.06% tree during recording. The per-stage conversion/cache results provide stronger attribution than changes in the whole process tree. The final run used aggregate instrumentation; its overhead is included.

The recording used a Media Foundation H.264 encoder enumerated as hardware (`H264 Encoder MFT`), with CPU NV12 input. Earlier Intel/NVIDIA candidates failed the existing startup probes. There is still CPU capture/readback, image copying and browser snapshot overhead; this is not a fully GPU-resident pipeline. Camera mode changes still restart capture synchronously. The earlier WPR stack-sampling attempt failed under system policy, so this review uses in-app scopes and process/thread CPU sampling rather than an ETW stack profile.

## Packaged application workflows

The workflow reports and camera recordings are local, ignored artifacts under `native-qt/e2e/reports/mixer-direct/`.

- Single-canvas editing: direct scene selection, visibility, resizing, dragging, source removal while recording, and clean Program popout.
- Studio transitions: staged Preview stays isolated until Take/Fade; disabling Studio applies Preview; both preferences survive real process restarts.
- Transparent overlays: static image reuse, changed pixels, and alpha over the underlying source.
- Recorded output: decoded H.264/AAC verifies the full-size/positioned source, hidden/removed sources, Break, audible browser tone and correctly gated silence. The camera/browser performance recording also verifies 1280x720/30, 48 kHz stereo, changing video frames, tone and final Break.
- Audio/scroll regression: visible green/amber/red track levels, numeric dBFS, peak hold, clipping, mute, master meter, C920 microphone, and distinct audio/camera/output scroll surfaces. The meter workflow uses the relocated single-canvas audio panel.
- Studio compositor regression: staged geometry, dragging, layer order, source replacement and Fade.

Build/packaging and the pixel-conversion executable are gates, separate from the application workflows. The conversion gate covers unaligned input/output, padded rows, SIMD tails, odd sizes, black/white endpoints, rounded chroma and output guard bytes.

## Reproduction

Set `VIDEO_MIXER_PROFILE` to a local CSV path before starting the portable app to collect cumulative calls, wall nanoseconds and current-thread cycles every five seconds. It is off by default and records no source names, URLs, frames or audio. `encoder_submit` includes `encoder_prepare`.

Run `e2e/mixer-performance-e2e.py --pid <pid> --report <private-directory>` from `native-qt`. Set `VIDEO_MIXER_E2E_LAYOUT=single` for single canvas; the default exercises Studio. Start with the standard Opening/Main/Break show. The script creates and removes QA sources and validates its recording. `mixer-direct-e2e.py`, `mixer-meters-e2e.py` and `mixer-compositor-e2e.py` cover the workflows above.

Profiled executable SHA-256: `2df2b539ce98bc86b58885b331374c5ed93150f6726e9677576ed5c6004de50a`. Final portable SHA-256: `b62e5b9bced4d697a23bf61f6cfc87b0ef1777da92183b73951834373edbcf69`. The final build adds the minimum audio-panel width needed to prevent horizontal clipping after mode switches and clarifies the Studio status text; its media pipeline is unchanged. Application workflows are rerun on that final portable. The current setup package is packaging-gate checked, not installed for this version's workflows.
