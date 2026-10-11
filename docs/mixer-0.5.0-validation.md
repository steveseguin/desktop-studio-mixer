# Desktop Studio 0.5.0 validation

Windows x64, 2026-10-11. Workflows run the packaged Release application using
the connected Logitech C920, private browser fixtures, and local stream
receivers. Guest checks use two synthetic Edge publishers through VDO.Ninja
signaling. Private shows, invitations, screenshots, audio/video recordings,
device identifiers and detailed reports remain in ignored
`native-qt/e2e/reports/studio-050/`.

## Changes exercised

- Native canvas formats: 720p30/60, 1080p30/60 and portrait 1080x1920/30.
  Mixer streaming follows the canvas format even when Capture output retains
  a different preset. GPU composition uses Direct2D/D3D11; CPU composition
  remains available. Both still exchange frames through CPU memory.
- Independent Program recording, alongside streaming and local outputs.
- Media Foundation video/audio files with looping, pause and restart.
- Corner resizing, crop, Fit/Fill/Stretch, lock, snapping, scene shortcuts,
  undo/redo and show-file persistence.
- Per-track RNNoise, gate and compressor, off by default.
- Backstage admission, guest Program returns, mix-minus and private talkback.
- Spout Program output and a per-user, 64-bit DirectShow virtual camera.
- Asynchronous native-source startup and retirement, automatic recovery,
  and independent bordered scroll panels that keep the canvas visible.

## Application workflow results

| Workflow | Observed result |
| --- | --- |
| Media and recording | Moving H.264/AAC MP4 loops beyond its duration, pauses, resumes and replays after reaching the end. Independent native 1080p60 recording decodes successfully. |
| Editing | CPU/GPU alpha, crop and Fit/Fill/Stretch compositions match closely: maximum mean channel difference 0.046/255, 99th percentile zero. Pointer resizing, locking, Ctrl+Z/Ctrl+Y, scene shortcuts, removed-source undo and Save/Open round trips pass. |
| Streaming with recording | Record before streaming, stop recording while SRT continues, start another recording during SRT, then stop SRT while recording continues. All three received/recorded files decode with H.264, AAC and the expected 731 Hz tone. |
| RTMP and WHIP | Private MediaMTX receivers obtain native 1920x1080 video from the 60 fps canvas, with audible audio. Eight-second receiver files decode to 434 and 448 frames respectively, including startup. Mixer canvas format overrides the stale output-tab preset. |
| Guests | Two browser guests publish camera/audio and receive native Program. Backstage stays off Program; Studio admission requires Take, including re-admission. Own-tone suppression is verified in each guest return. |
| Private talkback | Host microphone reaches both guests; the simultaneous Program recording has RMS 0.0 during private talkback. |
| Spout | A separate receiver obtains 60 frames, 30 distinct, at 1280x720 on the final build. |
| Virtual camera | FFmpeg DirectShow and Edge getUserMedia receive moving Program. Final-build Edge obtains 45 frames, 44 distinct, at 1280x720/30. Registration and unregistration work without elevation. |
| Camera | Physical C920 brightness, zoom, exposure, focus and white balance, including supported Auto controls. 1080p30, 720p30 and 640x480/15 modes; reconnect and application restart preserve requested settings. Original physical driver settings restored. |
| Production | Window capture, browser reconnect/transparency, camera/microphone, transparent chat fixture, headphone monitoring, channel mute, off-air listening and desktop audio excluding the monitor all pass. |
| Effects | Decoded recordings show compressor reduction of 10.74 dB, gate closure for a quiet signal, and RNNoise reduction of 12.46 dB on the noise fixture. These synthetic measurements are not a speech-quality evaluation. |
| Smaller windows | Both layouts at 1366x768 retain accessible controls, nonoverlapping meters and visibly bounded scroll areas. Each tool panel scrolls independently. |
| Visual audio meters | Final-build quiet (-34 dBFS), amber (-8 dBFS) and red (-2 dBFS) readings, peak hold, clipping and track/master mute pass. Audio, camera-control and output-settings scroll boundaries are verified. |
| Recovery | Close/reopen a window with the same executable/title: capture resumes automatically. Five reconnects retain usable UI Automation. Killing the source's own WebView2 process also recovers automatically. |

The recovery workflow caught a real frozen-window failure: Windows Graphics
Capture could remain marked active after the target closed. Capture health now
also validates the HWND and owning process. Browser recovery uses an animated
fixture; a static browser legitimately reports no new frames.

Repeated source retirement exposed a COM apartment lifetime bug. An audio
capture object created on one worker could be destroyed on another thread,
uninitializing the wrong apartment and breaking accessibility. Process MTA
usage now uses a cross-thread-safe cookie; thread-local WinRT initialization
and uninitialization are paired. The complete production workflow passed
after this fix.

## Performance and package verification

Measured on an Intel Core Ultra 7 265K (20 logical cores), Intel Graphics and
NVIDIA TITAN RTX, using the installed application at 1280x720/30 in single
canvas mode. **100% means one logical core**, not the entire machine. Tree
figures include WebView2 and encoder children. These are sequential 30-second
samples, not hardware-independent guarantees.

| Scenario | CPU composition: app / tree | GPU composition: app / tree |
| --- | ---: | ---: |
| Static title | 1.04% / 1.04% | 0.73% / 0.73% |
| C920 camera | 7.54% / 7.54% | 10.72% / 10.72% |
| Camera + animated browser | 25.07% / 43.78% | 31.40% / 45.28% |
| Camera + browser + transparent overlay | 25.95% / 54.17% | 31.89% / 67.81% |
| Those sources + microphone + recording | 52.34% / 92.60% | 60.22% / 99.17% |
| Break scene; sources still running off air | 17.63% / 52.48% | 17.92% / 45.48% |

CPU composition is the default because it used less CPU for the visible mixes.
GPU remains available for other workloads. Single-source opaque
frames already bypass composition, so the camera-only difference is run
variation, not a backend speedup. Off-air sources keep running so switching
back remains immediate; the Break scene itself is cached. Browser delivery remains about 30 fps;
transparent overlay delivery was about 13 fps. Recording bitrate is at least
12 Mbps (24 Mbps for 1080p60), rather than reducing quality to lower load.

The GPU review removed an unnecessary conversion of opaque frames to alpha
images, preserving their format and GPU cache keys. The initial GPU
camera/browser sample was 34.37% app CPU; the corrected sample was 31.40%.
The larger CPU/GPU comparison above determines the default. It does not claim
a general reduction from 0.4.3: opaque browsers now deliver 30 fps versus the
earlier approximately 21.5 fps, and guest returns/independent outputs add work.
Transparent WebView snapshots remain a substantial cost.

Both backend profiles use executable SHA-256
`b6f51bebb7f7baf247ceeddf1ed81a9f8a62fba8f18e677a7a9adda6ce83b49b`.
The final source change after profiling selects CPU by default; profiles
explicitly selected each backend. Full recording decode, the 731 Hz audio
tone, changing frames and the final scene are checked by the profiling script.

Final executable SHA-256:
`1e292212109495cd069e84f8dbaf55d6b8967047a42d0bd335fd40caba988979`.
The final binary re-ran media playback/replay, native 1080p60 recording,
editing/show persistence, visual meters, Spout/Edge camera output, independent
SRT streaming/recording and both smaller-window layouts.
Opening a legacy show without a renderer preference
selects CPU composition. Explicit saved renderer choices remain respected.

The meter workflow initially stopped after closing the camera dialog because
the automation provider returned a transient element without an automation
ID. The shared helper now ignores those elements; the complete rerun passed.

The disposable per-user installation upgraded from 0.4.3 to 0.5.0 without
changing the show file. Its executable matched the portable package and its
third-party license files were present. Uninstall preserves shows, removes its
own virtual-camera registration, and leaves a registration owned by a separate
portable copy intact. Both ownership cases were exercised, followed by removal
of the QA registration and installation.

The original show and original exported application settings were restored
byte-for-byte. Original physical camera controls were restored. Task-owned
media processes were closed. Game Capture retains its original HEAD and its
single pre-existing untracked documentation file; its source tree is unchanged.
Release compilation, Python syntax, Git whitespace and package-content checks
are gates, not substitutes for the application workflows above.

## Reproduction

Back up the show and application settings first. Scripts intentionally edit a
disposable show. Start the packaged app and supply `--pid <pid> --report <dir>`
to scripts in `native-qt/e2e`, running from `native-qt`:

- `mixer-050-e2e.py`, `mixer-editing-050-e2e.py`
- `mixer-outputs-050-e2e.py`, `mixer-protocols-050-e2e.py`
- `mixer-returns-050-e2e.py`, `mixer-local-outputs-050-e2e.py`
- `mixer-audio-effects-050-e2e.py`, `mixer-camera-e2e.py`
- `mixer-production-e2e.py`, `mixer-recovery-050-e2e.py`
- `mixer-layout-e2e.py`, `mixer-performance-e2e.py`

Before the local-output workflow, build its independent receiver with
`cmake --build build-mixer --target studio_spout_output_probe` from a Visual
Studio compiler environment. It is placed under `build-mixer/qa`, outside the
application package.

Install the Python dependencies from `e2e/mixer-requirements.txt`. The protocol
workflow requires MediaMTX (`MEDIAMTX_PATH` points to its executable). The
guest-return workflow requires launching the app with
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=0 --remote-debugging-address=127.0.0.1`
so it can inspect only the private source profiles it creates.

Use `VIDEO_MIXER_E2E_LAYOUT=single` and select `GPU composition` or
`CPU composition` with `VIDEO_MIXER_E2E_BACKEND` for profiling. Its CPU
convention is one logical core = 100%; tree totals include browser and encoder
children. Each scenario has five seconds of warm-up and 30 seconds of sampling.
Build, syntax, whitespace and artifact checks are gates, separate from actual
application workflows.

## Remaining limits

- No real phone/remote-network guest or connected social-account session was
  available. Synthetic guests and chat/browser integration were exercised.
- Physical USB unplug/replug and sustained multi-hour production need separate
  hardware/soak runs. Reconnect and missing-window recovery were exercised.
- Transparent browser capture still uses PNG snapshots capped at 15 fps.
  Opaque browser capture targets 30 fps; hardware/page activity affect cadence.
- GPU composition still reads back to CPU memory. Returns and independent
  recording use separate encoders; more guests increase load and may exceed
  a GPU's simultaneous encoder capacity.
- Virtual camera compatibility is verified with 64-bit FFmpeg and Edge,
  not every conferencing application or 32-bit consumer.
- Packages remain unsigned development builds. No public stream or release
  publication is part of this verification.
