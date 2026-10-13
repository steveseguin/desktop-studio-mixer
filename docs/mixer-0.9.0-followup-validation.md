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
