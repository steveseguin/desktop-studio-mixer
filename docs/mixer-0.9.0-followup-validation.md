# Desktop Studio 0.9.0: additional browser audio and recovery testing

This follow-up ran the packaged Windows app and verified actual recorded media,
private receiver playback, and visible browser controls. Reports and recordings
remain private under `native-qt/e2e/reports/studio-090-additional/`.

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
The final packaged executable also corrects the recovery notice described below:
`4fb5c3d2b1caa5783c1b6e1f3f753a32e1bb1438e6f4db5cbb10f704b5fa0598`.
This supersedes the original executable for the still-unreleased 0.9.0 build.

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
executable. The native recording/project implementation did not change in this
follow-up. This pass does not constitute another hour-long soak.

The installer workflow used the final executable. The updated documentation is
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
