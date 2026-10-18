# Desktop Studio 0.9.3 Windows qualification

Fresh settings select 1280 x 720 at 30 fps, 3,000 kbps video, 128 kbps
streaming audio, Auto hardware-first H.264, and no microphone. Saved choices
are preserved. Independent recording retains its separate higher quality
budget. The API and interface now agree on the 200 ms SRT default.
See the [encoder guide](https://steveseguin.github.io/desktop-studio-mixer/docs/encoder-guide/) and [default policy](streaming-defaults.md).

## Frozen package

Built from `74cc6a4b8eb17ed8d2c8b1b39178e329a4f42ee5`. Subsequent QA and
documentation edits do not change the signed application.

| Artifact | SHA-256 |
| --- | --- |
| Signed application | `4873f5feeb76451ce233c0950a96a3924efbb5418a733dead2bf80b98eac6a77` |
| Setup installer | `28dc596fd23759e319310ad110f8090af6750f1183100009ddf47fb898e296b2` |
| Portable ZIP | `519b071f296c053d48328e75d66ce9f5c7335f1a9cc7de39c82984c495b2e821` |

All 3,773 ZIP files match the frozen staging directory. The executable
extracted from setup matches the signed portable application. Stable aliases
are byte-identical to their versioned assets. Portable packaging now uses an
allowlist that excludes developer test executables and Qt6Test.dll. Checksums
and FFmpeg license/source information accompany the release.

## Host and scope

Windows 11 Pro 10.0.26200; Intel Core Ultra 7 265K, 20 logical CPUs and
63.5 GiB RAM. GPUs: RX 9060 XT (32.0.22042.14002), TITAN RTX
(32.0.16.1047), and Intel Graphics (32.0.101.6881). Work uses CPU affinity
0-9. Media workflows are serialized. User preferences are restored afterward.

The [earlier defaults measurements](defaults-validation-2026-10-18.md) retain
their original candidate hashes and scope. They are not measurements of this
new signed package.

## Packaged application workflows

Codec selection through the actual Studio interface passed at 720p30 with
an explicit 4 Mbps video setting and 128 kbps stereo Opus. These are codec
compatibility workflows, separate from the new 3 Mbps default-output checks.

| Codec | Observed encoder | Playback / reload / transport restart | Independent recording |
| --- | --- | --- | --- |
| H.264 | AMD `h264_amf` | Passed | Clean decode, moving video and stereo tone |
| AV1 | AMD `av1_amf` | Passed | Clean decode, moving video and stereo tone |
| VP9 | Software `libvpx-vp9` | Passed | Clean decode, moving video and stereo tone |

The nine browser observation windows measured 29.96-29.99 fps, with zero
reported dropped frames or freezes. Browser audio had nonzero measured signal,
and decoded recordings also contained the tone. The live Studio image was
inspected, including the source/master VU meters.

Fault-isolation workflows passed with real AMD RTMP publishing and independent
recording: AMF child termination, browser CPU pressure, browser crash/hang,
network starvation/recovery, publishing-worker termination, and simultaneous
browser/publisher failure. Expected worker failures were logged; unrelated
healthy workers did not restart. Browser audio was present before and after
recovery, and recovered receiver captures showed current moving video.

| Observation during the fault sequence | Result |
| --- | --- |
| API response time | 25.6 ms p95; 85.6 ms maximum |
| Local recording decode | Clean; 7,729 frames, 29.89 fps average |
| Recording frame interval | 34 ms p95; 133 ms maximum |
| Identified audio/video pulse offset | 53 ms absolute p95; 97 ms maximum |
| Recorded picture continuity | 51 duplicate pictures; 79 skipped source pictures |
| Application recording counters at end | 64 queue drops; 26 repeated frames |
| Sampled aggregate private memory peak | 874 MiB |

This demonstrates survival and recovery, not lossless recording under every
fault. Picture identities, queue counters, and timeline intervals measure
different things. Pulse offsets are file/fixture observations, not physical
speaker/display latency. The browser-pressure phase occupied about four CPU
cores in WebView2 while the main application averaged about 0.19 core.

An isolated per-user installation passed installed CLI/MCP control, 12 distinct
live portrait screenshots, and recording decode with a 34 ms maximum frame
gap. Installed, ZIP and staged executable hashes matched. Uninstall removed
the owned installation, automation files, shortcuts and registration; no owned
descendants remained.

Fresh GUI defaults and saved-setting persistence passed. Chosen 60 fps,
6 Mbps video and 192 kbps audio settings survived restart; the user's original
settings were restored afterward. SRT configured through the API displayed the
new 200 ms default, and an explicit 450 ms override was preserved.

All six 720-class H.264 default-output workflows passed at 30 fps / 3 Mbps:
Auto with independent recording, software, NVIDIA NVENC, Intel Quick Sync,
AMD AMF with recording, and portrait AMD AMF with recording. All six received
files passed strict decode, Baseline/Constrained Baseline profile, zero B-frame,
increasing-timestamp, frame-rate tolerance and decoded-audio checks.

| Workflow | Received fps | Mean CPU cores | Mean RSS MiB | Strict output check |
| --- | ---: | ---: | ---: | --- |
| `amf-720p30-record` | 29.80 | 0.88 | 509 | Passed |
| `amf-portrait-720p30-record` | 29.80 | 0.95 | 514 | Passed |
| `auto-720p30-record` | 30.00 | 0.90 | 538 | Passed |
| `nvenc-720p30` | 29.90 | 0.57 | 461 | Passed |
| `qsv-720p30` | 29.80 | 0.63 | 432 | Passed |
| `software-720p30` | 30.00 | 0.67 | 316 | Passed |

The three recording cases already showed 9-10 recording queue drops in their
first sample; those counters did not increase over their observed windows.
This is not a zero-startup-drop claim. [Full numeric measurements](mixer-0.9.3-metrics.json)
include individual publisher GPU engines, whole-system CPU, memory, bitrate
and decoded media details. These short local workflows are not a repeated
encoder ranking; recording adds work, and GPU engines must remain separate.

The focused automation continuation passed windowless program/preview/window
screenshots, scenes/layers, media/audio controls, SRT with recording, browser
audio/transparency, browser-crash recovery, and all 40 MCP tools. It also
verified monitor-device selection, portrait screenshots, separate recordings,
five themes, podcast layout, Spanish UI labels and keyboard scene navigation.
Unauthenticated screenshot requests returned 401; a browser-origin request
even with the token returned 403. Invalid commands, file-browser sources and
screenshot overwrite attempts were rejected. These are specific access-control
checks, not a claim of exhaustive browser-engine security verification.

SRT and RTMP plus independent recording passed strict decode and tone checks in
the corrected external-output invocation. WHIP reached the receiver but its
RTSP-remuxed file retained three duplicate timestamps at 0.967 seconds, with
no backward timestamps. The independent recording was clean at 30 fps, with
no duplicate timestamps and a 34 ms maximum frame gap. The strict WHIP relay-file
failure remains a failure.

A separate real Studio mixed-program WHIP/WHEP workflow passed directly in
Chrome: 29.84 frame callbacks per second, 76 distinct sampled images, nonzero
browser audio, and a clean independent recording. It used the 3 Mbps default.
Audio was observed through an explicit browser gesture and a zero-gain audio
graph; physical speaker/display latency was not measured. The screenshot was
visually inspected. Direct browser success does not erase the RTSP-file limit.

### Retained fixture failure

The first automation invocation reached SRT output and then failed to start
RTMP because its local MediaMTX receiver exited: a port selected using a TCP
socket was forbidden for UDP on this Windows host. Studio remained responsive
and its recording remained active, but this failed attempt does not qualify
RTMP/WHIP playback or recording quality. The fixture now allocates its ICE
port with a UDP socket, disables its unused SRT listener, and reports an exited
server before publishing. The second invocation passed SRT/RTMP but stopped
at the known WHIP relay-file timestamp check. The third invocation deliberately
omitted the optional MediaMTX block and completed the remaining controls,
browser and accessibility workflows, while repeating SRT plus recording.
The direct WHIP/WHEP browser workflow was separate. All attempts are retained;
none of these fixture changes modified the signed application.

Evidence is under `native-qt/e2e/reports/release-0.9.3/`, including the original
automation attempt, `followup-r2/automation-core` and
`followup-r3/automation-core`. The independent browser result and default-output
measurements are in the research workspace's `private/app-releases-20261018/`.

## Limits

This mixed-GPU Windows host does not establish AMD-only, clean-machine,
HDR/10-bit, other-driver, demanding-game or overnight reliability. Existing
WHIP-to-RTSP startup timestamp duplication remains a receiver-path limitation;
browser playback and strict relay-file timestamps are checked separately.
AV1/HEVC compatibility depends on the receiver. WebView2 Evergreen remains a
separately installed system runtime.

Application/setup signatures identify Social Stream Ninja and are timestamped.
This host reports an untrusted certificate root; signing does not establish
SmartScreen reputation. Archive/signing/build checks are gates, separate from
application testing. Builds and qualification run locally; no GitHub Actions
build was requested.
