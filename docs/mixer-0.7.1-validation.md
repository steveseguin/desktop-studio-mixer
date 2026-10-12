# Desktop Studio 0.7.1 and 0.7.2 validation

Run on October 12, 2026 on the existing Windows workstation with a Logitech C920,
Intel Core Ultra 7 265K (20 logical processors), Intel graphics and NVIDIA TITAN
RTX. Clean-machine validation is explicitly deferred. Private cameras, media,
URLs, discovery credentials and screen-reader transcripts remain in ignored
`native-qt/e2e/reports/studio-extended/` directories.

## Changes found necessary by the workflows

- Total system CPU/RAM previously appeared only in Capture & output and refreshed
  during publishing. A permanent status-bar readout now updates every second
  throughout the app, including idle mixing and recording. The API exposes the
  same measurements. Closely spaced UI/diagnostic requests share a CPU sample.
- NVDA treated the audio meters as task progress and generated continuous beeps.
  The custom-painted meters now expose read-only indicators and on-demand dBFS
  values. Ctrl+Shift+L announces a level. Recording completion and invalid source
  input now produce announcements. Painting, size and focus styling are preserved.
- The low-space guard missed the actual capacity of a constrained UNC submount.
  It now queries the recording directory with `GetDiskFreeSpaceExW`, including
  the caller's available space, instead of resolving its parent volume.
- A rejected hardware encoder could leave automation reporting `stopped` while
  the GUI showed an error. State now reports `error`. External-output status text
  comes from the current output session, avoiding a stale publishing message
  immediately after disk failure. Canvas state reports the actual compositor.
- Frequent camera/microphone API enumeration stalled the UI/compositor while
  isolated device helpers started and stopped. These queries now run off the UI
  thread. Concurrent clients share the current scan; results are not cached, so
  device arrival/removal remains observable. Disconnecting or quitting safely
  releases pending requests.
- Physical USB removal exposed a microphone capture loop that remained active
  after endpoint invalidation. It now detects WASAPI errors even when the device
  stops signaling audio events, so the existing supervisor can reconnect it.

## Completed application workflows

These are real native-app operations and decoded outputs, separate from gates.
The QA package contains the release executable and packaged dependencies.

| Workflow | Result and evidence |
|---|---|
| System readout | Visible during idle and independent recording. Eighteen samples under bounded CPU load compared with Windows/psutil: median difference 1.68 percentage points, maximum 8.72 across differing sample windows; RAM difference below 2 points. `system1/`. |
| RTMP, SRT, WHIP | Updated 0.7.1 executable published to private local receivers while recording independently. Saved receiver files and recordings decoded, including the expected audio tone. `protocols-final/` (final executable including USB audio recovery). |
| Hardware/compositor matrix | CPU/GPU composition × 1080p30/60 × Auto, NVENC, QSV, software and AMD selections: 20 cases. All 16 available combinations produced moving video and the expected audio tone; the four unavailable AMD selections reported errors and left the app usable. `matrix2/`. |
| Browser defaults and GPU | Top-level transparent background, default audio, HTML H.264 `D3D11VideoDecoder` with platform decoding, GPU composition/decoding enabled. Live VDO.Ninja received VP8 video/audio; requesting H.264 separately produced 14 `D3DVideoDecoder::Decode` calls in a ten-second trace. `browser3/`. |
| Browser GPU crash | Killed the browser's GPU process; a new process appeared, frames resumed, recording continued and the result decoded with audio. `browser3/`. |
| Browser audio idle/resume | Final binary captured a tone, suspended the page's AudioContext for 15 seconds, then resumed audio without restarting its browser worker. Recording continued and decoded. `browser-audio-idle/`. |
| Physical Logitech USB recovery | Final executable observed camera and microphone absent for 20.31 seconds. Microphone audio returned 11.92 seconds after device reappearance; camera frames returned in 13.36 seconds. Both outputs stayed active throughout. The 1.15 GB independent recording and 554 MB receiver capture decoded cleanly, with the continuity tone present throughout. `usb-fixed2/`. |
| Browser CPU exhaustion | One page started 24 busy JavaScript workers. Its process tree used 399.52% of one logical core, matching the four-core job limit. Healthy video/recording continued; state-request p95 17.89 ms, maximum 25.84 ms. This is a per-source limit. `browser3/`. |
| Low disk space | A private 256 MiB WSL tmpfs exposed over UNC fell below the 128 MiB reserve. Independent recording finalized with an error; publishing continued and a fresh receiver recording decoded. `disk071/`. |
| Actual disk full | The bounded filesystem returned ENOSPC during file publishing. Output reported an error; an independent recording on C: continued and decoded. The partial failed file was preserved. Temporary mounts were removed. `disk071/`. |
| Two MCP clients | 32 concurrent scene additions retained unique IDs; concurrent rename/remove operations remained consistent. Recording continued after both clients disconnected. Stale discovery and non-loopback targets were rejected. `concurrency1/`. |
| Lifecycle | Duplicate launch of the same show was rejected without disturbing recording. Five launch/quit cycles and five quits during output startup exited; startup/quit cases took approximately 0.98–1.09 seconds. `concurrency1/`. |
| Device polling during recording | Repeated camera/microphone queries during 720p60 recording: control-request p95 152.47 → 25.29 ms; maximum 289.65 → 26.86 ms. An interior 17-second encoded-video interval improved from 54.34 → 60.00 fps, with maximum frame spacing 288 → 19 ms and intervals over 40 ms reduced from 10 to zero. Both files decoded with audio. Quit during eight outstanding queries took 344 ms and owned workers exited. `devices-before/`, `devices-after/`. |
| Visual audio meters and scroll boundaries | Real GUI checks of green/amber/red levels, peak hold, clipping, track/master mute, and bordered audio, camera-control and output scroll areas passed. `meters-final/`. |
| NVDA 2026.2 | Real NVDA speech logging and keyboard-only source creation, invalid-file correction, track/master volume, mute, Preview/Take and recording. The recording decoded. Completion/errors/levels were spoken. Twelve seconds with stable focus produced no unsolicited speech or meter beeps. `nvda5/`. |
| Installer lifecycle | Installed into a fresh private per-user directory; exercised installed CLI, MCP and recording; quit while recording and decoded the finalized file. All owned workers exited. Uninstall removed the app, automation, shortcuts and registration. Installed, portable and ZIP executables had identical SHA-256 hashes. `installer071/`. |

The hardware files measured approximately 30/60 fps for Auto and software,
29.63/59.27–59.37 for NVENC, and 29.0/57.8 for QSV over short startup-inclusive
clips. Some hardware starts had dropped frames and intervals up to about 134 ms;
these are not claims of perfect cadence. Encode failures were zero. Windows
reported NVIDIA VideoEncode activity; QSV selected its hardware backend, although
the counter provider did not expose matching Intel VideoEncode activity. No AMD
GPU was present, and physical GPU removal/driver-reset fallback was not tested.

The default live VDO.Ninja publisher sent VP8 screen sharing at about 1 fps, which
used software decode on this configuration. The separate H.264 trace proves the
compatible hardware path; merely observing GPU composition would not prove it.

## Production endurance run

The run uses 1080p30 CPU composition, the C920 camera/microphone, a live VDO.Ninja
viewer, a transparent chat overlay with synchronized flash/tone, two scenes
switched every two minutes, independent MKV recording and NVENC RTMP publishing
to a private MediaMTX receiver. Source monitoring is disabled. It runs windowless
in an interactive Windows user session.

The hour was started on the initial candidate (0.7.0 plus the CPU readout).
Later accessibility, disk-space and asynchronous device-query fixes were verified
in separate updated release processes. This is not a claim that the final binary
completed a second hour after those changes.

The production ran for 3,600 measured seconds. The 2.88 GB MKV contains 3,616.5
seconds including setup/finalization; the whole recording and all four receiver
captures decode cleanly. It contains 106,410 video frames, averaging 29.42 fps,
with 1,119 intervals over 50 ms and a maximum interval of 500 ms across startup,
fault injection and the device-enumeration stalls described above. This is not
perfect 30 fps delivery.

The synchronized flash/tone measured a median video-minus-audio offset of
-3.33 ms in both the early and final windows (19 and 18 matched events).
No drift was measurable at this analysis resolution (33 ms video / 10 ms audio).
Individual measured offsets ranged from about -60 to +77 ms.

During the final twenty minutes, total private memory across the app and children
had a median of 1,409 MiB, range 1,386-1,431 MiB and a +0.95 MiB/minute fitted
slope. Median CPU across that process tree was 164% of one logical core, or about
8.2% of this 20-processor system; p95 was 176% of one core. These figures include
browser/device/encoder children. They do not prove absence of longer-term leaks.
Recording drops stopped increasing after frequent synchronous device scans ended;
the separate before/after recording proves the asynchronous-query improvement.
`endurance2/analysis.json` retains the full measurements.

Browser, microphone, camera, publishing-encoder and recording-encoder crashes
were injected; a twelve-second receiver outage was also injected. Recording and
publishing recovered. The first suspended-browser observation encountered a
harness race while the replacement worker had no Chromium child yet. Its failure
is retained in `endurance2/endurance.json`; the observer was corrected. A repeat
in the same ongoing production (`hang-retry/`) recovered video and audio in
15.63 seconds, with both outputs continuing throughout.

Concurrent matrix, screen-reader and resource-stress workflows overlap the first
part of the run. Resource figures must not be treated as an isolated benchmark
or a controlled comparison with previous releases.

The first physical USB removal lasted approximately 12.75 seconds and occurred
at the end of the hour. The outputs survived removal until their scheduled stop;
reconnection occurred after that stop. Camera frames resumed automatically about
5.5 seconds after device reappearance, but microphone capture stayed falsely
active and silent. The audio loop now probes an unresponsive endpoint once per
second and exits on WASAPI errors, allowing the isolated-source supervisor to
reconnect. The focused physical repeat on the final executable passed, with
recording and publishing active throughout; measurements are in the table above.
Microsoft documents device invalidation on
[GetNextPacketSize](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getnextpacketsize).
Silence alone is not treated as a capture failure.

## Gates, reproduction and limits

Release build and inherited `MainWindowTest` / `LocalControlServerTest` gates
passed, as did Python and JavaScript syntax and whitespace gates. The portable
ZIP and per-user installer were produced. The executable validated by the
installed workflow has SHA-256
`cd604563bcb5b17edeb74ca305fdf241f058e591f0bd79d8c51b789fdcd298d3`.
Only the README and validation documentation were refreshed in the packages
after that workflow. Gates are not substitutes for the application workflows
above.

Reusable entry points under `native-qt/e2e/`:

- `mixer-endurance-e2e.py` and `mixer-endurance-analysis.py`
- `mixer-hardware-matrix-e2e.py`
- `mixer-browser-extended-e2e.py` and `mixer-live-browser-recovery-e2e.py`
- `mixer-browser-audio-idle-e2e.py`
- `mixer-disk-failure-e2e.py` (requires an existing Ubuntu WSL distribution)
- `mixer-system-resources-e2e.py`
- `mixer-device-polling-e2e.py`
- `mixer-usb-recovery-e2e.py` (wait for READY before physically unplugging)
- `mixer-installed-e2e.py` (disposable per-user install and uninstall)
- `mixer-nvda-e2e.py` (a separately launched app and NVDA debug speech log)
- `mixer-automation-e2e.py` and `mixer-meters-e2e.py`
- `tools/mcp/studio-concurrency-e2e.mjs`

Use unique private report directories and the packaged executable; provide
`--mediamtx` for local receivers and `VIDEO_MIXER_E2E_VDO_URL` for a viewer the
operator is authorized to receive. Browser inspection enables loopback debugging
only in the QA child environment. Disk-full checks fill only their uniquely
created bounded tmpfs, never the host disk.

This covers the available hardware, local transport receivers and these failure
conditions. It is not a browser vulnerability audit, assistive-technology
conformance certification, hostile-network study or proof against OS/GPU-driver
crashes. Clean-machine/Windows 10 coverage, other camera drivers and a full
NVDA/JAWS/Narrator user audit remain outside this run.

## 0.7.2 interface workflow pass

The follow-up used the packaged application through Windows UI Automation,
mouse/keyboard input, its file dialogs, and visible Edge guest/receiver pages.
Studio commands were not substituted for GUI production actions. Read-only
browser instrumentation, controlled media, external receivers and process fault
injection supplied evidence. Screenshots were visually inspected and recordings
were decoded. Private evidence is under `native-qt/e2e/reports/studio-ui-071/`.

Opening a damaged show while unavailable devices retried exposed a real issue:
the device status could immediately replace the import failure message. Version
0.7.2 keeps the error in an acknowledgement dialog and confirms that the current
show was preserved. The actual failed-import workflow passed after the change.

The 4K playback check also exposed per-frame allocation/copy churn in the native
media worker. Its decode buffer is now reused, and one SIMD pass copies pixels
and normalizes the opaque byte required by Qt's CPU renderer. Full source
resolution is retained. A first optimization candidate failed the CPU pixel
check; it was corrected before the final candidate was retained.

Verified workflows on the 0.7.2 package:

| Workflow | Observed result |
| --- | --- |
| First recording | Added the physical C920 camera and microphone through the source dialog, saw the microphone meter move, recorded mute/unmute, stopped and saved the show. The recording decoded and contained the expected audible/silent intervals. |
| Direct canvas and editing | Single-canvas scene/source changes reached recorded Program. Drag/resize, keyboard positioning, crop, Fit/Fill/Stretch, opacity, layer lock, undo/redo, scene shortcuts and restoring a removed source passed. CPU/GPU crop/alpha samples matched. |
| Save/open and unavailable devices | Saved device identities and translucent colors survived a round trip. Invalid source references left the current show intact and displayed a persistent error. |
| Editing a live browser source | No-op, rename and audio-delay edits did not reload it. URL changes and explicit reconnect did. The saved audio remained continuous apart from the intentional 80 ms delay. |
| Independent stream/record | Starting/stopping recording independently of SRT publishing worked. Both recordings and the receiver file decoded with the expected tone. |
| RTMP and WHIP | Private receivers decoded 1080p60 H.264 and audible audio from each transport. This exercised RTMP, not RTMPS/TLS. |
| Mixed production | C920 camera/mic, native window, browser video and overlay mixed, recorded and switched through scenes. Smooth browser samples were 29–30 fps. Headphone mute, off-air Listen and desktop-audio monitoring exclusion passed. |
| C920 settings | Brightness and zoom changed actual pixels. Exposure/focus/white-balance Auto and manual settings worked. Repeated 1080p30, 720p30 and 480p15 changes, reconnect and process restart preserved the chosen settings. Original driver controls were restored. |
| Audio tracks | Green/amber/red levels, peak hold, clipping, track/master mute and scroll boundaries passed. Decoded recordings confirmed compression, gate and noise suppression effects. |
| Laptop workspace | Both modes remained operable at 1366×768 with distinguishable scroll regions. |
| Guests and wireless invite | Visible preflight and keyboard Start joined two controlled browser peers. Backstage, Preview/Take, re-admission, Program return and mix-minus passed. Private talkback reached both returns while Program recording had zero audio RMS. |
| Social Stream Ninja | A synthetic incoming message in the real dock was featured in Program and then cleared. The overlay retained a transparent page background. No social account was authenticated. |
| Local outputs | A separate Spout receiver obtained 60 frames, and visible Edge received 45 virtual-camera frames with 44 distinct images at 1280×720. |
| Workspace and playback | Scene create/rename/duplicate/delete, pause/restart/loop/end, five visibly different themes, four layouts with changed panel widths, 1080×1920 recording and fullscreen Program output passed. |
| Keyboard and NVDA | Keyboard-only source creation, invalid-file correction, track mute/gain, master gain, Preview/Take and recording passed. The saved recording decoded. NVDA spoke the workflow labels, and the unfocused meter sample produced no unsolicited meter speech or beeps. |
| Normal-profile preferences | Both canvas modes and French/Spanish/English menus survived process restarts. X exited with tray-on-close disabled. The original show was restored byte-for-byte. Isolated `--studio-show` sessions intentionally do not save global mode preferences. |
| Output failures | Separate publishing/recording encoder exits and a private RTMP receiver outage recovered. Scene controls remained usable. The ongoing recording and four receiver samples fully decoded with audio. |
| Media safety | A 4096×2160 source recorded successfully, corrupt media showed an error, and an attempted recording overwrite left the existing file byte-for-byte intact. |
| Capture recovery | Window resize/minimize/restore, close/reopen, repeated reconnect and a Chromium-process crash recovered while Studio remained operable. |
| Browser overlays | Background transparency survived reload/navigation, strict CSP, late CSS/body replacement and reconnect. Opaque mode retained page styling. Encoded pixels matched the backing layer and opaque overlay card. |
| Hostile browser content | Device/clipboard/screen requests, notifications, downloads, popup/file/custom-scheme escapes and native-command messages were blocked. An unauthenticated webpage command did not close Studio. A message flood and browser-host crash left a fully decodable recording. |
| Browser resource isolation | Hardware video decode and GPU composition were enabled in WebView2. A busy page reached its four-core CPU cap while main-window message latency stayed below 1.6 ms in the sample. A hung renderer recovered in 16.3 seconds. GPU crashes, retry exhaustion and a roughly 4 GiB allocation limit left the healthy browser and recording alive. |

The same 4096×2160/30 fps clip improved from median received rates of 22.15 fps
(CPU compositor) / 22.0 fps (GPU compositor) to 28.1 fps with both. This is still
below a sustained 30 fps and remains a performance limit for this workload.
The eight-sample measurements used the source's visible health readout. Total
process-tree CPU increased while more frames were delivered: 167.5% → 192.8%
with CPU composition and 176.2% → 184.9% with GPU composition, where 100% is one
logical processor. Do not describe this as a reduction in total CPU usage.
Pixel checks, large-media recording, playback controls, portrait output,
independent output lifecycles, RTMP/WHIP and encoder/network recovery were
repeated after the media change.

A five-minute mixed-show rehearsal on the final executable repeatedly faded
between scenes while recording the camera, microphone, moving browser, window
and chat overlay. Its 312.172-second recording contained 9,366 frames at
29.9995 fps; the largest timestamp gap was 37 ms (99th percentile 34 ms).
The entire file decoded successfully. Across Studio and its child processes,
the 300.344-second resource sample averaged 84.42% of one logical processor
(about 0.84 core). Private memory started at 799.77 MiB and ended at 809.17 MiB,
with a 794.18–844.12 MiB range. Main-window response samples remained at or
below 47 ms with the coarse sampling clock. This is a short rehearsal, not a
long-duration memory-leak proof. Simultaneous streaming and independent recording
were verified separately.

The final 0.7.2 executable has SHA-256
`b17008c0e94f35f26cf0e053ce92be82e012b55acdee06abbde2837062df9c71`.
The earlier 0.7.2 browser/camera/security matrix preceded the native-media
optimization; affected media/output workflows, workspace/preferences, the
rehearsal, keyboard/NVDA operation and guest/wireless browser invites were
repeated on this final executable. The browser-as-phone rerun used the actual
invite page's preflight and Start controls, as requested; it also passed
backstage, Take, return video/audio, mix-minus and private talkback isolation.
Build, Python syntax and whitespace checks are gates, not workflow testing.

The final portable, ZIP and privately installed executable matched that hash.
From the actual installation, CLI launched a headless studio, the packaged MCP
server exposed 31 tools, scene controls and a PNG Program screenshot worked,
and the saved recording fully decoded. Quit left no owned workers; uninstall
removed the private installation, automation files, shortcuts and registration.
The harness now waits for the temporary NSIS uninstaller to finish its final
shortcut/registry cleanup. This was an existing-machine install, not the
deferred clean-machine check. Only this validation document was refreshed in
the packages after that workflow.

The original show matched its baseline SHA-256 after restoration, the settings
registry export matched its backup byte-for-byte, and the original Game Capture
checkout retained its baseline HEAD and pre-existing untracked note.

Reusable additions under `native-qt/e2e/` include
`mixer-ui-workflows-e2e.py`, `mixer-first-show-e2e.py`,
`mixer-workspace-e2e.py`, `mixer-output-recovery-ui-e2e.py`,
`mixer-preferences-ui-e2e.py`, `mixer-media-performance-ui-e2e.py` and
`mixer-nvda-ui-runner.py`. For example, from `native-qt`, using a Python
environment with pywinauto, psutil, Pillow and NumPy:

```powershell
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/my-ui-run first-show workspace
python -B e2e/mixer-ui-workflows-e2e.py --report e2e/reports/my-output-run --mediamtx C:/path/to/mediamtx.exe outputs-050 protocols-050 output-recovery-ui
```

Use a fresh report directory. Guest/browser workflows additionally require
Playwright and Edge; microphone/camera workflows expect the available C920.
`STUDIO_UI_REHEARSAL_SECONDS=300` extends the `production` workflow.
The runner drives visible controls, captures screenshots and verifies worker
cleanup. On failure it leaves the owned app open for inspection.

Guest inputs here are synthetic browser peers, not a real phone or remote human.
Window tests use a native moving fixture, not an exclusive-fullscreen game.
Actual mobile orientation/backgrounding, authenticated social delivery,
multi-monitor/mixed-DPI hardware and the deferred clean-machine pass remain
unverified. Physical USB removal was not repeated; the earlier final-0.7.1
physical recovery evidence above remains the applicable result.
