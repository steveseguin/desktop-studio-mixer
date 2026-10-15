# Encoding defaults: packaged validation, 2026-10-14

Both Game Capture and Desktop Studio now default to **4,000 kbps video**, with **2,500 kbps Economy** available. Fresh window capture is **1280x720 at 60 fps**, hardware-first Auto. Camera format remains independently **1920x1080 at 30 fps**; a new Studio show remains **1280x720 at 30 fps**. Saved preferences are retained: an existing saved 12 Mbps setting must be changed to Standard manually.

## Encoding policy

- Windows Software H.264 uses **Media Foundation**, not x264. It requests lowest complexity (`AVEncCommonQualityVsSpeed=0`), between one and four workers, limited to half the logical processors. Both controls were accepted on this host.
- Native H.264 explicitly requests CBR with a one-second encoder reservoir. The maximum follows the selected target; it does not add 4 Mbps. Unsupported CBR requests are logged.
- Hardware-first Auto remains the default. Explicit FFmpeg NVENC/QSV/AMF retain their existing low-latency settings.
- The optional external FFmpeg libx264 path already specifies `veryfast` and `zerolatency`. The bundled LGPL FFmpeg has no libx264/libx265; this round did **not** benchmark x264 presets. Advanced software AV1/VP9 were not profiled this round.
- Rate control is not an upload cap or network pacer. AAC, transport headers, retransmissions, extra viewers and guest returns consume additional bandwidth. Independent recording retains its separate 12/24 Mbps quality settings and adds CPU/GPU work.

Microsoft documents [the H.264 encoder's rate control and complexity ranges](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder) and [worker-thread control](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencnumworkerthreads). These are Media Foundation controls, not x264 preset equivalents.

## Method and limits

Actual portable packages captured a full-screen 1920x1080 Chrome source with moving textured detail, frame identities and stereo audio. Streams went to a private local UDP receiver and were saved, then decoded strictly (`ffmpeg -xerror`) after profiling. Studio ran visibly with a real looping H.264/AAC file, UI Start/Stop buttons and isolated show settings. The C920 camera/microphone workflow also used actual interface controls. No public broadcast was started.

Host: Core Ultra 7 265K, 20 logical processors, TITAN RTX and Intel Graphics. CPU is the publisher process tree's CPU time divided by 20, expressed as whole-machine occupancy. It includes owned encoding/media workers, excludes the receiver/source browser and is not a promise of headroom on another CPU. RSS sums process working sets and may double-count shared pages. Results exclude the first five and last three resource samples. Normal clips are 20 seconds; stress/Studio clips are 30 seconds. This is comparative testing, not a long soak or a real demanding game's frame-time benchmark.

Profiling, media analysis, compilation and archive creation were serialized. Two fixture problems were corrected before the final results: an initially narrow capture window, and a UDP meter receive buffer that dropped an opening keyframe burst. The final meter has 4 MiB socket buffers. Earlier exploratory recordings are not the validated dataset.

## Game Capture results

| Configuration | Mean CPU | CPU p95 | Tree RSS MiB | UDP+IPv4 Mbps | Distinct frames/s |
|---|---:|---:|---:|---:|---:|
| Software 1080p60 / 4 Mbps | 5.26% | 5.96% | 230 | 4.38 | 59.93 |
| Software 720p60 / 4 Mbps (new default size) | 2.48% | 3.09% | 175 | 4.50 | 60.00 |
| Auto 720p60 / 4 Mbps | 3.25% | 4.02% | 198 | 4.50 | 59.80 |
| Software 720p60 / 2.5 Mbps | 3.15% | 3.62% | 177 | 2.94 | 60.00 |
| Software 720p30 / 4 Mbps | 1.74% | 2.12% | 174 | 4.36 | 30.00 |
| Intel QSV 1080p60 / 4 Mbps | 3.20% | 3.59% | 351 | 4.52 | 59.20 |
| Advanced FFmpeg NVENC 1080p60 / 4 Mbps | 3.25% | 3.76% | 348 | 4.35 | 58.65 |

All seven final streams decoded cleanly, retained stereo audio and passed continuity checks. The default software output had 17 ms p95 frame spacing and approximately 4.58 Mbps one-second peak UDP traffic. Native Auto also maintained approximately 60 fps. Decoded stills and the actual settings/Studio windows were visually reviewed: text and color bars remained legible, while Economy showed softer moving texture. This is a qualitative inspection, not a calibrated image-quality score.

The earlier package at an explicit **1080p60 / 12 Mbps** reference setting used **6.48% CPU**, **235 MiB** process-tree RSS and **12.86 Mbps** UDP+IPv4 traffic with software encoding. The new **720p60 / 4 Mbps** software configuration used **2.48% CPU**, **175 MiB** RSS and **4.50 Mbps** traffic. This comparison changes resolution and bitrate together; it does not attribute the entire improvement to the encoder complexity knob.

At matching 4 Mbps, old/new software CPU was 5.47/5.26% at 1080p60 and 2.52/2.48% at 720p60. These small differences are within short-run variation. Lower resolution/frame rate delivered the clearest CPU saving. Economy reduced upload to 2.94 Mbps but did not reliably reduce CPU.

### CPU contention and timing

Sixteen bounded CPU workers at 90% duty produced 90.7% average total-system CPU during the software run and 88.9% during Auto. Software retained **59.87 distinct fps**, Auto **59.86**, both approximately 60 decoded fps with 17 ms p95 spacing, clean decode, no unexpected 10 ms audio-silence windows and no capture queue drops. Publisher CPU was 2.88/2.53% respectively. This is synthetic CPU competition, not GPU saturation or a demanding game.

Separate frame traces at the new default measured software/Auto median encoder-output waits of 3.73/3.25 ms; p95 was 4.08/3.57 ms. Median capture readback was about 5 ms and includes GPU waiting; these wall times are not CPU execution time. Queue medians were 23.94/17.70 ms. Audio/video pulse offsets in the browser fixture are uncalibrated; no new absolute lip-sync accuracy claim is made.

### Remaining hardware-path observations

Explicit QSV has startup queue drops while its FFmpeg worker warms up (about 59.2 fps over the complete 20-second clip, 100 ms maximum gap). The advanced FFmpeg NVENC 1080p60 path averaged **58.65 fps**, with occasional gaps up to 67 ms, despite clean decode. These paths should receive further pacing/startup work; their results are not described as locked 60 fps. Native default Auto did not show that behavior.

## Desktop Studio results

All cases publish at the new 4 Mbps target. The media source itself decodes a detailed 720p60 file, including when the canvas outputs 30 fps.

| Configuration | Mean CPU | Tree RSS MiB | Distinct frames/s | Control API p95 |
|---|---:|---:|---:|---:|
| software-720p30 | 3.52% | 339 | 30.00 | 26.0 ms |
| software-1080p60 | 8.08% | 406 | 59.96 | 30.4 ms |
| auto-720p30-record | 5.11% | 570 | 30.00 | 26.5 ms |
| qsv-720p60 | 4.60% | 472 | 59.53 | 26.0 ms |

All four received files plus the independent local recording decoded cleanly. Program recording continued while publishing; output stopped and recording finalized normally. Every steady 10 ms audio window in these five files remained above RMS 0.026, with both expected stereo tones. Canvas 30/60 fps outputs had 34/17 ms p95 frame spacing. QSV's startup still lowers its whole-clip average slightly. Control API response time does not substitute for a measured game frame-time or UI input latency.

## Fixes found through actual use

- Capture shutdown now retires a shared callback guard and waits for in-flight resource access before freeing D3D textures. Late WinRT callbacks see a retired owner. An earlier QSV shutdown crashed in `d3d11.dll`; eight packaged QSV publish/quit repetitions after the change all exited normally. This is evidence for the hardening, not proof that every driver crash is prevented.
- Studio's media loop preserved neither the original presentation clock nor AAC priming semantics. Tests found 30?40 ms of loop silence. Automatic loops now keep a continuous clock, trim codec padding to file duration, and discard the first AAC priming block when Windows exposes it at the same timestamp as the real first block. Manual seeks still reset the clock. A separate PCM WAV loop/publish run also retained continuous stereo audio, and video-only seeking/EOF/restart passed. The rebuilt publishing/recording matrix had no detected loop silence. Paused seeking, held position, changed cue pictures, non-looping EOF, restart and a near-end looping seek all passed.
- The physical Logitech C920 interface workflow passed: 4 Mbps Standard and 2.5 Mbps Economy selection; window defaults 720p60; independent camera output 1080p30; no microphone; mute/unmute on primary and additional routes; saved settings restored. Explicit AMF fails clearly on this non-AMD host. Actual AMD encoding and OBS playback remain unverified.

## Evidence and reproduction

Local reports are in `native-qt/e2e/reports/encoder-defaults-2026-10-14/` in the respective checkout. Game's primary directories are `verified-baseline`, `verified-fullscreen`, `verified-stress`, `high-rate-reference`, `verified-ui-camera`, `final-shutdown` and `final-profile`. Studio's primary directories are `verified-final`, `transport-final`, `audio-only-final` and `video-only-final`. Earlier `verified`/`verified-loop` failures are retained as diagnostic evidence and superseded by the final pass.

Game package executable SHA-256: `8815933a6ece16de48bde4de9c34f198121b3a02e9701589263ce381ae4687b2`.
Studio package executable SHA-256: `f1412973f8d75109b7943bd98e2df7810e73f9b33349534328052116e4c2f8a0`.

The Game driver accepts a matrix such as:

```json
[{"id":"udp-software-default","resolution":"1280x720","fps":60,"video-encoder":"software"},
 {"id":"udp-auto-default","resolution":"1280x720","fps":60,"video-encoder":"auto"}]
```

From Game Capture's `native-qt`, using the existing local QA prerequisites:

```powershell
node e2e/external-output-packaged.js --publisher=dist/game-capture-encoder-final/game-capture.exe --output=e2e/reports/my-run --matrix=matrix.json --motion=true --fullscreen=true --wire-meter=true --seconds=30
# Add --load-workers=16 for bounded CPU contention. Analyze after the run ends.
.cache/desktop-ui-python/Scripts/python.exe e2e/summarize-encoder-defaults.py e2e/reports/my-run --ffmpeg dist/game-capture-encoder-final/ffmpeg/bin/ffmpeg.exe
```

Studio uses `e2e/mixer-encoder-defaults-e2e.py --package dist/desktop-studio-encoder-final --report e2e/reports/my-run`; `--transport-only` exercises seeks/EOF/restart. The helper uses the sibling Game Capture QA environment/resource sampler. Both UI gates passed 93 checks with one platform-specific skip; these are gates, separate from the packaged workflows above.

Not covered by this round: low-core-count hardware, a demanding game's own frame-time impact, GPU/VRAM saturation, AMD hardware, alternate external x264 builds/presets, long-duration memory growth, or an exhaustive rerun of every network protocol and loss-recovery mode. A clean-machine run remains deferred. Existing preferences and user shows were restored; no GitHub release or installer was published.

## Extended validation later on October 14

This follow-up used the same complete portable packages above, private local receivers and isolated shows. Profiling and decoding remained sequential. Reports are under `native-qt/e2e/reports/extended-validation-2026-10-14/` in each checkout.

### Ten-minute Studio recording and software publishing

The visible Studio published 720p30 / 4 Mbps software video while independently recording Program. A looping 12-second H.264/AAC source exercised about 50 loop boundaries. Sixteen bounded CPU workers ran for 90 seconds partway through the session.

- Received video: 29.99997 fps over 599.9 seconds; 34 ms p95 and maximum frame spacing. Local recording: 29.99588 fps over 601.8 seconds, 34 ms p95 and one maximum interval of 116 ms.
- Total-system CPU averaged 88.0% during contention. Studio's process tree averaged 4.85% before, 4.70% during and 4.98% after contention.
- Recording drops remained at the startup count of 14. All nine child PIDs remained unchanged. Both complete files decoded cleanly; the minimum 10 ms audio RMS stayed above 0.027, including loop boundaries.
- Main-process private memory grew 4.32 MiB between early and recovered windows; process-tree RSS grew 8.56 MiB. Median main-process handles decreased by 12. These are finite observations, not proof of no leaks.
- Control API p95 was 24.0 ms, maximum 28.7 ms. An actual application screenshot during 89% system CPU showed publishing, recording and active audio meters; the screenshot API returned in 40 ms.

Evidence: `software-recording-soak/results.json`, `soak-analysis.json` and `active-load.png`. The driver now supports `--soak-seconds 600 --load-workers 16`; run `e2e/analyze-studio-encoder-soak.py REPORT --ffmpeg FFMPEG` only after the app exits.

### Fault isolation, sync, recoverable recordings and logs

The headless fault workflow ran healthy frame-identified video and matching audio pulses alongside a browser source and RTMP publishing. It injected browser CPU overload, browser process termination/hang, 35 seconds of network starvation followed by recovery without closing sockets, publishing worker termination, and simultaneous browser/publisher faults.

The healthy media worker never restarted. Recording drops stayed at their startup count of nine. Browser audio was detected before and after recovery. Local and received files decoded cleanly. Local A/V offset p95 was 26 ms; received p95 was 24–30 ms. Post-recovery received pictures were about 0.20–0.23 seconds behind the live source. API p95/max were 28/38 ms. Five bounded worker-failure log entries were recorded.

Separate tests verified:

- A real app access violation (`0xC0000005`) during MKV recording produced a playable, fully decodable recording, crash JSON, minidump and log snapshot. Owned workers exited. Relaunch restored the source without automatically restarting recording or publishing, and a new recording decoded cleanly.
- Five sessions of log rotation retained at most four log files and preserved the crash snapshot. A synthetic private key was absent from logs and the generated issue report. Making the logging directory unavailable did not prevent launching or recording.
- Killing the Program recording writer did not stop four independent source recordings. Their files remained decodable; off-screen recording, mute/private-break routing, and black/silent output after removing a source passed.

Evidence: `fault-quality/fault-quality.json`, `crash-recovery/diagnostics-fault.json`, `recording-isolation/recording-isolation.json`.

### Headless frame-timing fix

The fault test exposed uneven headless frame timing: approximately 30 fps on average, but 47 ms p95 intervals and phase-dependent duplicate/skip pairs. A controlled 60-second recording changed Windows timer handling midway through the same owned app process; p95 intervals fell from 47 to 35 ms.

Windows can ignore timer-resolution requests for invisible/inaudible windows. Headless Studio now requests a 1 ms clock and explicitly keeps that request honored. The change is limited to `--studio-headless`, leaves process priority and CPU execution-speed policy alone, and balances the timer request on normal teardown. Older Windows can still use the timer request when the newer policy is unavailable. See Microsoft's [timer-resolution documentation](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod) and [process policy documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation).

A complete rebuilt package, `dist/desktop-studio-timing-review`, passed a fresh 60-second recording without instrumentation: 30 fps, 34–35 ms p95 intervals, 36 ms maximum interval and no duplicate pictures in the two measured 23-second windows. Process-tree CPU averaged 2.45% for this simpler media-plus-recording workload. This CPU number is not comparable to the multi-output soak.

Executable SHA-256: `f921fdc70011315b213af8f544bc33d9a62a6886c59266ab9d18eb098caf2880`. Evidence: `headless-timer-probe/cadence.json`, `headless-timing-fixed/cadence.json`. Reproduce with `e2e/mixer-headless-cadence-e2e.py --package PACKAGE --report REPORT --fixture FRAME_CLOCK_MP4`; the optional `--timer-experiment` is diagnostic only.

The full failure sequence also passed on this rebuilt package (`fault-timing-fixed/fault-quality.json`): 30.00 fps over 6,689 frames, 34 ms p95 and 45 ms maximum frame interval, eight duplicate/skip pairs, local A/V offset p95/max 22 ms, and received A/V p95 6–11.5 ms. Recording drops stayed at nine, healthy source restarts stayed at zero, and browser audio returned after recovery. API p95/max were 26/41 ms. The visible app's paused seeking, held position, EOF, restart and loop-boundary cue checks also passed (`visible-timing-fixed/results.json`).

### Longer Game Capture encoder/protocol runs

All cases used fresh 4 Mbps defaults, a moving full-screen source and real stereo audio. All seven received files decoded cleanly and passed audio-continuity checks. CPU percentages are whole-machine process-tree occupancy on this 20-logical-processor host.

| Selection / output | Recorded duration | Mean CPU | Distinct fps | Capture queue drops |
|---|---:|---:|---:|---:|
| FFmpeg NVENC 1080p60 / UDP | 90 s | 3.19% | 58.28 | 158 |
| Explicit NVENC 1080p60 / UDP | 60 s | 3.02% | 58.75 | 77 |
| Auto 1080p60 / UDP | 90 s | 5.17% | 60.00 | 0 |
| Software 720p60 / UDP | 90 s | 2.56% | 60.00 | 0 |
| Auto 720p60 / WHIP | 30 s | 3.25% | 60.00 | 0 |
| Auto 720p60 / SRT | 20 s before + 20 s after receiver restart | 2.17% | 60.00 | 0 |

SRT returned to publishing 3.8 seconds after the receiver restart; this is reconnect time, not steady playback latency. UDP+IPv4 traffic averaged 4.35–4.48 Mbps, with one-second peaks no higher than 4.79 Mbps. Both explicit NVENC and the advanced FFmpeg choice selected FFmpeg `h264_nvenc` on this host; the below-60-fps limitation therefore affects both selections. Auto selected a hardware Media Foundation encoder and held 60 fps.

Evidence: Game's `encoder-and-protocols/results.json`, `summary.json` and `analysis.json`. The matrix accepts per-case `seconds` to mix long encoder runs and shorter reconnect cases.

Remaining coverage: actual AMD hardware/OBS, weaker CPUs, demanding-game frame times, GPU/VRAM saturation, overnight memory growth, and a clean machine. This round did not repeat every protocol's packet-loss/FEC matrix or claim calibrated absolute lip sync from the browser-generated Game fixture. Explicit NVENC pacing remains unresolved. No release or installer was published by this follow-up.

## Additional resilience and fuzz validation on October 14

Both repositories were pulled with `git pull --ff-only` before this round; both were already current (Game Capture `b1967aaa`, Desktop Studio `4a36a65b`). Real workflows used complete portable packages, isolated settings/shows, and private localhost receivers. Resource runs, compilation and post-run decoding were serialized.

### Fixes found by the workflows

- **First launch at high display scaling:** both initial windows could extend beyond the usable desktop at 150–300% scaling. They now start maximized when their initial size does not fit the screen's work area. Existing scroll areas keep controls accessible without shrinking text or changing the theme.
- **Unavailable explicitly selected microphone:** the native Capture output path used to substitute the Windows default microphone. Both apps now report the unavailable selection and leave that input off. Explicitly choosing the Windows default still works as before; healthy game audio continues independently.
- **Publishing with no incoming audio:** removing that microphone fallback exposed FFmpeg waiting indefinitely for a declared audio track. The mux worker now supplies 48 kHz silence after 250 ms without PCM, only while video advances. It first drains queued real audio, bounds catch-up to one second, and trims any overlap when real audio resumes. Studio tracks this separately for each recording track. This changes the FFmpeg mux path, not WHIP's Opus transport or microphone selection. It cannot keep an absent video encoder alive.

### Actual packaged workflows

- **Display/UI:** opened both apps at 100%, 150%, 200%, 250% and 300% process-local Qt scaling; checked initial/maximized bounds, used the tabs/advanced toggle and scrollbars, and reviewed screenshots. All ten initial-window containment checks passed after the fix. Reports: Game checkout `game-display-fixed/` and `studio-display-fixed/`.
- **Large capture surfaces and reduced CPU availability:** Game Capture handled 4K, 8K, 5120×1440 and odd 641×479 windows, 24 rapid resizes, hiding/restoring the source and closing it. The 8K surface was GPU-scaled to 720p before readback. Capture stayed approximately 29.85–30.04 fps; the 8K phase had one queue drop. Restricting the app and owned children to two logical CPUs held 30.01 fps without additional capture drops. Source closure produced the documented clean headless exit, not an OS crash.
- **Studio isolation under large sources:** simultaneous local recording and software UDP publishing survived 4K/8K/portrait/odd surfaces, an 8192×4096 image, a truncated image, 20 rapid resizes, two-CPU affinity, killing the window worker, hanging the audio worker and destroying the captured window. Healthy media never restarted; recording drops remained 11→11. Both full files decoded cleanly with continuous healthy audio. Main-process private memory peaked at 299 MiB for the 8K image and returned to 37 MiB. API response maximum was 121 ms during source closure, otherwise at most 32 ms.
- **Unusual audio files in Studio:** 8/11.025/22.05/44.1/96/192 kHz, PCM 8/16/24/32-bit, float32, and mono/stereo/3/6/8 channels. Recorded Program and a separate source track; verified expected tones, finite samples and complete decoding. Float64 was rejected with an understandable unsupported-track error. NaN/infinity float input did not contaminate the recording. Empty, truncated, zero-rate, zero-channel and random files, plus unavailable camera/microphone/window identifiers, left the healthy source running. Program's minimum 10 ms RMS stayed above 0.0267; per-format API responses stayed below 28 ms.
- **Unavailable native devices in both apps:** nonexistent camera and Spout selections failed normally; an unavailable optional microphone preserved the source's 440/880 Hz stereo audio; an unavailable primary microphone published playable video with silent audio. Neither microphone case selected another device.
- **Fourteen-second audio-delivery fault in both apps:** paused only the owned native WASAPI capture thread while video continued to the real UDP receiver. Both outputs stayed live with zero reconnects, delivered approximately 14 seconds of silence, and restored the expected stereo tones. Complete files decoded cleanly. Video timestamp spacing p95 was 34 ms; maximum was 37 ms for Game Capture and 36 ms for Studio's native Capture output path.
- **Control API fuzzing in both apps:** malformed authentication/HTTP framing, oversized requests, 256 seeded invalid JSON commands, slow incomplete connections, connection limits, pipelining and 128 concurrent mixed valid/invalid requests. Health and schema still worked afterward; normal quit removed discovery files. Studio's driver now explicitly uses a disposable show.

- **Final Studio combined-fault regression:** the package with all fixes passed browser CPU overload/crash/hang, 35-second network starvation, publisher termination, and simultaneous browser/publisher failure. The 6,701-frame local recording averaged 30.00 fps, with 34 ms p95 / 45 ms maximum spacing and four duplicate/skip pairs. Local A/V offset stayed within 13 ms; received p95 offsets were 2–9.5 ms. Recording drops stayed 9→9, healthy sources never restarted, and browser audio returned. All recordings decoded cleanly. API p95/max were 25/31 ms; recovered pictures were 0.13–0.20 seconds behind the live source. Evidence: Studio `fault-quality-final/fault-quality.json`.

- **Game Capture mux regression after the fix:** software 720p30 / 4 Mbps SRT before and after receiver restart, RTSP over TCP, and mono UDP all passed. All four received recordings strictly decoded without errors, held approximately 30 fps / 34 ms p95 frame spacing, and had zero unexpected low-energy audio windows. SRT returned to publishing 3.82 seconds after receiver restart. Whole-machine process-tree CPU averaged 1.50–1.62% on this 20-logical-processor host. Evidence: Game `mux-regression/results.json`, `analysis.json`, and per-case resource samples.

Additional cadence analysis of Studio's large-source fault run found steady received output (30.00 fps; p95/max frame spacing 34 ms). Local recording averaged 29.95 fps with 34 ms p95 spacing, but had three isolated gaps over 70 ms: 105, 85 and 162 ms. Recording queue-drop counters did not increase. This remaining local-recording pacing limitation under source/load faults is not claimed fixed; see Studio `source-baseline/cadence.json`.

Large-surface runs used the intermediate packages containing the display fix; the audio-format, missing-device, audio-stall and Studio API-fuzz runs above used the final packages with all three fixes. Reports include executable hashes, measurements and failure logs under `native-qt/e2e/reports/resilience-2026-10-14/` in the corresponding checkout.

Final complete review packages:

- Game Capture: `native-qt/dist/game-capture-resilience-review/`; executable SHA-256 `e0b9a5fbbc13ef735f38c9ff016a5f37bdd307f3c7f7ee6a322762c872a36179`.
- Desktop Studio: `native-qt/dist/desktop-studio-resilience-review/`; executable SHA-256 `855f90263695304d7d893223c04db34fba155a9c95fbcca4ff8d463df8d4d244`.

Reproduction drivers are Game's `display-scaling-packaged.py` (`--studio` for Studio), `capture-resilience-packaged.py`, `device-failure-packaged.py`, and `audio-stall-packaged.py`; Studio's `mixer-audio-format-resilience-e2e.py` and `mixer-source-resilience-e2e.py`; and each repository's `local-control-fuzz-packaged.py`. The Studio source driver uses the sibling Game QA fixture. Audio-stall injection uses Frida only in the owned test process and makes no Windows device configuration changes.

Coverage limits: file decoding is not proof of support for every physical audio driver's format. Two-CPU affinity on this Core Ultra host is not an older laptop, and a large window surface is not a real 8K/HDR/mixed-DPI monitor configuration. No physical unplug/replug, AMD/OBS, GPU removal, real weak-machine/game-impact or overnight soak was performed in this round. Clean-machine testing remains deferred. Explicit NVENC pacing noted above remains unresolved. These are local review builds; no GitHub release or installer was published.

## Code review and additional protocol/state fuzzing on October 14

This follow-up reviewed native Capture audio liveness, show import validation, local control handling and publishing failure paths in both apps. It used rebuilt complete portable packages, disposable Studio shows, and private localhost receivers. Media runs, builds and decoding were serialized. Compilation and syntax checks were gates; the results below came from running the packaged applications.

### Confirmed findings and fixes

- **P1, Studio show replacement:** Qt's `toArray()` converted malformed `sources` values such as `null` into an empty list. An otherwise valid file was accepted and replaced the current show, potentially persisting the empty result. A packaged baseline reproduced HTTP 200 for this invalid import. Loading now requires an object root and actual source/scene arrays; a present scene `layers` field must also be an array. Validation runs before replacing the current show. Omitted layers remain supported.
- **P2, misleading Capture audio meters in both apps:** a stopped audio callback left its last nonzero RMS/peak visible indefinitely even while the receiver heard silence. The native primary, additional and mixed meter readings now expire after 250 ms without valid PCM. Diagnostics and the UI use the same getters. Freshness timestamps reset on capture start/stop; invalid/empty primary PCM no longer marks that input active. This affects the native Capture path, not Studio's separate per-source mixer meters.

### Packaged results

- **Control HTTP/API fuzzing, both apps:** all 291 checks passed per app, including 256 seeded invalid commands, malformed authorization/framing, oversized input, fragmented Unicode, incomplete-connection deadlines, bounded connections, ignored pipelined commands and a burst of 128 concurrent valid/invalid requests. Health/schema still responded, normal quit removed discovery files, and saved settings were restored.
- **Studio stateful fuzzing:** two seeds each rejected 51 malformed imports without changing the current show, then rejected 180 invalid commands while accepting 360 scene/layer edits during simultaneous local recording and software UDP publishing. Inputs included wrong JSON types, excessive nesting/size, duplicate scene IDs, missing sources, invalid audio/geometry ranges, nonfinite JSON numbers and blocked browser URL schemes. The final seed also saved and reopened a valid show, verified its configuration and waited for video/audio to return. The healthy media worker did not restart. In the final run, API maximum was 25.6 ms and recording drops stayed 10 to 10. Both complete recordings decoded cleanly; minimum 10 ms audio RMS was 0.0257 locally and 0.0259 at the receiver. Program and WHIP receiver screenshots were inspected.
- **Repeated native audio interruptions, both apps:** eight pauses of the owned capture thread (120, 220, 260, 550, 1300, 5000, 300 and 2000 ms) left video publishing live with zero reconnects. Samples during the longer stalls verified that primary/mixed meters cleared; meters and expected 440/880 Hz stereo tones returned afterward. Full received files decoded cleanly. Game delivered 1,200 video frames and Studio 1,199, with 34 ms p95/maximum frame spacing. The longest silent intervals were 4.93 and 4.96 seconds.
- **WHIP/RTMPS negotiation, both apps:** all 11 cases passed: authenticated browser playback, cross-origin redirect credential handling, two ICE Link formats verified by STUN traffic, authentication rejection, malformed/oversized SDP cleanup, untrusted TLS rejection, cancellation during a pending POST, RTMPS certificate rejection and relay-only failure without TURN. Synthetic credentials were checked against diagnostics/logs. The harness now reads the fixture's actual window title (fixing a stale Studio title) and accepts an explicit MediaMTX path.

Reports are under `native-qt/e2e/reports/review-fuzz-2026-10-14/`: `control-game/` or `control-studio/`, `audio-interruptions-game/audio-stall.json` or `audio-interruptions-studio/audio-stall.json`, and `whip-http-game/results.json` or `whip-http-studio-final/results.json`. Studio also has `stateful-baseline/stateful-fuzz.json` (the reproduced import failure), `stateful-fixed/stateful-fuzz.json` and `stateful-final/stateful-fuzz.json` (passing seeds 2601015/2601016).

Reproduce with Game's `audio-stall-packaged.py --fuzz --publisher EXE --report REPORT`, each repo's `local-control-fuzz-packaged.py` and `whip-http-packaged.js`, and Studio's `mixer-stateful-fuzz-e2e.py --package PACKAGE --report REPORT --fixture MP4 --seed 2601016`. The stateful fixture is the 12-second 720p60/stereo file from `encoder-defaults-2026-10-14/verified-loop/source.mp4`. Studio's Node harness can use the sibling Game `node_modules` through `NODE_PATH` and `--mediamtx=PATH` when its own QA dependencies are absent.

Complete candidate packages:

- Game Capture: `native-qt/dist/game-capture-fuzz-review/`; executable SHA-256 `f2ff511576e907b2fa1791809ec1f1e19423e22f3ecd6f2dbe6666263eebf816`.
- Desktop Studio: `native-qt/dist/desktop-studio-fuzz-review/`; executable SHA-256 `81fa31565b89d5f5d482741611d745bc39807e892545d633fb48c61d94be3ce2`.

Limits: these are bounded, seeded workflows, not exhaustive proof of safety. This round did not repeat physical device unplug/replug, AMD/OBS, weak-machine/game-impact, overnight resource growth, trusted-HTTPS certificate cases or every protocol's loss/FEC matrix. Previously observed explicit NVENC pacing and isolated Studio local-recording gaps under large-source faults remain unresolved. Clean-machine testing remains deferred. No release or installer was published by this follow-up.

## Follow-up: recording cadence, file audio and image isolation

The new review build addresses the two remaining pacing findings above and additional problems exposed by the reruns:

- **External hardware encoder pacing:** after successful startup/warm-up, low-latency Annex B hardware encoding collects available packets without blocking the next input frame. This removes a wait that throttled NVENC below 60 fps. IVF's existing wait is retained to preserve AV1/VP9 delivery latency. Input/PTS queues remain bounded; software encoding, bitrate defaults and quality presets are unchanged. The same fix is in Game Capture.
- **Local recording cadence:** Program and source recorders now own their frame clock. A render stall repeats the last completed picture instead of creating a long timestamp gap. `recording.repeated_frames` exposes Program's repeats. `dropped_frames` still includes replaced pending compositions and send failures; constant file cadence is not proof that every picture is new.
- **Still-image decoding:** Qt image codecs run in disposable helper processes, outside the compositor/UI. At most two loads run concurrently, each with a 1 GiB job-memory limit and a 15-second decode deadline. Cancelled/replaced loads cannot install an obsolete picture. PNG transparency and EXIF auto-transform are preserved.
- **JPEG packaging:** the package had `qjpeg.dll` but lacked its `jpeg62.dll` dependency. Packaging now includes the runtime and its libjpeg-turbo copyright notice. No additional browser engine is bundled.
- **File-audio continuity:** isolated sources drain audio before copying a picture. Local files allow up to 30 ms of PCM read-ahead and prime the mixer with one 10 ms scheduling quantum. A bounded 30 ms PCM prefix (about 11 KiB) covers automatic loop seeking; the same samples are skipped when the decoder catches up. Manual seeking resets the skip count. Existing 200 ms audio-queue limits and user audio-delay controls remain. This priming applies to local files, not microphones, browser/guest audio or native capture.

### Large sources, CPU pressure and source failures

The final package recorded locally and published software UDP through 4K/8K/portrait/odd capture surfaces, 20 rapid resizes, an 8192x4096 PNG, a truncated PNG, **45 seconds restricted to two logical CPUs**, a killed window worker, a suspended audio worker and destruction of the captured window. The healthy media worker never restarted. Both complete files decoded cleanly:

| Measurement | Local MKV | Received MKV |
|---|---:|---:|
| Video frames | 4,322 | 4,312 |
| Average fps | 30.00 | 30.00 |
| Frame interval p95 / maximum | 34 / 34 ms | 34 / 34 ms |
| Minimum 10 ms audio RMS | 0.02715 | 0.02729 |
| Minimum 1 ms audio RMS | 0.02528 | 0.02530 |
| Unexpected low-energy 1 ms windows | 0 | 0 |

The original local-recording gaps reached 158-162 ms; none exceeded 34 ms in this run. API maximum was 89.0 ms during audio-worker recovery; adding the large PNG returned in 21.6 ms instead of the reproduced 131.6 ms synchronous decode. Recording drops stayed at nine from startup, with seven repeated compositions. Main-process private memory returned to about 40 MiB after removing the large/failed sources. These are measured bounded workflows, not a guarantee against arbitrary scheduling stalls.

The audio check was tightened during investigation: the first read-ahead change passed 10 ms RMS windows but still contained five quiet 1 ms windows at a loop boundary. Prefix caching removed that loop gap; file priming then removed separate two-millisecond scheduling gaps. The final result above uses the stricter one-millisecond check, without lowering its threshold. Evidence: `source-baseline/`, `source-final/`, `source-audio-read-ahead/`, `source-loop-prefix/`, and passing `source-primed-final/` plus `source-scoped-final/` under `native-qt/e2e/reports/pacing-fixes-2026-10-14/`.

Reproduce with `e2e/mixer-source-resilience-e2e.py --package PACKAGE --report REPORT --fixture MP4 --max-video-gap-ms 70 --cpu-seconds 45`. The fixture is `encoder-defaults-2026-10-14/verified-loop/source.mp4`, a 12-second 720p60 file with continuous 440/880 Hz stereo tones. Candidate: `native-qt/dist/desktop-studio-pacing-review/`; executable SHA-256 `0b302aff4826b5009f69109915ec78daed04b9788b607ee020e341d558067634`.

### Combined browser/publisher failures and synchronization

The same final executable passed the complete combined-fault sequence: browser CPU overload, browser termination and suspension, 35 seconds of network starvation without closing the socket, bandwidth restoration, publisher termination, and simultaneous browser/publisher failure. Healthy source restarts stayed at zero, browser audio returned, and the failure log stayed bounded at five worker-failure records. The 6,693-frame local recording decoded cleanly at 30.00 fps, with 34 ms p95/maximum spacing and seven duplicate/skip pairs; its longest repeated picture was 34 ms. Measured local A/V offset stayed within 29 ms. Receiver clips remained decodable and moving after recovery; measured receiver A/V offsets ranged from 46.5 to 56.2 ms across the four clips. The latter is not a promise of sub-frame sync across publisher restarts. Recovered pictures were approximately 0.23 seconds behind the live source. API p95/maximum were 25.6/33.8 ms.

Evidence: `fault-quality-scoped-final/fault-quality.json`, preserved recordings and worker logs under the same reports directory. `mixer-fault-quality-e2e.py` matches frequency-coded audio pulses to visible frame IDs, preserves media timestamps and analyzes the complete recordings after the app exits. Earlier complete passes are retained as `fault-quality-release-candidate/` and `fault-quality-verified/`. Measured offsets varied between starts; these runs did not show accumulating drift across loops.

### Images, unusual audio and visible controls

The final executable also passed these independent packaged workflows:

- **Images:** loaded transparent and half-transparent PNGs, JPEG and an 8192x4096 PNG; checked actual Program pixels, including the expected half-alpha blend. Corrupt PNG and unsupported SVG reported source errors. Replacing a pending image selected the new picture, and twelve rapid add/remove operations did not resurrect sources. Terminating or suspending an image helper during startup left recording, publishing and the healthy media worker running; failed helpers were cleaned up. Both complete recordings decoded cleanly with continuous audio. The current package does not include the SVG plugin. Evidence: `image-isolation-scoped-final/image-isolation.json`.
- **Audio formats and bad inputs:** repeated ten format cases and nine invalid/missing-input cases while recording Program and a separate source track. The cases cover 8-192 kHz, integer PCM 8/16/24/32, float32, mono/stereo/3/6/8 channels and nonfinite float samples. Unsupported float64 was rejected; it is not counted as supported. All resulting recordings decoded cleanly and the healthy reference track continued. Evidence: `audio-formats-scoped-final/audio-formats.json`.
- **Playback in the visible app:** drove pause/seek through the local API at 3.0 and 5.5 seconds, verified paused position, reached EOF, restarted, and looped from a cue at the file's end. Compared the captured pictures. This checks the running playback behavior; it does not exercise every playback-dialog control with mouse input. Evidence: `visible-transport-scoped-final/results.json`.
- **Visible output controls:** started/stopped publishing through UI Automation during simultaneous local recording, then reviewed the single-canvas interface screenshots. Track/master VU meters and bordered scroll areas remained visible. Both CPU and GPU composition used hardware NVENC, with clean complete local/received recordings and continuous stereo tones.

The last comparison used 1080p60, the same moving 720p60 file, approximately 4 Mbps publishing, simultaneous recording, and one 30-second run per renderer:

| Measurement | CPU composition | GPU composition |
|---|---:|---:|
| Received distinct frames/second, including startup | 59.83 | 59.83 |
| Received frame interval p95 | 17 ms | 17 ms |
| Process-tree CPU, whole 20-logical-CPU machine | 11.24% | 10.69% |
| Sum of process working sets | 777 MiB | 861 MiB |
| API response p95 | 30.0 ms | 26.6 ms |

Evidence: `visible-nvenc-scoped-final/results.json` and `visible-nvenc-gpu-final/results.json`. GPU composition saved about half a percentage point of whole-machine CPU in this pair of runs, while using more memory. This is not enough evidence to change the default. The workload still occupied roughly two logical CPUs; the pacing fixes are not a claim that high-resolution mixing plus recording is cheap. Summed working sets can count shared pages more than once. Reproduce using `mixer-encoder-defaults-e2e.py --package PACKAGE --report REPORT --fixture MP4 --case nvenc-1080p60-record --renderer cpu` (or `gpu`); use `--transport-only` for the playback workflow.

### Final scope and remaining coverage

`scoped-final-suite.json` records successful serialized runs of the Game encoder matrix and analysis, Studio large-source faults, combined browser/publisher faults, audio formats, visible output/playback controls and image isolation. The GPU comparison and explicit Game codec validation followed separately, without concurrent builds or decoding. Build, Python/Node syntax and PowerShell parsing checks are gates rather than substitutes for these workflows.

These are bounded runs on this host, not exhaustive proof against every failure. AMD/OBS, physical GPU removal, real older/slow-PC gameplay, overnight resource growth, every protocol's loss/FEC matrix and calibrated Game Capture A/V sync were not repeated in this follow-up. Two-CPU affinity is not a substitute for an older machine. Clean-machine testing remains deferred. Receiver A/V offset after publisher restarts can exceed one 30 fps frame as quantified above. Review packages remain local; no installer or GitHub release was published.

## AMD follow-up integration

The latest Game Capture remote work was reviewed on this Intel/TITAN RTX host. Studio had no new remote commits and was missing three related AMD policies: preserving live H.264 SPS/PPS without stale `dump_extra` headers, forcing periodic true IDRs at the requested GOP interval, and using AMF's own HEVC AUDs without a metadata filter that can reject incomplete initialization extradata. All three are now ported; Studio's separate encoder process and optimized pixel conversion remain intact. Actual AMD decoding/playback evidence belongs to Game Capture's earlier Radeon 780M runs, not this Studio build.

Process-loopback audio also gains the default-render-rate hint and opt-in timing/energy trace. Missing/invalid device information uses 48 kHz. A rejected alternate format receives one 48 kHz retry on a fresh COM audio client; device invalidation and unrelated failures do not trigger that retry. The selected process tree is preserved. Studio retains its worker isolation, COM runtime handling, and capture-device supervision. Mixed/output PCM remains 48 kHz. No endpoint settings or timer policies are changed.

The AMD host's kernel crashes, periodic input silence and strict receiver A/V-sync failures remain unqualified. The format preference is a compatibility improvement; the original cause has not been established, and no global sync offset was added to disguise differing receiver behavior.

### Packaged verification on this host

- **Eight native capture-audio scenarios:** 90 seconds each at 44.1 and 48 kHz, rejected 44.1 kHz followed by successful 48 kHz, missing default-render metadata, zero/out-of-range hints, rejection of both attempts, and device invalidation at initialization. The last two preserved live video with silent audio; all cases had zero publishing reconnects. Audible cases had no unexpected quiet 10 ms windows. Both long runs decoded completely with correct stereo tones; minimum 10 ms RMS was 0.02242 / 0.02706. All eight files had increasing audio/video timestamps, maximum audio spacing 21.334 ms and maximum video spacing 38 ms. Separate decoder passes avoid mistaking interleaved FFmpeg log lines for missing frames.
- **Explicit AMD unavailable:** actual H.264 and HEVC launch attempts exited normally with a clear requested-encoder error and cleaned up their child processes. Observed FFmpeg arguments verified H.264 header/IDR policy and the HEVC filter removal. No AMD hardware encoding occurred.
- **Visible NVENC workflow:** used the interface's Start/Stop output control while independently recording 1080p60. Complete local and received files decoded cleanly with continuous stereo audio. Received distinct cadence was 59.83 fps including startup, p95 spacing 17 ms, API p95 24.9 ms. Process-tree CPU averaged 11.41% of this 20-logical-CPU host and summed working sets 772 MiB. This is a regression result, not a claimed CPU reduction. Reviewed the single-canvas screenshot and active track/master VU meters.
- **Mixer/source faults:** simultaneous recording and software publishing survived 4K/8K/portrait/odd capture surfaces, resize storms, large/corrupt images, 45 seconds on two logical CPUs, window-worker termination, audio-worker suspension and source-window disappearance. Healthy media never restarted. Local/received files contained 4,345 / 4,329 frames near 30 fps; maximum spacing was 34 / 37 ms. No unexpected quiet 1 ms windows were found; minimum 1 ms RMS was 0.02551 / 0.02481. Maximum API response was 28.8 ms.

The rate scenarios use real Windows capture/encoding with Frida altering only the owned app's rate hint or selected initialization result. The physical endpoint remained 48 kHz; this does not reproduce the AMD host's physical 44.1 kHz device or a live endpoint switch. No user audio settings changed. The native Capture path supplies the rate cases; the final mixer run separately exercises isolated window/audio workers.

Evidence: `native-qt/e2e/reports/amd-completion-2026-10-14/`, including `audio-quality-summary.json`, `amd-unavailable/`, `amd-hevc-unavailable/`, `visible-nvenc/` and `source-faults/`. Game Capture's `e2e/process-audio-format-packaged.py` and `amd-unavailable-packaged.py` accept this Studio executable; existing Studio encoder/source drivers reproduce the mixer cases. The Game checkout also validates delayed private RTMP/HLS audio/video receiver retries and cancellation using the improved public-output runner; those are not tests of the actual public service.

Candidate: `native-qt/dist/desktop-studio-amd-completion-review/`; executable SHA-256 `0b5cfe34634213f47526244290c711bae302b3fb28d40343a6b702ecc0ea81b8`. Release compilation and syntax/whitespace checks passed as gates. No installer or release was published. Actual AMD encoding/OBS compatibility and the unstable AMD host's crash diagnosis remain hardware follow-ups.

## Additional audio-rate fuzzing

The next packaged run exposed a reproducible process-capture problem at a 22.05 kHz rate hint in both apps. Windows returned 221-frame buffers every 10 ms, with zero-energy input at 8.77 and 17.63 seconds. Both 20-second received files contained two unexpectedly quiet 10 ms analysis windows. The bounded capture trace sees those zeros before the app's resampler, so this was not hidden by adjusting timestamps or fabricating replacement sound. The physical endpoint stayed at 48 kHz; the test changes only the owned application's format hint.

The virtual process-loopback fallback now uses 48 kHz when the hinted rate cannot represent a whole number of frames in the requested 10 ms period. This covers 22.05 and 11.025 kHz while preserving 44.1, 48, 96 and 192 kHz preferences. It applies only when the virtual client's format is unavailable; actual endpoint/microphone formats continue through their existing converter. No Windows setting changes. This avoids the reproduced condition on this host; it does not establish the cause of the separate AMD host's approximately 26-second dropouts.

The audio fault driver now also injects `E_INVALIDARG` to exercise that fresh-client 48 kHz retry, and retains measured quiet-window evidence if output validation fails. Initial failures and traces remain in `native-qt/e2e/reports/amd-followup-2026-10-14/audio-22050-none/`. The initial six-case-per-app suite passed the other ten cases, including 8, 96, 192 and 384 kHz capture and the injected format rejection.

### Five-minute software-publishing contention run

Before the fractional-rate fix, the complete `desktop-studio-amd-completion-review` package ran a visible 720p30 software stream at the default 4,000 kbps while independently recording a looping 60 fps media source. Fourteen synthetic workers added CPU contention for 90 seconds midway through the five-minute stream. Actual whole-machine CPU averaged 80.2% during contention. Start/Stop publishing used the interface; the app's own screenshot API captured the live interface, active source/master VU meters, bounded panels and total-system CPU display.

Received video contained 30.000 distinct frames/second with 34 ms p95 frame spacing. Both full recordings strictly decoded, with no unexpected quiet 10 ms audio windows (minimum RMS 0.02672 local / 0.02679 received). Publishing reported zero dropped frames, failures and reconnects. The recording queue counter began at 10 startup drops and finished at 13; three additional recording drops were counted during contention.

The complete app process tree averaged 4.81% of this 20-logical-processor machine, with API p95 26.4 ms and a 522 MiB mean summed working set. The stable post-load working-set average was about 9.4 MiB above the pre-load window; handles did not grow. A five-minute run cannot rule out an overnight leak, and summed working sets can count shared pages more than once. Recording retained its normal automatic encoder selection; software publishing was verified as `H264 Encoder MFT`. Synthetic contention does not reproduce a particular game or older computer.

Evidence: `native-qt/e2e/reports/amd-followup-soak-2026-10-14/` (`results.json`, `resource-phases.json`, `under-load.png`, and both MKVs). Reproduce with `mixer-encoder-defaults-e2e.py --package PACKAGE --report REPORT --fixture MP4 --soak-seconds 300 --load-workers 14`. This performance evidence belongs to the pre-rate-fix executable hash already listed above; the fresh audio-rate packages receive separate verification below.

### Rebuilt-package audio verification

Both rebuilt packages passed nine scenarios each: 90 seconds with the previously failing 22.05 kHz hint, plus 20 seconds each at 11.025, 8, 44.1, 48, 96, 192 and 384 kHz, and injected `E_INVALIDARG` at 44.1 kHz. The fractional-period hints selected 48 kHz directly; the injected error caused exactly one fresh-client retry at 48 kHz. All other valid rates were captured as requested. All eighteen received files strictly decoded with the expected left/right tones, no unexpected quiet 10 ms windows, no steady-state zero-energy input buffers, increasing audio/video timestamps and zero publishing reconnects. Continuity measurements exclude the first and last second. Maximum video spacing was 39 ms in Game Capture and 38 ms in Studio; maximum AAC audio-frame spacing was 21.334 ms in both. These are cadence/continuity measurements, not calibrated source-to-receiver A/V sync.

Evidence: `native-qt/e2e/reports/audio-rate-final-2026-10-14/audio-quality-summary.json` and its nine `audio-*/` directories. The Studio tests here exercise native capture through its packaged headless entry point; mixer/browser workflows are reported separately. The physical render endpoint remained at 48 kHz throughout.

Reproduce using Game Capture's `e2e/process-audio-format-packaged.py --publisher PACKAGE/desktop-studio.exe --report REPORT --rate 22050 --seconds 90`; use `--rate 44100 --fault invalid-rate --seconds 20` for the injected initialization rejection. The reusable fault hook and output quality checks run against either packaged executable.

Fresh local package: `native-qt/dist/desktop-studio-audio-rate-review/`; executable SHA-256 `b45caa92c2f3a533ef2e29b364c26ad3323e6b8844f48a11fe6ec1576d7c9dbf`. Release builds and parsing/whitespace checks passed as gates. The prior review packages and their evidence remain preserved. No installer or GitHub release was produced by this follow-up.

### Private protocol recovery and retained WHIP failures

The rebuilt packages each published real 720p30 software-encoded H.264 with stereo audio to private MediaMTX receivers over RTMP, SRT, RTSP/TCP (`rtspt://`) and WHIP. RTMP and SRT each included a three-second server outage and automatic recovery. Each app produced five files passing strict decoding, picture identity/cadence and stereo-continuity checks across RTMP/SRT/RTSP: RTMP initial/recovered, SRT initial/recovered and RTSP steady. Distinct cadence was approximately 30 fps, p95/max frame spacing 34 ms, with no unexpected low-energy audio windows. Shutdown was normal in every case.

| Recovery after private server restart | Game Capture | Desktop Studio |
| --- | ---: | ---: |
| RTMP | 4.370 s | 1.046 s |
| SRT | 3.792 s | 3.948 s |

These times start after the deliberately stopped server is relaunched; they exclude the three-second outage and are not end-to-end playout latency. Software capture/publishing process trees averaged 1.43-1.70% of the 20-logical-processor host. Clean files averaged 3.74-3.81 Mbps of video. A one-second video peak reached 5.10 Mbps, so the 4 Mbps target is not a strict instantaneous upload ceiling; audio and transport overhead are additional.

Game Capture's WHIP file also passed, with one 52 ms video interval near startup. Studio's WHIP-to-MediaMTX-to-FFmpeg file **failed** because three initial video timestamps were duplicated. The complete protocol suite remains marked failed rather than treating a live connection as a quality pass. Three further traced Studio starts reproduced duplicate recorded timestamps in two files (one duplicate each). Across all three, incoming RTSP video frame-marker timestamps advanced by exactly 3,000 ticks at 90 kHz; all six audio/video RTP streams had zero duplicate/backward marker timestamps and zero parser errors. Maximum change in sender-report clock mapping was 0.501 ms. This localizes the observed timestamp clamping to receiver/remux timing, consistent with the earlier documented limitation, rather than a publisher RTP timestamp rewind. The tracer now counts frame markers explicitly so distinct frames sharing a timestamp cannot disappear from its timestamp-change log.

Evidence: both checkouts' `native-qt/e2e/reports/audio-rate-final-2026-10-14/protocols/`; combined table in Game's `protocol-summary.json`; Studio's `whip-startup-trace/` retains the failed recordings, `analysis.json`, `rtsp-timestamps.jsonl`, `rtsp-trace-summary.json` and `clock-evidence.json`. Reproduce with Game Capture's `external-output-packaged.js --publisher=EXE --output=REPORT --matrix=e2e/external-recovery-matrix.json --motion=true --fullscreen=true --sample-ms=1000`, followed by `summarize-encoder-defaults.py` and `analyze-external-output.py` after publishing ends. Add `--rtsp-trace=true` to inspect the MediaMTX-to-FFmpeg RTP/RTCP stream. These native capture entry-point runs do not imply every Studio interface workflow or public service was exercised.

### Independent WHIP loss/jitter cross-check

The fresh Studio package also ran its actual mixer and independent recording against the private aiortc receiver, which retains assembled H.264 before decoding. Baseline and 1% packet loss alone delivered 29.98 fps with no non-increasing timestamps; maximum loss-only gap was 66.7 ms. **Combined 1% loss and up to 25 ms random packet delay failed:** 27.70 fps and a 966.7 ms video gap. Restored reception returned to 30.13 fps with 33.3 ms maximum spacing. The complete retained H.264 bitstream decoded cleanly; this failure was lost/delayed pictures rather than concealed corrupt bitstream being accepted as clean. The stricter combined-impairment quality result remains open; no sender-clock change or relaxed threshold was used to make it pass.

The app and local recording stayed healthy through that failure. Recording decoded 2,276 frames at exactly 30 fps, maximum interval 34 ms and maximum measured A/V offset 20 ms. Recording drops stayed 10 to 10; API maximum was 24.95 ms; process-tree private memory ranged from 383 to 396 MiB. Evidence: Studio's `whip-independent/udp-fault.json`, `summary.json`, raw receiver H.264/PCM and `independent-recording.mkv`. Reproduce with `STUDIO_CLOCK_FIXTURE` pointing at the frequency/frame-identity fixture and `mixer-whip-reference-e2e.py --package PACKAGE --report REPORT`. This receiver result does not certify other services or an arbitrary WAN.

### Browser and recording isolation closeout

The rebuilt Studio package passed the browser-audio suspension workflow: after 15 seconds of deliberate Web Audio suspension, the same browser worker kept rendering and resumed captured audio without reconnecting. Its full recording decoded cleanly, including the intended silence. It also passed four simultaneous source recordings while exercising off-screen sources, mute/on-air controls, private break and source removal. Terminating the real Program FFmpeg writer stopped that recording independently; all four source files continued growing and finalized with clean decodes. Removing a source correctly left its recorded tail black and silent. The user's saved show was unchanged.

Evidence: Studio's `native-qt/e2e/reports/audio-rate-final-2026-10-14/browser-idle/`, `recording-isolation/` and `mixer-final-suite.json`. The five-minute contention files were also checked separately for cadence: local/received had 9,057 / 8,999 frames at 30 fps, increasing timestamps, maximum spacing 34 ms, and zero intervals over 50 ms (`amd-followup-soak-2026-10-14/file-cadence.json`). All media runs, builds and post-run decoding were serialized; no owned app/media workers remained after cleanup.

Remaining qualifications: the retained WHIP receiver/remux startup timestamp failures and combined-loss/jitter freeze; physical AMD encoding/OBS behavior and crash diagnosis; actual endpoint rate/device switches; overnight resource stability and the deferred clean-machine run. The audio-rate fix is verified on the available Intel/NVIDIA host and does not certify those remaining cases.

### WHIP repair follow-up (2026-10-14)

The sender's fixed one-second repair budget could run out after a burst of
reordering-induced NACKs, refusing subsequent genuine-loss repairs until the
next window. Both apps now replenish that budget continuously. Its average
allowance, burst ceiling, cache limits and 50 ms duplicate-request suppression
remain bounded. No extra video FEC or guaranteed loss-free delivery is claimed.
Game Capture also receives Studio's negotiated RTX/NACK/PLI and video pacing
implementation, enabled only for native WHIP; VDO.Ninja transport is unchanged.
Game Capture diagnostics now report the negotiated recovery capabilities,
retransmitted packet count and repair cache misses.

Instrumentation observes aiortc's unchanged jitter-buffer behavior and NACKs.
The old Studio package produced 44 buffer resets, one non-increasing decoded
timestamp and a failed quality result in `whip-fixes-2026-10-14/observed-baseline`.
With continuous replenishment, two complete mixer/recording/publishing runs
passed: combined 1% loss plus random 0–25 ms packet delay/reordering delivered
29.97/29.98 fps, maximum video PTS spacing 33.4 ms, zero buffer resets, strictly
increasing timestamps and clean full raw H.264 decode. The receiver does not
negotiate RED or the playout-delay hint. Independent recordings stayed at 30 fps,
maximum spacing 34 ms, measured maximum A/V offsets 18/28 ms, and unchanged
recording drop counters. API maxima were 26.5/28.4 ms. These are repeat
observations under actual scheduling, not an identical packet-loss replay.

Game Capture's packaged software encoder also passed real window/process-audio
capture through the same receiver: 10 seconds clean, 20 seconds at 1% loss,
60 seconds of combined loss/jitter, and 10 seconds restored. The impaired phase
delivered 29.97 fps, maximum spacing 37.7 ms, no buffer resets or non-increasing
PTS, audible audio and clean full H.264 decode. No reconnects or cache misses;
maximum diagnostics request time 27.4 ms. Evidence:
`game-capture/native-qt/e2e/reports/whip-fixes-2026-10-14/software/results.json`.
Reproduction harness: `e2e/whip-recovery-packaged.py`, with `--receiver-root`
pointing at Desktop Studio's `native-qt` directory containing the private
aiortc dependencies, and `--fixture` pointing at the frame-clock MP4.

**Chromium qualification remains open.** The same Studio candidate was tested
with combined A/V playback and negotiated RED both on and off. Ordinary Opus
passed (30.03 impaired displayed fps, 67 ms maximum gap). RED enabled delivered
29.20 fps but had a 335 ms gap, failing the unchanged <250 ms gate. Sender RTP
continued through that interval. The independent local recording stayed clean
at 30 fps with no new recording drops and maximum measured A/V offset 13 ms.
Retain `browser-continuous/udp-fault.json` as failed; the independent decoder
passes do not certify every browser/service/link.

### MediaMTX-to-FFmpeg startup: verified receiver workaround

The new trace retains RTSP `RTP-Info` origins alongside RTP frame markers and
RTCP sender reports; FFmpeg `-debug_ts` retains its demuxer timeline. In the
unmodified MediaMTX 1.21.2 path, an on-demand RTSP stream can return an initialized
video origin and no audio origin. FFmpeg's shared RTCP clock initialization
then replaces that partial mapping. One of three new ordinary starts had a
73.155 ms backward demuxer step when the initial video sender report arrived,
despite monotonic input RTP. See `startup-debug/`.

An explicit private-receiver warm-up (RTSP DESCRIBE, wait 500 ms, then start
the recorder) lets MediaMTX observe both tracks before its PLAY response.
Six starts then had complete RTP-Info, strictly increasing recorded timestamps,
30 fps and maximum frame spacing 34 ms; all six full files decoded cleanly.
No media timestamps were regenerated, rewritten or filtered. The normal
unwarmed path is still covered and still fails intermittently: this is a
receiver workaround, not a publisher timestamp fix.

Use Game Capture's existing external-output harness with
`--rtsp-trace=true --receiver-debug=true --rtsp-warmup-ms=500` to reproduce.
The warm-up is opt-in and restricted to the private loopback fixture.
Evidence: Studio's `whip-fixes-2026-10-14/startup-warm/timestamp-verification.json`.
Five clips also passed the complete pulse/audio/cadence analysis; the sixth
15-second clip had too few matching fixture A/V pulses, so the aggregate
`analysis.json` remains failed. Its separate timestamp/full-decode check passed.
Do not represent the six timestamp checks as six complete A/V quality passes.

Relevant implementation references:
[FFmpeg RTP clock mapping](https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavformat/rtpdec.c)
and [RTSP shared sender-report origin](https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavformat/rtsp.c).

### Longer Chromium comparison with matched video-repair settings

The unchanged WHIP candidate above was tested again with combined browser A/V
playback: 10 seconds clean, 60 seconds of 1% packet loss plus 0-25 ms random
delay/reordering, and 10 seconds restored, for both RED on and off. Both cases
used 1,500 ms repair history and the default 500 ms playout hint. The relay RNG
was reset to seed 72941 for each case; this makes the fixture reproducible,
not packet-for-packet identical across different audio packetizations.

**Both cases failed the existing <250 ms maximum video-gap requirement.** RED
on averaged 29.48 impaired displayed fps with a 366 ms gap; RED off averaged
29.67 fps with a 334 ms gap, plus a 283 ms gap during the restored phase. Audio
remained audible, video timestamps remained increasing, RTX was active and
repair-cache misses were zero. Turning RED off is therefore not an established
workaround. The previous shorter RED-off pass remains valid for that run only.

The private browser receiver now logs bounded playback events and 250 ms
WebRTC statistics. During the pauses, incoming video-frame counters continued
while decoded-frame counters temporarily stopped. This locates the symptom
in the receiving video path but does not prove its root cause or exclude
sender repair timing. No production defaults or timestamp policies changed.

Independent recording stayed clean: 5,145 frames at 30.00 fps, maximum interval
34 ms, no repeated/skipped source pictures and maximum measured A/V offset
15 ms. Recording drops stayed 12 -> 12. Maximum API time was 31.2 ms; app-tree
private memory ranged from 383.0 to 397.1 MiB over the run.

Evidence: `native-qt/e2e/reports/followup-2026-10-14/browser-matched-history/`,
including failed `udp-fault.json` and `gap-diagnostics.json`. Reproduce with
`mixer-whip-browser-recovery-e2e.py`, `STUDIO_BROWSER_COMBINED=1`,
`STUDIO_BROWSER_IMPAIRED_SECONDS=60`, `STUDIO_WHIP_HISTORY_MS=1500`,
`STUDIO_BROWSER_SEED=72941`, and the existing `STUDIO_CLOCK_FIXTURE`.

A separate RED-enabled diagnostic run increased only the advanced playout hint
to 750 ms (`STUDIO_WHIP_PLAYOUT_MS=750`). Negotiation confirmed 750 ms, but it
still failed: 29.76 impaired displayed fps and a 300 ms gap, zero cache misses,
increasing timestamps and audible audio. The restored phase returned to
30.00 fps with maximum gap 53 ms. This does not establish a buffer-setting
workaround; the application's 500 ms default remains unchanged. Its independent
recording decoded cleanly at 30 fps, maximum measured A/V offset 28 ms, with
two repeated/skipped source pictures and maximum repeated-picture interval
34 ms. Evidence: `followup-2026-10-14/browser-playout-750/udp-fault.json`.

### Repeated browser, publisher and network fault isolation

The same packaged Studio passed `mixer-fault-quality-e2e.py` with local recording,
named source-audio tracks and real RTMP reception running together. Faults were
24 busy browser JavaScript workers, browser-host termination, a suspended
browser host, 35 seconds of upstream bandwidth starvation, publisher-worker
termination, and simultaneous browser termination/publisher suspension.
Bandwidth was restored without the fixture resetting the TCP connection.

The healthy media worker never restarted. Browser audio was present in the
actual recorded track both before and after recovery. All four received clips
decoded cleanly at 30 fps, maximum frame interval 34 ms, no duplicate/skipped
source pictures, and maximum measured A/V offsets of 69.2, 28.2, 30.0 and
31.2 ms respectively. Restored receiver-picture age was approximately
0.20-0.23 seconds, without persistent accumulated network delay.

The complete 6,719-frame local recording and named source audio decoded cleanly
at 30 fps with maximum measured A/V offset 24 ms. Heavy browser CPU pressure
caused **two additional dropped source pictures**, replaced by repeated pictures
to preserve the recording timeline (recording drops 9 -> 11). The longest
repeated-picture interval was 33 ms. Other fault phases added no recording
drops. Maximum control API time was 31.4 ms. App-tree private memory peaked
at 928.9 MiB during browser stress and ended at 647.5 MiB, versus 633.3 MiB at
the first sample; this short run does not establish long-term leak freedom.
Five bounded worker-failure log entries identified the injected failures.

Evidence: `native-qt/e2e/reports/followup-2026-10-14/fault-quality/`.
The harness can now reuse `STUDIO_CLOCK_FIXTURE` instead of regenerating the
same media, and all decoding was performed after capture/publishing stopped.

### Full application crash, diagnostics and logging failures

`mixer-diagnostics-fault-e2e.py` injected an actual unhandled access violation
into its own packaged Studio process while recording. The interrupted MKV
decoded cleanly with continuous audible audio; all owned child processes exited.
The crash handler wrote the expected `0xC0000005` report, minidump and preserved
log snapshot. Relaunch left outputs off and produced another clean recording.

Five subsequent launches verified bounded log rotation and retention of the
crash snapshot. A synthetic private stream key was absent from the generated
issue report and logs. Finally, a file deliberately blocking creation of the
private log directory did not prevent launching or making a clean recording.
The test used a private data directory and left the user's show unchanged.
Evidence: `native-qt/e2e/reports/followup-2026-10-14/crash-diagnostics/`.
