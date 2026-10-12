# Desktop Studio 0.6.2: browser resource limits and recovery

Packaged Release application workflows on Windows, 2026-10-11, with 20 logical
processors, 64 GiB RAM, Intel Graphics and NVIDIA TITAN RTX. Private received
media, source identifiers and raw diagnostics remain in ignored QA reports.

## Changes

- Each browser worker's Windows job now limits its entire process tree to at
  most 50% of machine CPU cycles, further capped at four logical CPUs. On this
  machine the configured rate is 20%. Committed memory is limited to one quarter
  of physical RAM, clamped between 1 and 4 GiB. These limits include Chromium
  renderer, Web Worker, GPU and audio processes. Initialization fails if the
  resource protections cannot be installed.
- Source health reports sustained CPU-limit pressure. A separate two-second
  JavaScript responsiveness probe detects a stalled renderer even when its
  native host heartbeat continues. A pending probe expires after ten seconds;
  native process-failure events and host heartbeat supervision remain active.
- Three automatic reconnects are allowed per source within a rolling minute.
  A further failure stops that source and displays a paused/reconnect message.
  Manual Reconnect or an explicit source configuration restart resets the budget.
- Browser audio and transparent background defaults remain enabled. No unsafe
  GPU flags, device permissions, host objects or browser security bypasses were
  added. Hardware decoding continues to be selected by WebView2 for compatible
  codec/driver combinations.
- A reproduced WebView2 decoder-reinitialization bug also required a WebRTC
  compatibility workaround. On Edge/WebView2 152-154, source pages suppress the
  RTP color-space extension before offer/answer negotiation, including implicit
  local-description generation. Other runtime majors retain normal behavior.
  **Preserve original video color metadata** opts an individual source out and
  is saved with the show. Codec preference order and ordinary HTML video/HEVC
  decoding are not changed. The restriction to 152-154 is based on the reported
  affected runtime family; the installed 154.0.4258.62 build was tested here.

The CPU and memory limits are **per source**, not aggregate limits across every
browser in a show. Windows schedules the CPU limit by cycles; process CPU-time
measurements are approximate rather than an exact reading of that quota.

## Actual application workflows

`mixer-browser-resilience-e2e.py` used two independent browser sources and recorded
Program while applying faults to one of them:

| Workflow | Observed result |
| --- | --- |
| H.264 video in a browser | Chromium reported `D3D11VideoDecoder` and `kIsPlatformVideoDecoder=true`; GPU composition and video decoding enabled. |
| Default browser audio/background | Audio reached visible track/master meters and the recording; HTML/body computed transparent without changing source defaults. |
| 24 busy Web Workers | About 4.01 logical CPUs of process time under the four-core target; CPU-limit indicator appeared. Main-window response p95 0.52 ms, maximum 1.53 ms. |
| Infinite renderer JavaScript loop | Source worker replaced and recovered in 14.51 seconds; the other source and recording survived. |
| Chromium GPU-process termination | Affected source recovered; the healthy browser worker and recording survived. |
| Four rapid host crashes | Three automatic reconnects, then the source paused. Manual Reconnect recovered it. |
| Attempted 5 GiB allocation | Allocation stopped with `RangeError`; sampled tree private memory peaked at 4060.78 MiB under the 4 GiB job limit. The healthy browser and recording survived. |
| Final recording | Fully decoded without errors. Audio RMS 0.1255; minimum complete one-second window 0.1228. Longest nearly identical video run was six frames, excluding the first/last second. Healthy browser worker PID stayed unchanged throughout. |

The CPU-pressure response measurements use a cached native main-window handle,
so they exclude UI Automation discovery time. The page under deliberate CPU
pressure lost frame rate; the limit protects the rest of the application rather
than promising smooth playback for an overloaded page.

`mixer-vdo-browser-e2e.py` opened the user-authorized live VDO.Ninja viewer through
the normal Add source dialog. Its default source settings were transparent,
unmuted and gain 1. Incoming video was VP8 at approximately 1280x555 / 1 fps;
an active Opus audio track was present. Recorded output decoded completely and
contained the incoming audio (RMS 0.1488 in the final counter run). Both HTML/body
backgrounds were transparent. Whole application-tree CPU during that short
recording was approximately 90% of one logical core, not 90% of the machine.

The supplied VP8 stream showed GPU 3D activity but no video-decode engine activity
in a twelve-sample Windows counter run. A separate twelve-second Chromium trace
showed generic WebRTC decoding and YUV uploads, without D3D11 decode events.
This indicates a software decode path for that VP8 stream on this configuration;
GPU rendering being enabled alone is not proof of hardware video decoding.
Receive-only WebRTC statistics omit hardware decoder identity for privacy;
hardware decoding was positively confirmed separately with the H.264 fixture.

Requesting H.264 on the same VDO.Ninja stream initially also fell back to
software. A controlled comparison reproduced the
[reported WebView2 hybrid-decoder bug](https://github.com/MicrosoftEdge/WebView2Feedback/issues/5755):
stopping negotiation of the RTP color-space extension restored actual D3D decode
events in the GPU process. Disabling the hybrid decoder globally was rejected
because that would remove its HEVC path. The packaged fix uses the targeted
negotiation workaround instead. The source option preserves the original color
metadata when it is more important than avoiding this runtime fallback.

The final packaged app was then exercised with H.264 requested on the live viewer:
an eight-second trace contained 11 `D3DVideoDecoder::Decode` calls and Windows
reported video-decode-engine activity. Its recording included incoming audio
(RMS 0.3965) and decoded cleanly. Enabling the source's metadata-preservation
option saved `preserveColorMetadata=true` and restored software fallback;
turning it off saved `false` and produced 13 D3D decode calls in a new trace.
These checks used the shipped compatibility script, not a QA-injected workaround.
Short-run whole-tree CPU while recording was 78.54% of one logical core. Network
content and capture timing varied between runs, so this is not a controlled
performance comparison with the earlier VP8 result.

`mixer-browser-security-e2e.py` was also rerun against this package. Camera,
microphone, screen capture, clipboard, notifications and native host-object access
were denied. File/custom-scheme navigation, downloads and popups were blocked.
Malformed native messages were ignored. An eight-second message flood left the
app responsive with 0.01 MiB parent RSS growth. Killing the browser host recovered
the source while recording continued; that recording fully decoded afterward.

## Scope and artifacts

The Release build/package, Python syntax and whitespace checks are gates,
separate from the shipped-application workflows above. Test scripts accept the
live viewer through `VIDEO_MIXER_E2E_VDO_URL` rather than committing a private
stream identifier. Private artifacts are in
`native-qt/e2e/reports/browser-resilience/`.

Tested executable SHA-256:
`8ce2dff5c7acc04e89a6dff93d0b5ef652805d60447c5536e8940f30d70fb2df`.

The portable ZIP and installer are rebuilt for 0.6.2. Installer upgrade testing
was last performed for 0.6.0. This patch does not repeat the earlier protocol and
hardware-encoder suite. The application was closed after testing and the user's
original show/settings restored byte-for-byte; Game Capture was unchanged.

Browser-process isolation does not isolate a machine-wide GPU-driver/OS failure
or guarantee freedom from browser vulnerabilities. Transparent extraction still
uses CPU PNG snapshots with a 15 fps ceiling. There is no claim that this patch
turns that path into a GPU-only compositor or improves ordinary capture CPU use.

References: [Windows job CPU-rate control](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_cpu_rate_control_information),
[WebView2 process model](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/process-model),
[WebRTC hardware-statistics privacy rules](https://w3c.github.io/webrtc-stats/#limiting-exposure-of-hardware-capabilities).
