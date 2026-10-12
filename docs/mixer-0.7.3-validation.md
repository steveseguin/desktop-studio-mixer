# Desktop Studio 0.7.3 audio and isolation validation

Packaged Windows application workflows on the same 20-logical-processor machine
as 0.7.2. Visible native controls and Edge invitation pages drive the workflows;
read-only instrumentation, real playback-device loopback, private RTMP/SRT
receivers and full recording decodes verify output. QA media, private room links,
logs and screenshots remain ignored under
`native-qt/e2e/reports/studio-audio-073/`.

## Changes

- Monitoring output selection persists by Windows endpoint ID, with full device
  names and an asynchronous Refresh control. An unavailable selected endpoint
  stays unavailable instead of routing to the Windows default.
- Monitoring device discovery and playback run in separate processes. A fixed
  four-block queue isolates the Program mixer from driver calls; a stuck write
  is bounded by a 250 ms IPC deadline. Failure disables monitoring and allows
  an explicit retry, leaving streaming and recording running.
- Admitted guests hear other admitted guests off-screen. Each return subtracts
  its own contribution before limiting. Unadmitted guests remain private;
  track mute applies to returns as well as Program. Scene routing of public
  Program audio is unchanged.
- Guest invitations default to Program video plus mix-minus audio. Wireless
  cameras default to send-only. The invite dialog offers none/audio/video return
  and saves the choice. A changed link must be reopened by a connected sender.
  Newly saved shows also preserve the master return switch.
- CLI/API/MCP gains `monitor.set`; `state.monitor` reports endpoint IDs, full
  names, availability, gain, enable state and discovery progress.

Endpoint mapping follows Microsoft's documented
[waveOut/MMDevice endpoint mapping](https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles-for-legacy-windows-multimedia-applications).
The local VDO.Ninja `main.js` and `webrtc.js` were inspected for `broadcast`,
`view`, `novideo` and `noaudio`. Each guest receives only its dedicated Studio
return stream, avoiding a second room-mesh audio path. An empty `view` list
requests no streams; `novideo`/`noaudio` affect incoming playback rather than
outgoing camera/microphone capture. No VDO.Ninja source or signaling code was
modified. Overload fixtures run only on loopback pages, not hosted services.

## Observed workflows

The monitoring workflow selected each real Windows playback endpoint through
the dropdown: display audio and Realtek Digital Output. The browser's 731 Hz
tone measured approximately 0.062–0.064 on the selected endpoint and zero on
the other. Both endpoints were silent with monitoring off. Refresh preserved
selection, and twelve repeated output switches completed.

During simultaneous native recording and private RTMP publishing, terminating
the monitoring worker disabled monitoring in 0.12 seconds; suspending it did so
in 0.35 seconds. Preview remained usable, and monitoring restarted explicitly.
Both the recording and receiver file decoded fully, with minimum one-second
audio RMS above 0.123 throughout their interior samples. These measurements
exercise process failure/hang, not a deliberately broken kernel audio driver.

Two visible browser guests used distinct 440/1660 Hz microphones, and a third
browser acted as a wireless camera with a 2370 Hz microphone. All entered via
the real preflight and Start controls. The checks established:

- Backstage guest voices were absent from returns.
- Admission enabled conversation before Take. Both callers still heard each
  other after switching Program to Break, without hearing themselves.
- The recorded off-screen Program was silent while that private conversation
  continued. Muting/backstaging a caller removed that voice; re-admission restored
  it. Private host talkback was also checked with the physical C920 microphone.
- Send-only wireless video/audio reached Studio with no inbound audio/video.
  Audio-only return carried the other guests and Program, excluded its own
  voice, and received no video frames. Full return received video and audio.
- Rejoining the wireless camera through all return modes preserved the other
  guests' mix-minus. After one caller left, the remaining caller received another
  120 return-video frames over four seconds.
- Restarting Studio with the same show resumed the remaining guest's live return.
  A second restart preserved a disabled master return switch, and the wireless
  camera's saved send-only mode survived both restarts.

In three six-second samples of this three-input room, replacing a wireless video
return with no return reduced process-tree private memory by approximately
116–122 MiB. CPU fell from 107.4% to 99.5%, 122.4% to 101.2%, and 135.7% to
108.2% (the final executable), where
100% is one logical processor. These are short workload-specific observations,
not a general CPU guarantee. The saved work is the unused return connection,
encoder and video delivery; the wireless input's quality is unchanged.

The combined browser overload run used three loopback pages, each spawning
24 busy workers. Their aggregate CPU was 1210% of one logical processor
(about 60.5% of this machine's total capacity), with a maximum measured native
window-response latency of 1.56 ms. The limit is per browser source, not an
application-wide budget. A healthy browser worker stayed unchanged throughout.
A hung renderer recovered in 13.74 seconds; a GPU-process crash recovered;
three automatic retries were followed by a pause after a fourth crash, and
manual Reconnect worked. Allocation pressure reached 4059 MiB in one browser
tree and produced a JavaScript RangeError without taking down the healthy source.
The complete recording decoded, with minimum one-second audio RMS 0.119 and a
maximum run of five nearly identical sampled frames. Browser diagnostics
reported D3D11VideoDecoder and hardware video decode enabled.

The mixed production rehearsal exercised the C920 at 1280x720/30, its microphone,
window capture, a transparent chat overlay, browser audio, scene cuts/fades,
recording, and a private SRT receiver. Both media files decoded completely.
During 120 seconds of mixed-show activity, private memory moved from 796.9 to
800.1 MiB; the maximum observed native window-response latency was 47 ms.
Monitoring, mute, off-air Listen and desktop capture exclusion were checked
against real playback loopback, with no monitor feedback into desktop audio.

The simultaneous recording/publishing fault workflow killed each output encoder
independently, then stopped and restarted the private receiver. Encoder recovery
was observed in 0.28 seconds for each path. All five resulting media files
decoded fully with audio RMS between 0.1247 and 0.1259; the recording continued
through the publishing/network failures.

## Reproduction and scope

The final executable SHA-256 is
`384d91cc1c1921c95771eebd8e6d1bcee57f5e6b1d17beed79500ef61cb130fc`.
The full six-workflow matrix ran on the preceding 0.7.3 candidate. A final
monitoring-only follow-up made selected endpoint/refresh state one atomic
snapshot; the monitoring failure/output workflow and full guest conversation,
wireless modes and show restarts were repeated on the final executable.

Normal-profile GUI restarts preserved the selected Realtek endpoint and both
canvas modes. A deliberately missing saved endpoint stayed marked unavailable
through three Refresh actions and refused monitoring without speaker fallback.
English/French/Spanish language restarts and the explicit close preference also
passed. This simulated a missing saved output; it was not physical audio-device
unplug/replug. The original show and preference backup were restored afterward.

A private installation on this machine matched the portable and ZIP executable
hash. The installed CLI launched a headless show, and a real MCP client exercised
the packaged server's 32 tools list, scene control, Program PNG screenshot,
state resource, output discovery/Refresh, monitoring device/gain/enable, and
invalid-device/gain rejection. Headless recording decoded fully. Quit left no
owned workers, and uninstall removed the private app, automation files, shortcuts
and registration. Only documentation was refreshed in the packages afterward.
This was not the deferred clean-machine check.

The original show matched its baseline SHA-256 and the settings registry export
matched its backup byte-for-byte. Game Capture retained its original HEAD and
pre-existing untracked note. Native build, Python/JavaScript syntax, JSON parsing,
dependency installation, whitespace and archive checks passed as gates.

Use a fresh private report directory with the packaged executable:

```powershell
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/audio-run --mediamtx C:/path/to/mediamtx.exe monitor-isolation conversation returns-050 browser-resilience output-recovery-ui
python -B e2e/mixer-preferences-ui-e2e.py --report e2e/reports/preference-run
```

Run from `native-qt` with the existing desktop QA Python dependencies,
Playwright/Edge, and two playback endpoints. The room uses controlled synthetic
browser media; no actual mobile orientation/backgrounding, authenticated social
delivery, clean-machine setup or other audio driver families are covered here.
Process containment does not establish protection against OS/kernel-driver
failure. Build, syntax, schema and archive checks are gates, distinct from these
application workflows.
