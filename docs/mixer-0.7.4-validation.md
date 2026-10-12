# Desktop Studio 0.7.4 workflow and timing review

This review uses the packaged Windows app, actual native controls and visible
Edge invitation pages. Recordings and private RTMP/SRT/WHIP receivers are decoded
to verify the delivered picture and sound. Private media, room links, screenshots
and raw measurements remain under `native-qt/e2e/reports/studio-workflows-074/`.
The machine has 20 logical processors. CPU numbers below use 100% for one logical
processor, not the whole machine. Build and syntax checks are gates, not these
application workflows.

## Intended shows and priorities

| Priority / show | Supported user flow | Important boundary |
|---|---|---|
| P0: solo presenter | Webcam/window, microphone, titles, chat overlay, single-canvas editing, record or publish | Camera controls and delivered frame rate depend on the device and exposure. |
| P0: interview / panel | Invite, preflight, backstage, admit, frame guests, switch shots, mix-minus and private talkback | New guests stay audible off-screen; admission can put their audio on Program before Take. Each video return has its own encoder. |
| P0: podcast | Always-on microphones/guests, audio meters, monitor device, static cover or camera scene, Program recording | The recording is the mixed Program, not separate guest or microphone tracks. |
| P0: gaming | Native window/game feed, game audio, mic, webcam, overlay, 30/60 fps Program | Windows capture is not an injected game hook. Fresh picture cadence must be measured separately from output timestamps. Exclusive-fullscreen games were not exercised here. |
| P0: recording plus streaming | Independent Record and GO LIVE lifecycles; private RTMP, SRT and WHIP receivers | One publishing destination and one Program canvas. The two outputs can fail/recover independently; earlier isolation evidence remains in 0.7.3. |
| P0: vertical production | 720x1280 or 1080x1920 at 30/60 fps; Stacked, Fill/crop, titles/overlays; portrait record/stream | Stop all outputs before changing canvas. Landscape and portrait cannot run as separate simultaneous productions. |
| P0: low-latency conversation | VDO.Ninja receive pages and dedicated mix-minus returns; selectable wireless return | Network conditions, browser rendering, monitoring and receiver buffers add delay. A local measurement is not an Internet latency guarantee. |
| P1: produced show | Opening/Main/Break, single or Studio mode, cut/fade, prerecorded clips, saved layouts | Media runs off-air to keep switches immediate. Restarting a clip is manual; AAC loop padding is not guaranteed gapless. |
| P1: repeatable show | Workspace autosave, exported show copies, relative media/image paths inside the show folder | Copy the assets too. Export does not switch the active autosave workspace or embed assets. |
| P1: unattended / accessible operation | Local CLI/API/MCP, headless production and screenshots, keyboard/screen reader labels, themes and languages | Local automation is authenticated. Existing automation/accessibility evidence is documented in 0.7.0–0.7.3. |

## Changes from the review

- Media loops wait for the complete decoded interval, including the number of
  emitted audio samples. Previously, seeking at the last sample's leading
  timestamp let audio queue up progressively behind the next picture loop.
- A 400 ms scene fade now fades scene-following audio as well as the picture.
  Always-on conversation tracks stay constant. Mute and Backstage remain immediate.
- Newly invited guests default to always-on Program audio. Existing shows keep
  their saved Scene setting. The admission message and tooltip explain when
  audio becomes live, including before Take in Studio mode.
- Portrait adds 720x1280/30/60 and 1080x1920/60 alongside the existing
  1080x1920/30 preset. Existing show/API format IDs remain stable.
- Stacked arranges unlocked visible pictures vertically and uses Fill for each
  tile. Lock backgrounds/overlays first; select Fit when cropping is unwanted.
- Existing window/Spout workers reconnect when the canvas format changes, so
  changing a show from 30 to 60 fps also changes capture pacing. Cameras retain
  their separately chosen device format.
- Save a copy makes export/autosave behavior explicit. Media/images beneath the
  export directory use relative references and resolve against the imported show.
- A blocked output writer no longer attributes every failure to bitrate. The
  message distinguishes checking recording storage from checking a destination,
  credentials and bandwidth. Raw FFmpeg errors still cannot leak private URLs.
- Immediate automation screenshots compose the current Program state. They no
  longer return a cached landscape frame after a portrait format command, and
  do not advance output pacing or submit extra recorded frames.

## Measured correctness

The unmodified 0.7.3 baseline completed the physical C920/mic first-show flow,
workspace/theme/layout changes, save/open validation, crop/resize/undo/redo,
CPU/GPU pixel comparison, virtual-camera consumption and independent SRT/record
lifecycles. Its first WHIP stop pointer action did not change state; the second
did. The complete RTMP/WHIP workflow subsequently passed on the changed build.
No product fix is attributed to that unreproduced pointer-action failure.

A shared encoded flash/beep clock exposed real native-media loop drift. In the
95-second baseline, the 95th percentile absolute audio/video offset was 166 ms,
and the early/late median difference was 90 ms. After the loop fix, a three-minute
run matched 179 pulses: 95th percentile 22 ms, median 6 ms, early/late difference
4 ms. Output averaged 30.0001 fps; the largest timestamp gap was 36 ms. Private
memory moved from 336.7 to 332.4 MiB, and process-tree CPU was 74.8% of one logical
processor (about 3.7% of the machine). This is a controlled-file result, not a
universal source synchronization guarantee.

Two distinct continuous-tone media sources exercised both directions of a real
scene fade. Audio's 20–80% change took 220–229 ms, consistent with a 400 ms full
fade; audio/picture midpoints differed by at most 19 ms in the successful run.
An audio-only WAV with a cover image recorded the expected tone. Exported local
asset references were relative; a copied show folder loaded its changed blue
cover and played the relocated WAV into a decoded recording.

Two real VDO.Ninja browser guests and a browser acting as a wireless camera
repeated the conversation workflow. Both off-screen guests appeared in the
Program recording by default. Enabling Scene made the off-screen Program silent
while their private conversation continued. Backstage, mute, re-admission,
send-only/audio-only/video returns, mix-minus, a peer leaving, and enabled/disabled
return persistence through app restarts passed. The wireless full-return versus
send-only samples used 100.7% versus 90.2% of one logical processor and 1664.7 versus
1542.6 MiB private memory; these are short workload-specific samples.

The eight canvas presets each recorded a 60 fps 1280x720 moving flash/beep file
for 25 seconds, using CPU composition and independent recording. Decoded output
averaged the selected 30 or 60 fps. The recorder's initial dropped-frame counter
did not grow during any measured interval; its startup count was 11–29 frames.
Timestamp gaps stayed below 38 ms at 30 fps and 21 ms at 60 fps. Short repeats
remain possible at media-loop boundaries; this is distinct from timestamp pacing.

| Canvas | CPU, % of one logical processor | 95th percentile absolute A/V offset |
|---|---:|---:|
| 1280x720/30 | 74.2 | 18 ms |
| 1280x720/60 | 95.8 | 17 ms |
| 1920x1080/30 | 120.2 | 27 ms |
| 1920x1080/60 | 181.3 | 17 ms |
| 720x1280/30 | 80.6 | 18 ms |
| 720x1280/60 | 121.6 | 12 ms |
| 1080x1920/30 | 111.6 | 22 ms |
| 1080x1920/60 | 176.8 | 20 ms |

An opaque browser playing the same clip into 720p30 recording used 51.0% of one
logical processor, with a 62 ms 95th percentile A/V offset. Transparent capture
used 72.8%, with 93 ms and its intentional 15 fps source ceiling. Both retained
default 60 ms Program audio delay. The native media decoder consumed roughly
45–49% of one logical processor regardless of canvas; composition/upscaling and
encoding accounted for most of the increase at larger output formats. These
results identify costs; the loop fix is not claimed as a CPU optimization.

Selecting the existing GPU compositor for the same 1080p60 workload reduced the
short-run tree CPU measurement from 181.3% to 156.2% of one logical processor
(about 14%), while private memory was roughly 504–525 MiB versus 433 MiB with CPU
composition. Recorded rate remained 60.003 fps and 95th percentile absolute sync
23 ms. This supports offering both choices; it does not justify a universal GPU
default or a claim of zero-copy processing.

Portrait presets, stacked geometry, Studio preview/Take/fade, Fill/crop and
show export/open were exercised through the interface. A private SRT consumer
decoded 720x1280 video and the expected audio while two independent recordings
started/stopped before, during and after streaming. The virtual-camera portion
of that workflow remained 1280x720; it does not establish portrait virtual-camera
compatibility in downstream applications.

A separate VDO.Ninja invitation used a synthetic camera and microphone sharing
one audio clock. In twenty same-PC samples, sender canvas to received webpage
video delay was 55 ms median / 80 ms 95th percentile. This measurement ends at the
receiving video element, before Studio composition, recording or audience playback.
Its 30-second Program recording averaged 30.0007 fps, matched 27 flash/beep pairs,
and measured 47 ms median / 80 ms 95th percentile absolute A/V offset. No timestamp
gap exceeded 36 ms. The default browser audio delay remained enabled; real sources
can need different calibration.

The native-window workflow was then repeated with a GPU-rendered Edge canvas
whose actual animation rate was measured independently. Four changes on the
existing source delivered 29.97, 60.11, 29.98 and 59.96 frames/s for 30/60/30/60
targets, while the producer remained near 60 fps. The 60 fps recording had 623
decoded frames and 95.9% of consecutive interior frames changed significantly.
This verifies the existing-source rate fix with a consistently presented input;
it does not erase the less consistent GDI-window observation below.

## Remaining work

- Measure and improve **fresh-frame gaming capture cadence**. The GDI animated
  window negotiated approximately 30 fps, then 52–54 fps after switching to 60.
  Its recording had steady 60 fps timestamps but only about 64% of consecutive
  frames contained significant picture changes. This fails the stronger motion
  criterion; correct output timestamps alone do not establish smooth 60 fps input.
- Browser video is currently 30 fps and transparent overlays up to 15 fps.
  Browser viewport configuration remains 1280x720; crop/Fill can place it in a
  portrait show, but a dedicated portrait web layout/viewport control is still needed.
- Local media decoding and final frame readbacks still involve CPU memory.
  High-resolution/60 fps upscaling and multiple return encoders are significant
  costs. GPU composition is optional; it does not prove hardware video decoding.
- Separate/ISO recordings, multitrack audio, multiple independent Program buses,
  simultaneous portrait/landscape output and direct multi-destination publishing
  remain feature gaps. A single Program can be sent to an external relay.
- Guest invitation creation, admission/backstage and return-mode selection still
  need dedicated API/MCP commands. A headless guest production currently needs a
  prepared show; general scene/source/audio/output control is already available.
- Only positive audio delay (0–500 ms) is adjustable. Sources with audio arriving
  late need video delay or a more complete timestamp-aligned source scheduler.
- Controlled C920 lip-sync/clap calibration, actual phone rotation/backgrounding,
  long-distance/adverse-network latency, authenticated social-service delivery,
  other GPU/audio-driver families and the deferred clean-machine install are not
  established by these runs. Earlier browser/device/output fault results remain
  scoped to the machines and failures described in their reports.

The original Game Capture application and Ninja plugin are not changed. VDO.Ninja
continues to run as a complete isolated browser page, preserving its page-level
features rather than introducing a partial native receiver.

## Artifact and closeout

The final executable SHA-256 is
`2d080194dedcef0cbec64079bd71797ebd73937da0ef02efb654f1c19f3a44a4`.
The full format matrix and guest timing ran on the preceding 0.7.4 candidate.
Subsequent changes were limited to Stacked Fill, output-error wording and fresh
automation screenshots. Portrait and fade/podcast/relocated-show workflows were
repeated after the screenshot composition change.

On the build before that screenshot change, publishing and recording encoder
workers were terminated independently, followed by a private receiver outage.
Replacement workers appeared in 0.02/0.28 seconds; recording continued and all
five recording/receiver files decoded fully with audio. The restarted receiver
received the stream again. RTMP/WHIP and pause/restart/loop/end, themes, workspace
layouts and fullscreen Program were also repeated during closeout.

A private install on this machine matched the portable and ZIP executable hash.
Its actual CLI and MCP client exercised 32-tool discovery, all four portrait
presets, invalid-format rejection, monitor devices, state resources and Program
PNG capture. The immediate screenshot had the correct portrait dimensions and
new scene's blue pixels. Twelve live portrait screenshots followed a moving
input while recording; the recording decoded fully and its largest timestamp
gap was 48 ms. Quit left no owned workers, and uninstall removed the private
installation, shortcuts and registration. This is not the deferred clean-machine
check. Final documentation was refreshed without changing the executable.

The original show retained its baseline SHA-256. Registry preferences were
restored byte-for-byte, including removing the theme value introduced by the
workspace workflow. Game Capture retained its original HEAD and pre-existing
untracked note. Build, syntax, JSON, dependency installation, whitespace and
package checks passed as gates.

## Reproduction

Use fresh private report directories from `native-qt`, the desktop QA Python
environment, Playwright/Edge and a local MediaMTX executable:

```powershell
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/show-review --mediamtx C:/path/to/mediamtx.exe show-types conversation guest-timing gaming-rate portrait protocols-050 workspace
$env:STUDIO_OUTPUT_FORMAT='720x1280 / 60 fps'
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/portrait-outputs outputs-050
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/timing-matrix timing
$env:STUDIO_TIMING_CASES='media-720p30'
$env:STUDIO_TIMING_SECONDS='180'
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/loop-endurance timing
```

`STUDIO_TIMING_RENDERER=gpu` selects the GPU comparison; the default uses CPU.
`STUDIO_GAME_FIXTURE=gdi` selects the less consistent native GDI animation and
retains its stricter motion assertion as a diagnostic failure, not a passing
claim. The normal window workflow uses the independently measured Edge canvas.
The runner leaves a failed app open for inspection; quit it before starting a
second GUI workflow. Original user shows use a separate path from every run.
