# Video Mixer 0.4.1: opaque frames and shared browser audio scan

Compared with 0.4.0 on Windows x64, 2026-10-11, using the packaged Release application, Logitech C920 camera/microphone and private loopback browser fixtures. Both runs use single-canvas mode, the same 1280x720/30 composition, encoder configuration, source layouts, and five-second warmup followed by about 30 seconds per scenario.

## Changes

- Camera capture marks frames opaque only after normalizing every alpha byte to 255. Unknown capture sources keep their existing alpha behavior.
- A single opaque source filling the native-size canvas can share its immutable capture image directly. Resizing, overlays, partial opacity and other layouts still use the painter.
- Scenes with an opaque background use opaque image storage. Imported translucent backgrounds retain premultiplied alpha. Fades detach shared images before painting, preserving the source and cached scenes.
- Captured camera layers retain Qt's original straight-alpha blend rounding when composited. An initial pixel gate detected rounding differences in a broader shortcut; the shipped path preserves exact pixels.
- Proven-opaque output bypasses optional alpha-background flattening. This only saves work when flattening is enabled; the default configuration already skips it.
- All mixer browser sources share one Windows process/audio-session scan per second, with reference-counted root registrations. It checks existing mute state before writing it. Reconnection/removal updates registrations; talkback windows remain outside this registry. Web Audio samples and headphone monitoring retain their separate paths.

Capture cadence, composition frame rate, output resolution, encoder settings, transparent-overlay polling and off-air source activity are unchanged. Single canvas, Studio mode, camera controls, VU meters and scroll-panel boundaries remain available.

## Measurement scope

CPU percentages use **100% = one logical core**. This Intel Core Ultra 7 265K has 20 logical cores; divide these values by 20 for a whole-machine percentage. Tree totals include WebView2 and FFmpeg children. Browser process load varies between runs, so native app savings do not imply equal savings for the entire process tree.

Aggregate instrumentation is enabled in both runs. Scoped wall times include waits; encoder submission includes preparation and must not be added to it. No ETW stack profile was collected; this comparison uses process/thread CPU and the existing in-app stage scopes.

| Scenario | 0.4.0 app | 0.4.1 app | 0.4.0 tree | 0.4.1 tree |
| --- | ---: | ---: | ---: | ---: |
| Static title | 0.78% | 0.88% | 0.78% | 0.88% |
| C920 camera | 12.30% | 8.43% | 12.30% | 8.43% |
| Camera + animated browser | 24.18% | 22.20% | 28.34% | 28.78% |
| Camera + browser + transparent overlay | 27.10% | 24.04% | 48.36% | 49.32% |
| Mixed recording + C920 microphone | 57.73% | 52.57% | 82.51% | 81.87% |
| Break, sources active off air | 20.89% | 17.06% | 40.07% | 42.63% |

The native camera-only run used about **31% less CPU**; native mixed recording used about **9% less**. These are single-run observations, not guaranteed reductions. Total recording load including children was nearly unchanged: 0.825 versus 0.819 of one core (about 4.1% of this machine). Static-title differences are tiny absolute changes near idle.

Across seven camera-only intervals (1,064 compositor calls per run), mean composition wall time fell from **0.690 ms to 0.012 ms per call**. The shortcut shares the complete camera image instead of repainting it into a new full-frame image. These averages include cache hits; they do not measure capture or display work.

Seven complete recording intervals, approximately 35 seconds each run:

| Scoped stage | 0.4.0 | 0.4.1 |
| --- | ---: | ---: |
| Mixed composition per encoded frame | 1.350 ms | 1.336 ms |
| Canvas painting per encoded frame | 0.119 ms | 0.121 ms |
| Mixer copy/submission per encoded frame | 1.000 ms | 0.992 ms |
| Encoder preparation per encoded frame | 0.605 ms | 0.594 ms |
| Encoder submission, including preparation | 2.739 ms | 2.712 ms |
| Browser session scans, two browser sources | 70 | 35 |
| Browser scan wall time per encoded frame | 0.441 ms | 0.230 ms |

The shared scan reduced its measured total wall time by about 48%. Mixed-scene composition/encoding timings were essentially unchanged; the camera-only bypass does not apply once browser/overlay layers are present. The whole-process recording difference cannot all be attributed to these scoped stages.

The final recording decoded to 1,152 frames and 38.4 seconds of audio, with H.264 1280x720/30 and AAC 48 kHz stereo. More than 99% of sampled adjacent fixture frames changed; the 731 Hz browser tone and final Break frame passed. End-of-sample source health remained around 30 fps for the C920, 21.5 fps for the animated browser and 12.7 fps for transparent snapshots. These health labels are short samples, not whole-run averages. Recording UI round-trip p95 was 40.9 ms before and 41.6 ms after, with no timeout.

## Packaged application workflows

The same portable executable was used for the after measurements and application workflows:
`4ee857cb315a2010bbbdff773cbe209759653b02b9da99a12dadfc47a5775b29` (SHA-256).

- Single-canvas editing and decoded recording: transparency changes, dragging/resizing, visibility, live source removal, clean Program popout, scene/audio gating, and layout persistence across real restarts. The recording decoded to 788 frames and verified audible/quiet phases.
- Studio composition: staged geometry stays isolated until Take/Fade; layer order, source replacement, selection hit testing and cached-scene fades passed.
- Production: C920 camera/microphone, native window capture, transparent chat, browser reconnect and capture-mode changes, mixed recording, and a private loopback SRT receiver. Received H.264/AAC audio decoded with the expected 731 Hz tone.
- Actual Windows speaker-loopback measurements: monitoring on/off, track mute, desktop capture excluding the monitor, and listening to off-air sources. Two distinct browser tones verified the shared scan across reconnect and source removal; duplicate browser playback measured zero at both fixture frequencies with monitoring off.
- Visible audio/scroll regression: quiet/amber/red track levels (-34/-8/-2 dBFS), peak hold, CLIP, track/master mute and bordered or contrasting track, camera and output scroll areas all passed in the packaged app.

The build and pixel executable are **gates**, separate from the application workflows. `opaque_composition_gate` passed 54 exact image comparisons covering camera frame ownership/detachment, opaque and translucent backgrounds behind a full-size source, transformed geometry, clipping, partial opacity, transparent overlays and fades. The comparison uses the previous ARGB/premultiplied painter paths as references.

## Reproduction and artifacts

Run `e2e/mixer-performance-e2e.py --pid <pid> --report <private-directory>` with `VIDEO_MIXER_E2E_LAYOUT=single`. Set `VIDEO_MIXER_PROFILE` to a CSV path before starting the app for scoped timings. The script resolves FFmpeg relative to itself, so it also works when launched from the repository root.

Private recordings, screenshots, stage CSVs and workflow reports are ignored under `native-qt/e2e/reports/mixer-performance-041/`. The original Game Capture repository is unchanged.

The portable ZIP and setup executable are archive-integrity gated; the executable extracted from setup matches the tested portable. Setup was not installed for this version's workflows. The original show and settings were restored after testing.
