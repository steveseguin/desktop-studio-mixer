# Desktop Studio 0.6.0 validation

Windows x64, 2026-10-11. These are real workflows in the packaged Release
application, with a Logitech C920, Intel graphics, NVIDIA TITAN RTX,
private local browser pages and stream receivers. Private recordings, device
identifiers, shows and detailed reports remain in ignored
`native-qt/e2e/reports/studio-isolation/`.

## Process boundaries

Studio camera, microphone/desktop audio, window/Spout inputs, media decoding,
WebView2 hosting, video encoding and each publishing/recording destination
run in separate instances of the shipped executable. Helpers enter a fixed
internal worker dispatch before application/settings/control-server startup.

IPC uses anonymous mappings and an explicit inherited-handle list. No new
listening port, public named pipe or shared file is needed. Helper startup
arguments contain handle values, not source URLs or credentials. This does
not hide destination URLs from the existing FFmpeg muxer command line;
other software running as the same Windows user is outside this boundary.

The parent creates each worker suspended, assigns a kill-on-close job, then
resumes it. Descendants belong to the same job. Media slots, PCM batches,
JSON messages and output queues have bounds. Frame buffers reserve space for
up to 8192-pixel sources but commit only the bytes used; reusable image
buffers and static-overlay identities avoid repeated allocation/copying.

Source heartbeats detect hangs. Encoders restart with the same requested
configuration and a fresh keyframe, with bounded retries. Persistent missing
video cannot be masked by continued audio. Publishers reconnect independently.
Recording destination failures preserve the original file and never reopen
it automatically. A clean stop waits for the muxer and reports finalization
errors rather than falsely saying the recording was saved.

## Browser hardening and hostile-page workflow

WebView2 uses Evergreen with its normal Chromium sandbox; the browser engine
is not bundled. Receiving pages have host objects, developer tools, default
script dialogs and context menus disabled. Permissions deny camera,
microphone, clipboard, notifications and screen capture. Popups, downloads,
external URI handlers, local-file navigation and unsafe frame navigation are
blocked. Profiles cannot traverse outside their app-owned directory. Older
runtimes missing required security interfaces, or failed security-handler
registration, prevent navigation and ask the user to update/reconnect.
Replacing a browser source waits for its previous host/job to release the
profile; the main window and thread pool do not block on that dependency.
Newly created chat/talkback panels retain their initial Open request until
the asynchronous host is ready. Camera startup returns its initial settings
in the synchronized reply: a concurrent frame publication can no longer make
a successful mode change appear as 0x0 with missing controls.

The only receiving-page bridge accepts bounded audio/error messages after
checking the sending page. Unknown command types do nothing. JSON is capped
before conversion; PCM format, sample rate, chunk size and message/byte rates
are validated. There is no script-to-shell, file or native-command API.
This follows [Microsoft WebView2 security guidance](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security).

The packaged hostile-page workflow verified permission rejection, file/custom
URI rejection, blocked popups/downloads and malformed/oversized messages. An
8-second message flood left recording active, with a 61.09 ms maximum main
window probe and no parent RSS growth. Killing the browser host recovered
automatically. The entire recording decoded without errors. Unauthenticated
requests to the separately enabled QA control API could not quit the app.

QA enables WebView2 debugging on loopback for page instrumentation. Production
does not enable that endpoint. No security claim here rules out an unknown
WebView2/Windows vulnerability. Pages retain ordinary browser network access.

## Crash, hang and destination recovery

The final matrix (`faults-final3/isolation.json`) passed all three protocols.
Each success below means a real local receiver file decoded with changing
video, audio and no decoder warnings; an active status label alone does not
count as recovery.

| Injected failure | Observed behavior |
| --- | --- |
| Kill camera, microphone, window, media or browser helper | Only that source restarts; recording retains its encoder/output helpers. Recovery took roughly 3-4.3 seconds including the observation delay. |
| Suspend camera/browser host | Heartbeat watchdog replaces it; the app and recording continue. |
| Kill publishing encoder | Encoder restarts with the selected configuration; RTMP, SRT and WHIP receivers obtain fresh decodable media. |
| Kill independent recording encoder | Recording remains active and the publisher continues. |
| Kill RTMP/SRT FFmpeg muxer | Destination reconnects and the receiver decodes resumed video/audio. |
| Restart the receiver server | RTMP, SRT and WHIP reconnect; independent recording continues. |
| Kill publishing worker | Each protocol replaces its worker and resumes delivery. |
| Suspend RTMP publishing worker | Timed IPC detects the hang, replaces the helper and resumes delivery. |
| Kill recording worker or its FFmpeg muxer | Recording reports an error, preserves the existing file and leaves publishing running. A new recording can be started and decoded. |

The continuous capture-failure recording decoded 1,351 frames with audio RMS
0.1278 and no decoder warnings. Its frozen intervals correspond to deliberately
suspended sources; the recording itself kept running.

Two harness fixes were needed: cache the main HWND before suspending a separate
UI Automation provider, and retry receiver attachment when a dying session
closes before producing video. The final rerun retains strict full-decode and
audio checks. Recording errors now retain the specific muxer failure instead
of replacing it with a generic server-credentials message.

## Hardware recording

Real 1920x1080/60 recordings were fully decoded with strictly increasing
timestamps. Normal runs reported zero encode failures. The sampled durations
include output startup/stop overhead; they are not a lab frame-pacing benchmark.

| Selection | Active encoder | Decoded frames | Observed fps |
| --- | --- | ---: | ---: |
| Auto (Hardware, then Software) | H264 Encoder MFT | 519 | 59.98 |
| NVIDIA NVENC | FFmpeg h264_nvenc | 517 | 59.43 |
| Intel Quick Sync | FFmpeg h264_qsv | 518 | 58.31 |
| Software | H264 Encoder MFT | 530 | 60.0 |

NVIDIA Windows video-encode engine counters measured about 40% activity for
the isolated FFmpeg NVENC process. Auto and Quick Sync produced hardware-path
output; their sampled video-encode counter lists were empty, so those runs
do not provide independent engine-counter corroboration. Explicit Software
mode correctly reported no hardware encoder. AMD hardware was unavailable.

The encoder-kill workflow replaced the helper, kept the same recording live
and fully decoded 672 frames afterward. Graceful application exit left zero
owned worker descendants.

## Performance

Same 720p30 single-canvas CPU-composition scenarios as 0.5.0. Each scenario
warms for five seconds and samples for 30 seconds. **100% means one logical
CPU core**, not the whole machine. Tree CPU includes helpers, WebView2 and
FFmpeg. RSS sums shared pages in multiple processes, so it is not a measure
of unique physical memory; the sustained workflow also measures private commit.

| Scenario | Main CPU | Tree CPU | Tree RSS MiB | UI probe p95 ms |
| --- | ---: | ---: | ---: | ---: |
| static title | 1.25% | 1.25% | 243.5 | 37.24 |
| camera | 3.54% | 11.87% | 304.2 | 33.33 |
| camera and browser | 14.05% | 47.95% | 817.8 | 39.44 |
| camera browser overlay | 15.76% | 64.11% | 1253.5 | 48.97 |
| mixed recording | 19.31% | 96.36% | 1505.3 | 43.70 |
| off air sources | 6.20% | 54.66% | 1340.0 | 48.83 |

The initial isolation implementation used 132.20% of one core in the mixed
recording scenario. Optimized runs measured 89.15% and 96.36%; the final
binary measurement above is 96.36% (about 27% below the initial isolation
build). The previous 0.5.0 run measured 92.60%, so this does not establish a CPU
improvement over 0.5.0. The final run followed real Spout/camera workflows;
its different process history limits direct RSS comparisons. Isolation does
add process/memory overhead. It does not
reduce canvas resolution, recording bitrate or browser capture targets.

Opaque browser capture stayed about 29-30 fps; transparent PNG capture stayed
about 13 fps against its existing 15 fps cap. The 38.6-second performance
recording decoded to 1,159 frames, retained the expected 731 Hz tone, changed
on every sampled transition and ended on the selected Break scene.

Improvements include event-driven media wakeups, reused frame storage,
preserved opaque/static-overlay metadata, event-driven browser audio delivery
outside GUI painting/accessibility work, avoiding an extra recorder copy,
lazy D3D capture initialization and per-process timing/QoS. The latter matters
because [Windows timer resolution is per process](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod);
hidden workers still produce the visible live Program.

## Sustained recording

An initial ten-minute camera/microphone/opaque-browser/transparent-overlay
recording completed 600.42 seconds of measurement. Tree CPU averaged 74.02%
of one core, private commit grew 37.36 MiB, handles decreased by 44 and process
count remained 22. The changing canvas stayed active. After warm-up, private
commit cycled mostly around 740-758 MiB rather than increasing continuously.
A separate 526-second watcher observed no capture-worker replacements.

The complete MKV decoded successfully: 18,330 video packets with strictly
increasing timestamps spanning 610.967 seconds (setup/warm-up precedes the
measurement). A long coherent audio-tone assertion initially failed because
its phase changed across the measurement window. Post-validation of the same
file found the expected tone: median 100 ms amplitude 0.1125 and tenth
percentile 0.0430. The harness now checks short-window tone amplitudes and
recording cadence explicitly. This is not a proof of sample-accurate clock
synchronization between independently clocked devices.

A second ten-minute run on the final binary passed with exit code zero:
600.42 seconds of measurement, 18,330 video packets,
strictly increasing video timestamps, and a clean full-file decode. All sampled
worker PID sets were identical; process count stayed 22. Tree CPU averaged
78.97% of one core. Private commit changed by 32.71 MiB,
handles changed by -35, and UI probes had p95 51.47 ms and maximum
709.76 ms. Memory fluctuated during the run rather than increasing steadily.
The final audio sample had RMS 0.0805, median 100 ms tone amplitude
0.1127, and tenth percentile 0.0622.
The final strict timestamp check was also applied separately to this file.
UI probes reached 710 ms; this is not a claim that every UI
interaction stayed below 60 ms. A separate full-recording continuity check
at 160x90 found at most three exactly identical consecutive frames (100 ms);
that UI probe did not correspond to a comparable freeze in recorded output.

After recording finalized, forcibly terminating the main app left zero owned
worker descendants, including WebView2 and FFmpeg processes.

## Other packaged workflows

- Production: opaque browser capture at about 30 fps; manual reconnect and
  opaque/transparent changes; independent browser headphone monitoring, mute,
  removal and off-air Listen; real window capture, C920/microphone and SRT.
- Editing: CPU/GPU alpha/crop/Fit/Fill/Stretch comparison, drag/resize/lock,
  undo/redo, scene hotkeys and saved-show reload. Maximum mean CPU/GPU pixel
  difference was 0.061 on a 0-255 scale.
- Media boundaries: actual 4096x2160 input through IPC and recording, changing
  preview, rejected recording overwrite with unchanged file hash, and corrupt
  media errors without an application crash. This is not a 4K30 performance claim.
- Audio effects: compressor reduced the fixture by 10.75 dB, gate silenced the
  quiet fixture, and noise suppression reduced fixture RMS by 55.53 dB.
- VU/layout: quiet tracks, amber/red levels, peak hold, clip indicator and mute;
  distinct scroll boundaries; readable single-canvas and Studio layouts at
  1366x768 with no overlapping controls.
- Spout input: a separate native sender supplied changing video; killing
  the input helper recovered automatically without stopping recording. The
  complete recording decoded and contained 105 distinct sampled frames.
- Local outputs: a separate Spout consumer received 60 frames with 29 distinct
  images; real Edge camera capture received 45 frames with 44 distinct images
  at 1280x720/30 through Desktop Studio Virtual Camera.
- Output security: WHIP 403, oversized response and cross-origin redirect;
  bearer credentials were not forwarded to the other origin. Encrypted SRT
  and the simultaneous independent recording fully decoded with audio.
- Private synthetic guests: backstage, admission to Preview before Take,
  per-guest mix-minus, re-admission and private host talkback; the recording of
  Program remained silent during private talkback. The script now creates its
  own media source and separates fixture frequencies while retaining the
  original -55 dB guest-tone rejection threshold.
- Physical C920: brightness changed recorded pixels, zoom read back correctly,
  manual/Auto exposure/focus/white balance worked, and two cycles through
  1080p30, 720p30 and 640x480/15 succeeded. Reconnect and app restart restored
  saved controls/mode; the recording decoded and original controls were restored.
- Browser recovery: window close/reopen, five rapid reconnects, actual WebView2
  process termination, and three chat-dock open/close cycles.

The final hostile-browser rerun also passed against the binary containing
profile retirement, first-interaction and audio-delivery fixes. Detailed
reports retain earlier failures and distinguish app fixes from harness fixes.

## Packaging and restoration

The per-user 0.5.0 installer was installed in a disposable directory and
upgraded to 0.6.0 without changing the saved show. The installed executable
matched the portable executable byte for byte. Its real Preview/Cut/Fade,
WebView2 capture/Take, recording and mute/scene-switch workflow passed; the
complete installed-app recording decoded cleanly.

Uninstall preserved the show, removed the virtual-camera registration owned
by that installation, and left no owned workers. Afterward, the original show
and QSettings export were restored byte for byte, camera controls were restored,
and QA-created virtual-camera state was removed. Game Capture's Git HEAD and
working-tree status matched the pre-run baseline.

Validated application SHA-256:
`54228cfeb6fe3cfa9222c01851cbe4780193c5303b32928782663d8b11d487d4`.
Portable ZIP and per-user setup are under `native-qt/dist/`; no public release
was published. Final packaging also includes this updated validation report.

## Reproduction

Back up the show and application settings first. Scripts intentionally edit
a disposable show. Run from `native-qt` with the desktop Python requirements:

- `e2e/mixer-hardware-isolated-e2e.py --pid <pid> --report <private-dir>`
- `e2e/mixer-browser-security-e2e.py --pid <pid> --report <private-dir>`
- `e2e/mixer-isolation-e2e.py --pid <pid> --report <private-dir>`
- `e2e/mixer-soak-isolated-e2e.py --pid <pid> --report <private-dir>`
- `e2e/mixer-media-boundaries-e2e.py --pid <pid> --report <private-dir>`
- `e2e/mixer-output-security-e2e.py --pid <pid> --report <private-dir>`
- `e2e/mixer-spout-input-isolated-e2e.py --pid <pid> --report <private-dir>`

The Spout input workflow needs the `spout_test_sender` CMake target built with
`VERSUS_BUILD_TESTS=ON` under `native-qt/build-isolation-gates/`. The pixel
conversion/composition gates in that separate build both passed. Python syntax
and whitespace gates also passed.

The isolation matrix uses MediaMTX from `MEDIAMTX_PATH`. Security/guest checks
need QA-only loopback WebView2 debugging. The hardware script reads the
authenticated local-control discovery file under the report directory.
The soak defaults to 600 seconds; `STUDIO_SOAK_SECONDS` changes its duration.
Run GUI workflows sequentially, and keep builds/other workloads out of CPU
and memory measurements. Build, syntax and native assertions are gates, not
substitutes for these application workflows.

## Limits

These boundaries cover the Studio ingest, browser, encoder and remote/file
destination components. The UI/compositor, headphone playback, local Program
outputs, guest-return networking and some inherited capture paths still run
native code in the main process. OS/kernel/GPU-driver faults cannot be
contained by an application process boundary.

This run cannot cover every camera/driver, hardware encoder, website, streaming
service or Windows release. No physical USB unplug/replug, AMD GPU, public
stream destination, multi-hour show, actual disk-full device, or real remote
phone/social-account session is claimed. Packages remain unsigned development
builds. Transparent pages still use PNG snapshots; composition still reads
back to CPU memory.
