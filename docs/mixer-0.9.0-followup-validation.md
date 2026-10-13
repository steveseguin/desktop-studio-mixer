# Desktop Studio 0.9.0: additional browser audio and recovery testing

This follow-up ran the packaged Windows app and verified actual recorded media,
private receiver playback, and visible browser controls. Reports and recordings
remain private under `native-qt/e2e/reports/studio-090-additional/`.
The later recording-recovery pass below uses `studio-090-recovery-depth/`.

## Browser audio defect found and fixed

On the original 0.9.0 executable
(`ab185f5d76a551fd991b0c36ad5436b94a7a80842190724efcad41aa99e0e5db`),
replacing the audio track inside an existing `MediaStream` could leave the
removed track audible. Changing an element from a live stream back to a file
could also leave the old live stream connected and omit the file's audio.
Recorded 440/880 Hz fixtures reproduced both failures; this was an actual audio
routing failure, not just an incorrect meter.

The audio bridge now compares track membership, rebuilds the live-stream tap
when it changes, disconnects obsolete taps/listeners, and restores automatically
captured file audio after `srcObject` is cleared. It preserves audio graphs
created by the page. Empty streams stay silent and resume when a track is added.

The first audio-corrected packaged executable has SHA-256
`38495cb5f658eab8497070aa6b037963731815a21c47ebd90749c4ddba8e9ce3`.
The final packaged executable for that browser pass also corrects the recovery notice described below:
`4fb5c3d2b1caa5783c1b6e1f3f753a32e1bb1438e6f4db5cbb10f704b5fa0598`.
The subsequent recording-recovery build is identified below; 0.9.0 remains unreleased.

## Recovery notice found during visual inspection

The browser picture recovered after its host was killed, but the bottom status
bar still said "Reconnecting automatically". A replacement browser proxy need
not emit a recovery signal because it begins without an error. The app now
retires that source's previous warning when usable video returns, preserving
any newer unrelated status message. Obsolete capture callbacks are ignored.
The visible workflow now checks this notice, and the final screenshot shows
"Browser video resumed" while the recovered picture and audio meters are active.

## Completed workflows

| Workflow | Actual result |
|---|---|
| Browser audio switching | Twenty separately recorded phases passed full decoding and tone/silence checks: file playback, pause, mute, file replacement, file-to-stream, stream replacement, track replacement/removal/addition, stream-to-file, page-owned audio graphs, targeted disconnect/reconnect, context close, cross-origin iframe playback/removal, navigation and playback afterward. The native browser worker stayed the same; default transparency survived navigation. |
| Guest regression | Four, six and eight private VDO.Ninja guests received video and mix-minus. At eight guests, own-tone measurements were approximately -91 to -104 dB and another guest's tone approximately -39 dB. Recording decoded through Private break, backstage/readmit and a peer leaving. |
| Visible browser security | Camera, microphone, screen, clipboard and notification requests were denied; host objects, file/custom navigation, popups/downloads and malformed privileged messages were blocked/ignored. The final build's eight-second message flood had a maximum observed UI response of 51.89 ms and 0.12 MiB parent RSS growth. Killing the browser host recovered while recording continued; its recording fully decoded and the recovery notice cleared. This is bounded fixture coverage, not proof against all browser vulnerabilities. |
| Visible chat interaction | Open / Invite opened the chat dock on its first click; closing and reopening it three times passed. |
| Suspended browser audio | An AudioContext stayed silent for 15 seconds and then resumed with the same browser worker. Recording stayed active and fully decoded with the expected tone before/after silence. |
| Abrupt app termination | Killed only the owned app while Program, a named audio track and a separate source video were recording. All captured descendants exited. Its Unicode-named autosave/media reopened, a stale owner lock was recovered, and outputs stayed off after restart. Both interrupted MKVs exported through the app to MP4 and fully decoded with the expected tone. A new recording after restart also decoded. |
| Damaged project and backup | A malformed active show was preserved byte-for-byte in a recovery file. The app created a usable default show and subsequently opened the rolling backup as its active project. |
| Low disk and actual disk-full | Used a disposable 256 MiB WSL tmpfs, without filling the Windows drive. Program recording stopped at the 128 MB guard while RTMP reception continued. A separate file publisher reached real ENOSPC and reported an error while independent Program recording survived. Surviving recordings/receiver output decoded; the temporary mount was removed. |
| Installed application | Installed into a private QA directory, exercised packaged CLI/MCP control, captured twelve distinct portrait live screenshots and recorded moving video. The recording fully decoded; its maximum frame interval during screenshots was 47 ms. All owned descendants exited, installed/ZIP/portable executables matched, and uninstall removed the app, automation, shortcuts and installation registration. |

Disk and abrupt-crash workflows used the original executable. Guest, chat and
suspended-audio regressions used the first audio-corrected executable; browser
audio switching and visible security/recovery were repeated on the final
executable. The native recording/project implementation did not change in that
browser pass; the subsequent recording changes are described below. Neither
pass constitutes another hour-long soak.

The installer workflow above used the browser-recovery executable. Its documentation was
packaged with the same tested runtime, verified against SHA-256 manifests. No
GitHub release was published. The saved user show and original Game Capture
checkout were unchanged, registry preferences were restored byte-for-byte, and
no test app processes remained.

## Limits and reproduction

This fix does not establish support for every protected or CORS-restricted
browser audio graph. It does not resolve the transparent snapshot frame-rate
limit or the WebView2 memory growth measured in the
[earlier rehearsal](mixer-0.9.0-validation.md). The eight-guest color differences
also remain; the panel check verifies picture identity/placement, not accurate
color reproduction. Clean-machine/Windows 10 and external hardware/account
checks remain as previously listed.

New workflows: `mixer-browser-audio-switching-e2e.py` and
`mixer-crash-project-recovery-e2e.py`. Existing workflows used here include
`mixer-disk-failure-e2e.py`, `mixer-panel-080-e2e.py`, and the visible
`mixer-ui-workflows-e2e.py` cases `browser-security` and `browser-interaction`.
Installer coverage uses `mixer-installed-e2e.py`.
Build, JavaScript/Python syntax, package hashes and whitespace checks are gates,
separate from this actual app testing.

## Recording recovery and combined-load follow-up

The original browser-recovery build (`4fb5c3d2...`) recovered a one-minute
recording after abrupt termination, but its last video timestamp was 58.215 s.
Four separate source recordings ended at 58.225 s. All files decoded and all
four selected audio tones survived MP4 export, but roughly 1.8 seconds remained
inside the muxers when the processes were killed.

Recording now uses a 250 ms MKV cluster limit alongside packet flushing. It
does not change encoding quality, bitrate or frame rate. On the corrected build
(`d5fc7ac0c90edcbaa34b428bf0a4195967c92d50b0b78bcaa6a4b98e7b2ebbbc`),
the same one-minute scenario preserved video through 59.943 s in Program and
59.899-59.920 s in the four source recordings. All five files and all selected
audio tracks decoded after export. These measured endings are not a universal
maximum-loss guarantee; this does not force Windows or drive caches to disk.

| Abrupt-exit scenario | Observed result after shorter clusters |
|---|---|
| About 0.1 s after Recording appeared | No file contained a video frame. Starting the encoder/writer does not mean a frame has reached the recording. |
| About 0.7 s | All five MKVs decoded, through approximately 0.679-0.702 s. |
| About 2.3 s | All five MKVs decoded, through approximately 2.218-2.241 s. |
| About 5.5 s | All five MKVs decoded, through approximately 5.220-5.433 s. |
| About 60 s | All five MKVs decoded and exported; named audio track identities and expected tones survived. Every captured descendant exited after its parent was killed, and outputs remained off after reopening the show. |

The final executable adds playback guidance and has SHA-256
`63c42bd1d6585b69825eab07e39d82c4853f3281bc659391f8d432ba9a41f7eb`.
Using its actual interface, the interrupted MKV played picture and audio but
reported no duration. The Playback dialog now explains **Recordings > Export
recording as MP4...** and disables seeking until a duration is available.
The visible export workflow preserved the original file, decoded all exported
tracks, played the MP4 with audio, sought to its middle and near its end with
the keyboard, and resumed playback.

The export fault workflow suspended and killed an export worker after it wrote
about 6 MiB of partial MP4. Independent Program recording kept advancing and
fully decoded afterward; the app rejected overwriting the partial MP4, and
retrying with a new filename succeeded. Malformed input reported an error.
Killing the whole app during another partial export also terminated its worker,
preserved the original MKVs, and allowed a successful export after reopening.
The maximum sampled API response during the suspended export was 31.42 ms.

The 1080x1920 portrait/60 fps combined workflow sustained Program plus 16 named
audio tracks, four separate source videos, browser audio and hardware RTMP to
a private receiver. All five recordings and both receiver captures decoded;
all 16 source tones were present. It produced 4,768 Program video packets,
including setup/receiver capture time, with a maximum timestamp interval of
33 ms. The recording drop counter stayed at its startup value of 28. The
approximately 63-second sampled interval used 4.07 logical cores across the
entire tree; API p95/max were 30.46/32.18 ms. This measures output cadence,
not 60 distinct frames from source media recorded at 30 fps.

The 720p30 maximum-track workload ran for 600 seconds after warm-up, with the
same five recording files, 17 Program-file audio streams and private hardware
RTMP. All five recordings and both receiver captures decoded. All 16 selected
source tones survived, including browser audio; an interior sample of Private
break was silent across all 21 audio streams (17 in Program plus four source
files). There were 18,559 Program video packets including setup/receiver time,
with a maximum timestamp interval of 50 ms, no worker restarts and no increase
from the startup recording-drop count of 13.

Total CPU averaged 2.43 logical cores during the ten-minute sampled interval.
The earlier 63-second baseline was 2.35 cores. Matching short windows initially
showed 2.34 versus 2.65 cores; a later three-minute corrected-build interval was
2.35 cores. These are sequential observations, not an isolated benchmark proving
zero overhead. A ten-second process sample attributed about 0.68 cores to
Program's 17-track AAC writer and 0.30 cores to the main app. Total private memory
grew 97.67 MiB and handles grew by 241; main private memory grew 12.89 MiB.
Memory fluctuated after warm-up, but this does not establish leak-free operation.
The first offline privacy check failed because the harness supplied `.5` instead
of `0.5` to FFmpeg; correcting that argument and analyzing the retained media
completed verification. Live API latency samples were not persisted before that
harness error and are not claimed for the ten-minute run.

A further finalization case suspended the Program writer, requested Stop,
confirmed finalization was still busy, and terminated the app. All five MKVs
decoded afterward, and no captured descendants survived. This checks a blocked
writer during shutdown, not physical drive removal.

Large-file coverage generated a 4,985,796,308-byte MKV by repeating recovered
media, then used the app to export a 4,992,205,131-byte MP4 in 13.34 seconds.
Both containers played in the app, sought to about 89:57 (beyond the 4 GiB file
boundary), and resumed with picture/audio. Beginning, middle and end samples
decoded all tracks. Source hashes stayed unchanged; the two large fixtures were
removed after verification, retaining hashes and near-end screenshots. This was
large-file export/playback coverage, not a 90-minute live recording rehearsal or
a complete decode of both large files.

The final executable passed a fresh installation, packaged CLI/MCP control,
twelve distinct portrait screenshots during recording, full recording decode
and uninstall. The maximum frame interval during screenshots was 52 ms.
Installed, portable and ZIP executable hashes matched; all owned descendants
exited. Final documentation is repackaged with the same tested runtime and
checked against its hash manifest. User preferences were restored byte-for-byte;
the saved user show and original Game Capture checkout were unchanged.

Remaining checks include actual power loss/OS failure, storage disconnection,
longer maximum-load rehearsals, remote phones/guests and physical lip-sync,
authenticated provider/chat accounts, other GPUs and audio devices, external
virtual-camera consumers, lock/sleep/RDP transitions, and the deferred clean
Windows 10/machine installation. Software process termination is not equivalent
to losing power or disconnecting a disk.

Reproduction: `mixer-crash-timing-e2e.py`, `mixer-export-failure-e2e.py`,
`mixer-combined-recording-e2e.py`, `mixer-large-file-recovery-e2e.py`, and the visible runner's
`recovery-playback-ui` workflow. `STUDIO_RECOVERY_INPUT` selects an interrupted
MKV; `STUDIO_COMBINED_SECONDS` and `STUDIO_COMBINED_FORMAT` control the bounded
combined workload. `STUDIO_CRASH_AGES` selects crash timings and
`STUDIO_CRASH_DURING_STOP=1` exercises interrupted finalization.

## Fault quality and diagnostic retention

The next pass uses private evidence under
`native-qt/e2e/reports/studio-090-fault-quality/`. It adds two packaged-app
workflows, `mixer-diagnostics-fault-e2e.py` and `mixer-fault-quality-e2e.py`.

Review found that restarting truncated the main diagnostic log, and repeated
concurrent instances accumulated UUID-named logs. Logs now rotate on startup
and at 5 MiB, keeping three archives per independently locked instance slot.
Slots are reused after their owner exits. Native crash reports also preserve
the last log beside the JSON/minidump. The exception handler no longer enters
the logger, whose mutex could belong to the faulting thread. Worker starts and
failures now identify the component and PID; failures are logged once per
worker without source URLs or command payloads.

The diagnostics workflow records moving video and a known audio tone, then
injects an actual unhandled access violation into its owned packaged process.
It verifies the exception code, JSON, minidump, crash-log snapshot, complete
decode of the interrupted MKV, descendant cleanup, and a fresh recording after
restart. Five further launches rotate near-limit closed logs while checking
that the crash snapshot remains unchanged. A synthetic publishing credential
must be absent from logs and the issue report. Blocking the log directory with
a regular file must still allow launching, recording and decoding media.
These checks do not establish that crash dumps contain no private process data.

The quality workflow records Program plus separate media/browser audio tracks
while publishing hardware-encoded RTMP through a private loopback proxy. Its
moving source contains binary frame numbers and audio pulses whose frequencies
identify their matching source second. Analysis uses decoded video/audio PTS
and source identities, measuring repeated and skipped source pictures as well
as container timing; it cannot mistake a neighboring one-second pulse for the
correct sync match. It exercises 24 busy JavaScript workers, browser process
termination, browser suspension, 35 seconds at approximately 256 kbit/s,
bandwidth restoration without a fixture socket reset, publisher termination,
and simultaneous browser termination/publisher suspension. Each fault must
leave the independent media worker and recording alive. Received clips are
decoded and checked against a current Program screenshot for stale backlog.

The initial quality run on executable
`519dc0a43ae4b33dc9ca1774041803124d594265fafb47cbd3d99b6c9ee2018f`
recorded 6,699 video frames at 30.001 fps. Local maximum A/V offset was 26 ms;
the maximum timestamp gap was 58 ms and the longest repeated picture was
92 ms. Four receiver clips decoded with maximum measured offsets of 19.5 to
36.2 ms. Recording drops stayed at the startup count of 13, browser audio was
present before/after recovery, and the unaffected media worker never restarted.
API p95/maximum was 27.6/38.6 ms. Total private memory peaked at 940.9 MiB
during deliberate browser overload and fell after the browser was replaced.
This is a short containment measurement, not a long-term leak clearance.

Process CPU samples in that run attributed about 0.12 logical cores to the
main app at baseline and 0.21 during the busy-browser interval. The browser
runtime used approximately four logical cores during intentional overload,
consistent with its existing per-source budget. Both hardware encoders together
used approximately 0.29/0.32 cores. These are observations of this workload,
not an optimization benchmark or a promise for other machines. Newly started
processes contribute only after their first sample.

The harness initially supplied an unsupported RTSP `rw_timeout` input option;
using the RTSP-specific `timeout` option allowed the complete run. The first
diagnostics attempt expected HTTP 200 for issue-report creation; the API
correctly returned 201. Both fixture mistakes were corrected before the
successful runs above.

The final executable adds the worker failure records:
`caf831dd7a2bb24b17f7d44610cadabcc02ea4b5f233b2a2ae1a22af7d30a4b3`.
Its repeated quality run recorded 6,711 frames at 29.999 fps. Program and the
separate clock audio track independently measured 15 ms p95 and 17 ms maximum
absolute A/V offset, with no unmatched pulses. The largest timestamp gap was
52 ms, and the longest repeated picture was 78 ms. There were 17 repeated and
14 skipped source pictures in the measured interval, so this is not a claim
of zero picture loss. Recording queue drops remained at the startup count of
nine throughout all faults. Four received clips decoded with maximum A/V
offsets of 15.2-27 ms. Their last pictures were 200-233 ms behind the current
Program screenshot; the restored stream was not playing a growing old backlog.

API p95/maximum was 26.9/31.9 ms. The main process averaged 0.21 logical cores
both at baseline and during browser CPU pressure; the deliberately busy browser
runtime averaged 3.99 cores. Total private memory was 634.6 MiB initially,
peaked at 940.3 MiB, and ended at 647.2 MiB. At the half-second sampling cadence,
replacement browser processes first appeared after 2.14 seconds for termination
and 12.45 seconds for suspension. A publisher replacement appeared after
1.09 seconds for termination; during the simultaneous fault, browser/publisher
replacements appeared after 2.20/5.45 seconds. These are process-replacement
observations, not first-decoded-frame recovery latencies. The failure log
contained exactly the five injected worker failures, with component, PID and
reason; it did not repeat warnings on every media/status poll.

The final executable also passed the access-violation/recording/rotation/log
failure workflow again. Concurrent visible instances retained separate logs,
reused released default and secondary slots, and archived the secondary
instance's preceding session. That older fixture first tried opening the same
show in both instances; the app correctly refused the second open. Giving each
instance its own show completed the intended log-isolation workflow. Opening
the default show in that old fixture had also serialized newer default fields;
cleanup detected the changed hash and restored the exact original bytes from
the app's verified backup. The fixture now explicitly selects separate shows
and checks the user's default-show hash. User preferences were restored
byte-for-byte, and the original Game Capture checkout was unchanged. A fresh
installation passed CLI/MCP control, portrait screenshots during recording,
media decode and uninstall. Portable, ZIP and installed executable hashes
matched, and owned children exited. Build, native crash-reporter and whitespace
checks are gates; the packaged workflows above are the actual testing.

The next production checks should prioritize long repeated-failure rehearsals
with memory/handle/GPU-memory trends; SRT/WHIP/guest packet loss, reordering and
variable delay; long-run physical lip-sync across independent device clocks;
slow/disconnected/network storage; and real GPU/device/reset, sleep and RDP
transitions. Maximum retained crash-artifact storage, worker minidumps and
broader secret-redaction cases also remain follow-up work. The clean-machine
check remains deferred. No software fault injection proves power-loss safety.
