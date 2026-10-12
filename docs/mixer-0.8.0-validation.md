# Desktop Studio 0.8.0: production controls and validation

This pass implements practical solo-show, interview, gaming, podcast and portrait
controls, then exercises the packaged Windows application and its real media
outputs. Build, syntax, schema and whitespace checks are separate gates, not
end-to-end testing. Private recordings, screenshots, invitations, device details
and logs stay in ignored `native-qt/e2e/reports/studio-080/` directories.

The final executable SHA-256 is
`baba40f1f4df20985e01c62c163f5c6b4640f1b09ddb62114769272dda3d6980`.
Most broad coverage and the extended rehearsal use candidate
`f763c48d0b0100f11bb99135353620110a1ccd5731e89365c7e47ce1ca1c5ce3`.
The viewport candidate (`780cabb69781aa4b1212d6d7f9963bc0fb930b3028f5d0dce41b4fb1bb33625a`) retains capture windows' configured size even when taller
than the operator's monitor, and passes that size to the opaque capture path.
Portrait browser GUI/recording, both capture modes at both portrait sizes, live
WebRTC hardware decoding and GPU/CPU faults, conversation, production controls,
shortcut conflicts, Unicode files and SRT mode errors ran on that candidate.
Headless browser audio and crash recovery plus media boundaries were repeated
on the intermediate candidate that had the window-size fix but still used the
old opaque capture dimensions. Show-format workflows used an earlier candidate,
before browser warning delivery, MP4 process cleanup and toolbar/status sizing
changes. Candidate `9d25b033f191af147c8beec56bbcf4b838cc95fd11b1b8dfba3a75ebedb4cbfa` additionally updates the spoken names of the new
toggle buttons and announces recording-state changes without repeatedly speaking
the changing disk estimate. Real NVDA workflows were repeated on it, including
70 seconds of quiet observation during recording. The final executable fixes the
audio scheduling timestamp issue found by the long run, described below. These
scopes must not be conflated.

## Implemented behavior

- **Private break** keeps recording and streaming running with a silent slate.
  Admitted guests and unmuted host microphones retain their private conversation.
  Resume restores the selected Program. It starts off each launch.
- Per-track **On air** separates public audio from admitted-guest conversation
  and headphone Listen. **Mute mics** silences local microphones across all buses
  while preserving individual channel mute settings.
- **Hotkeys** registers optional, configurable Windows shortcuts for break,
  microphone mute, Take, held talkback and scenes. Scene keys stage Preview in
  Studio mode. Registration is all-or-none on conflict. No keyboard hook or input
  logging is used; release polling runs only while a registered key is held.
- Audio tracks offer stereo, averaged mono, left-to-both and right-to-both
  routing before effects and output buses. This operates on a track's decoded
  stereo pair; it is not arbitrary multichannel hardware-input selection.
- Browser sources have independent viewport dimensions. New UI sources use the
  canvas size; existing shows and API defaults retain 1280x720. **Safe area**
  displays a generic 10% margin in the editing canvas, never in Program media.
- **Recordings** opens the output folder or exports a completed MKV to a new MP4
  by remuxing without re-encoding. The original remains intact, existing outputs
  are refused, and FFmpeg has independent process/job cleanup. Recording status
  includes an approximate remaining disk time using its existing disk check.
- Media playback exposes elapsed/duration/remaining time and optional restart
  when a source enters Program. A shared source that stays visible keeps playing.
- CLI/API/MCP adds invitation creation, admission/backstage, return-mode control,
  production controls, channel routing, viewport size, media cue and MP4 export.
  The schema now exposes 36 tools. Private invitation URLs are returned only by
  explicit guest commands, not normal state.
- Browser media without cross-origin audio permission now reports a specific
  warning. The bridge does not bypass browser security; this media can still be
  silent. Unsupported protected/unusual audio paths remain limitations.

## Actual user workflows and media

| Workflow | Observed result and scope |
| --- | --- |
| Production controls through the visible GUI | Decoded recordings verify stereo/mono/left/right routing from a 44.1 kHz source, exact silence for Off air and Private break, and resumed audio. MP4 exported through file dialogs decoded with the original MKV retained. Disk estimate became available. |
| Global shortcuts with another application focused | Break/resume, microphone mute/unmute, scene selection and held-talkback press/release worked without stealing foreground focus. An actual crash found during the first run was fixed: Qt can pass null result storage for a thread `WM_HOTKEY`. The final packaged run passed. |
| Shortcut conflicts and customization | A separate process held the default break shortcut. Studio identified the conflict and disabled the complete set. Editing to a new combination worked, the old one was released, and global scene/Take shortcuts respected Preview/Program staging. |
| Interview and wireless invitation UI | Two synthetic VDO.Ninja guests and a wireless sender completed invite/preflight/start/admit, backstage, mute, Off air, private break, off-screen conversation, Scene routing and return-mode workflows. Own voice was removed from returns. Peer departure, rejoin and persisted return choices were exercised. This uses live WebRTC pages, not real phones or distant humans. |
| Show formats | Real bidirectional 400 ms picture/audio fades, audio-only media with cover art, and a copied show folder with relative assets passed. Assets were copied manually; there is no automatic collect-assets feature. |
| Unicode and spaced file paths | Actual media with accented and Japanese filenames loaded, saved with a relative path, reloaded from an exported show, recorded and exported to MP4. Both output files decoded with tone. |
| Media cue UI | A clip reached its natural end, displayed duration/remaining time, then restarted on off-screen-to-Program activation. Its recording decoded. This is not seeking, a playlist or frame-accurate cueing. |
| Headless browser audio and viewport | Oscillator, AudioWorklet, HTML media, permitted CORS media and a cross-origin iframe produced decoded audio. A responsive page selected its green portrait layout at 720x1280 and blue landscape layout after changing viewport to 1280x720. No-CORS media was silent and displayed the explicit warning. |
| Main-app termination | Killing the owned main process terminated its workers. The interrupted MKV retained playable picture and tone. Relaunch retained the isolated show's audio routing and did not automatically start outputs. A decode using preserved source timestamps completed without errors; the first null-output analysis had timestamp-rounding warnings and was repeated with the correct output time base. This short case is not a guarantee for every crash or filesystem failure. |
| API MP4 export | Exported audio decoded; a second export to the same path was refused and the existing file hash stayed unchanged. |
| Guest panel | Four, six and eight synthetic live WebRTC guests received video and other guests' audio with their own tone below -50 dB. Initial full-frame-source process-tree private memory was approximately 1,874 / 2,910 / 3,927 MiB respectively. These are short-run observations, not recommended capacity limits or a grid benchmark. Recording decoded through private break, backstage/readmit and a departing guest. |
| Eight-guest grid | A second run used eight distinct moving colored pictures in assigned grid cells and verified their identity/placement, returned audio/video, private break, backstage/readmit and recording. Private memory at four/six/eight guests was approximately 1,857 / 2,787 / 3,725 MiB. Received RGB values differed from sender colors by up to 28/255 per channel; this establishes layout and routing, not color fidelity. The first strict color assertion failed and is retained in the private report. Broader color-metadata/codec qualification remains open. |
| Guest color metadata | A subsequent live H.264 color-chart comparison measured maximum channel error of 28/255 with the compatibility workaround and 2/255 with **Preserve original video color metadata** enabled. The option is an available remedy for color-sensitive sources. It can incur software decoding on affected runtimes; this comparison did not measure the decoder/cost of the preserved-metadata case. |
| Portrait | All four portrait presets, Stacked layout, crop/Fill, Preview/Take/fade and export/open passed. Real Spout and Edge virtual-camera consumers received 720x1280 moving frames (60/45 frames, 29/43 distinct respectively). Virtual camera supplies video only. |
| Portrait browser GUI and capture modes | The initial full-frame recording assertion caught Windows shrinking a 1280-pixel-tall browser to monitor height. A further check caught the opaque path still requesting 1280x720 capture. Both were fixed. The repeated GUI workflow verified canvas defaults, responsive layout changes, persistence, and a safe guide absent from the actual recording. Moving pages at 720x1280 and 1080x1920 then filled the correct edges in both transparent and opaque modes. Static opaque pages correctly emit few frames; the fixture was animated for the cadence assertion. |
| Large and corrupt media | A moving 4096x2160 file played and recorded into the selected canvas. The recording decoded, an overwrite attempt left its hash unchanged, and corrupt media reported an error while the interface remained usable. |
| Publish and record | Private RTMP and WHIP receivers decoded 1080p60 picture/audio. Encrypted SRT decoded. WHIP 403, cross-origin bearer-token redirect and oversized SDP answer failures did not stop independent recording. Actual authenticated social-service delivery is not established. |
| SRT mode boundaries | Listener and rendezvous requests were explicitly rejected: only caller mode is implemented. Independent recording continued and decoded. Cancelling a caller connection with no receiver completed in approximately 1.05 seconds. This is error-handling coverage, not listener/rendezvous support. |
| Restricted network | A local RTMP proxy restricted bandwidth for 35 seconds. Receiver playback recovered after the owned TCP connection was explicitly reset, flushing stale backlog. Independent recording decoded throughout; API p95 was about 25 ms. This does not establish seamless recovery from bandwidth restoration alone. |
| RTMPS negative connection | A private ephemeral self-signed TLS server was refused before RTMP application data; its server observed unexpected EOF. The publish command enables certificate verification. This is a negative connection check, not positive trusted RTMPS delivery or complete TLS validation coverage. No Windows trust-store modification was made. |
| Hardware/compositor matrix | CPU/GPU composition at 1080p30/60 exercised Auto, NVIDIA, Intel and software encoders. All 16 available combinations decoded; four unavailable AMD cases were rejected explicitly. NVIDIA Video Encode GPU counters were observed; Intel reported the actual QSV backend. Measured rates were roughly 29.98/59.94 Auto, 29.62/59.37 NVIDIA, 28.88/57.90 Intel and 30/60 software. The gate allows 90% of target, so these are not all perfect nominal frame rates. Concurrent work prevents an isolated performance comparison. |
| Browser GPU and overload | The supplied example viewer did not deliver video during its 50-second check. An owned private H.264 sender then produced real D3D decode trace events. Browser GPU-process termination recovered. A 24-worker page reached about four CPU cores; API p95 stayed near 26 ms, healthy video continued, and the resulting recording decoded with audio. |
| Concurrent browser failures | Three busy pages consumed about 12 CPU cores in total while each individual limit remained active. Healthy browser output continued. A hung renderer was replaced in 14.22 seconds; a GPU crash recovered. Three automatic reconnects followed by a fourth crash paused that source; manual Reconnect restored it. Memory exhaustion reached approximately 4,054 MiB and produced a JavaScript allocation error while healthy recording continued. |
| Headphone output and failure isolation | Actual loopback capture verified audio went only to the selected one of two Windows endpoints, and was silent with monitoring off. Crashing and hanging the monitor worker disabled it without stopping Program recording or RTMP; both recordings decoded with continuous tone. Device names and measurements remain private. |
| Hostile browser page | Local device, clipboard and screen permissions, file/custom-scheme navigation, popups and downloads were blocked. Malformed native messages and unauthenticated local API access failed. An eight-second message flood kept UI sampling below 73 ms with about 0.26 MiB parent RSS growth. Worker recovery and recording passed. This is not a security certification or a guarantee against browser/driver vulnerabilities. |
| Accessibility | Real NVDA and keyboard workflows added sources, recovered from validation errors, changed gain/mute and Preview/Take, then recorded and decoded output. The final run also verified the spoken Resume show/Unmute mics button names and 70 seconds of active recording without unsolicited speech. Unfocused meters generated no continuous speech/beeps. Broader assistive-technology certification remains open. |
| Installed CLI/MCP | A private installation of the final executable on this machine matched the portable/ZIP executable. Actual MCP calls discovered 36 tools and controlled guest admission, routing and private production controls. Twelve live portrait screenshots changed while recording; recording decoded with a largest timestamp gap of 49 ms. Quit left no owned descendants. Uninstall removed the private installation and its registrations. This is not the deferred clean-machine check. |

The brief hardware, panel and browser measurements overlap some other tests.
They measure these workloads on this machine, not comparative efficiency gains.
Windowless runs use the current Windows user session; they do not establish
Windows service/Session 0 operation or capture while the session is locked.
Per-source browser budgets do not form a combined budget: several hostile pages
can consume the sum of their individual limits. Each guest video return still
has a separate encoder, making large panels materially more expensive.

## Extended rehearsal

The pre-viewport-fix candidate completed one hour at 1080p30 with CPU composition,
C920 camera/microphone, a browser overlay, scene changes, independent MKV recording
and NVIDIA RTMP publishing to a private receiver. All seven injected faults
recovered: browser, microphone, camera, publishing encoder and recording encoder
termination, a 12-second receiver outage, and a suspended browser worker. The
example viewer was replaced by the owned live H.264 sender at approximately
743 seconds; its earlier portion showed a waiting page. Every receiver capture
decoded without errors.

**This original run was not a clean pass.** Full decoding of the recording found
repeated audio timestamps under load, despite its small measured flash/tone drift.
The audio loop had stamped each 10 ms chunk with the current wall clock. Catch-up
iterations following a scheduling stall could therefore stamp several chunks
within the same millisecond. The final build uses the scheduled sample interval,
keeping successive chunks 10 ms apart; larger overruns can still move the clock
forward under the existing bounded recovery policy.

A packaged reproduction suspending only the owned main process for 65 ms,
45 times, produced the same timestamp errors before the fix and decoded cleanly
after it, with continuous tone. The final build then passed 900 such scheduling
stalls: the complete MKV decoded without errors and every measured half-second
audio window retained tone. Guest conversation/mix-minus, RTMP/WHIP publishing,
encrypted SRT and WHIP failure-isolation workflows also passed on this build.
This is not a fresh clean one-hour run of the corrected executable.

Final-build 30-second timing cases delivered approximately 30 output frames per
second with maximum frame intervals of 35–36 ms. The measured p95 absolute
flash/tone offsets were 18.65 ms for local media, 65.65 ms for an opaque browser
video, and 67 ms for a transparent overlay. The opaque source included its
configured 60 ms audio delay; the overlay itself updates at 15 fps. Whole-tree
CPU use averaged approximately 1.18, 0.44 and 0.66 logical cores respectively;
the main process used 0.29, 0.08 and 0.06 cores. These short observations overlap
other QA activity and are not isolated comparative performance benchmarks.

The original hour provides these scoped measurements:

- 108,062 video frames over 3,614.506 seconds, averaging 29.896 fps. There were
  150 intervals over 50 ms; the largest was 404 ms, including injected faults.
- Flash/tone median video-minus-audio offsets were 0 ms early and -16.67 ms late,
  a -16.67 ms change. This measures the synthetic overlay's clock, not physical
  camera/microphone or distant-guest lip sync, and does not erase the timestamp
  errors above.
- Last 20 minutes: 1,424–1,467 MiB process-tree private memory, median 1,441 MiB,
  with a positive fitted slope of 1.72 MiB/minute. Longer-run memory profiling is
  still needed; this is not a leak-free claim.
- Last-20-minute CPU median was 146.4% of one logical CPU (about 1.46 cores) for
  the complete Studio tree, including capture, browsers and two output paths.
  State-plus-process sampling p95 was 36.4 ms. The recording drop counter remained
  at 22 during that interval; it is not a complete count of all late video frames.

Other QA/build activity overlapped parts of the run. These observations are not
an isolated performance comparison or a claim that composition alone costs 1.46
cores.

## Remaining engineering

- Separate/ISO guest recordings and multitrack audio; arbitrary physical
  multichannel input selection; multiple Program buses, simultaneous landscape
  and portrait, and direct multiple publishing destinations.
- SRT listener and rendezvous modes. Current SRT publishing is caller mode.
- Automatic asset collection/relinking/backups and an active-project Save As
  workflow. Current Save a copy is deliberately a snapshot export.
- Seeking/scrubbing, playlists, frame-accurate media cueing and stronger gapless
  AAC looping; a complete source-clock scheduler or video delay when audio is late.
- Shared guest video encoding, an aggregate browser resource budget and fewer
  CPU frame readbacks. Browser video remains 30 fps, transparent overlays up to
  15 fps. The older GDI-window gaming cadence limitation is not erased by this run.
- Color fidelity across browser codecs/color metadata and all hardware encode
  paths, including the measured guest-grid channel deviations above; HDR is not
  established by an SDR canvas test. Preserving original browser metadata fixed
  the chart tested here, but is not a universal codec/driver qualification.
- A browser audio fallback for unsupported cross-origin/protected/complex graphs.
- A longer clean soak on the corrected audio clock and investigation of the
  positive memory slope observed late in the original hour.
- Broader localization and accessibility coverage. Local Spout/virtual output
  and some inherited native paths still run in the main process; process isolation
  cannot contain OS/kernel/GPU-driver faults.

## Checks requiring user equipment or coordination

- Real phone rotation, backgrounding, incoming calls and Wi-Fi/cellular handoff;
  real distant guests and a physical camera/microphone clap or lip-sync check.
- Authenticated streaming destinations and social-chat accounts, including a
  trusted RTMPS endpoint. Private receivers do not establish provider acceptance.
- Multichannel USB interfaces, Bluetooth headsets, other camera/GPU families
  including AMD, HDR/multiple-GPU systems, and actual exclusive-fullscreen games.
- Removable and network recording drives, including disconnection during writes.
- Zoom/Teams/Discord camera consumers and their audio-device workflows.
- Coordinated Windows lock/sleep/resume and remote-desktop disconnect. These
  should not be forced during the user's normal session.
- The clean-machine/Windows 10 install explicitly deferred by the user.

## Reproduction

Run from `native-qt` with the existing desktop QA Python, Playwright/Edge and a
local MediaMTX executable, using new private report directories:

```powershell
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/production-review --mediamtx C:/path/to/mediamtx.exe production-controls media-cue portrait-browser conversation show-types portrait output-security protocols-050
python -B e2e/mixer-080-automation-e2e.py --report e2e/reports/automation-review
python -B e2e/mixer-panel-080-e2e.py --report e2e/reports/panel-review
python -B e2e/mixer-network-080-e2e.py --report e2e/reports/network-review --mediamtx C:/path/to/mediamtx.exe
```

Do not run multiple visible UI workflows at once. The GUI runner leaves a failed
owned app available for inspection. Tests use separate show files. No invitations,
recordings, credentials, browser profiles or private test logs belong in Git.
