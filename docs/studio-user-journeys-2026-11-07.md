# First launch, recording recovery, live output and anonymous chat

Completed 2026-11-07 UTC in `C:\Users\Steve\code\video-mixer-chat-integration`, on branch `feature/studio-chat-integration`. The user's main checkout was left untouched.

The final development portable is `native-qt/dist/desktop-studio-journeys-nov7/desktop-studio.exe`, SHA-256 **`9984300df07ed23ba5ed165ea13253317daabf83c982c08487e20259b2236774`**. It is timestamp-signed using the local Social Stream Ninja certificate; Windows trust remains `UnknownError`. Runtime files and SSN 3.50.17 are unchanged from the preceding package and hard-linked; the executable and documentation are independent files. This pass did not rebuild the installer or ZIP.

## What changed

- An interrupted recording previously required the user to find the MKV and discover the export command. Each show now journals recording paths before writing starts, and reopening it offers **Recover recording...**. Export creates a new MP4, preserves the MKV, and clears the reminder after success. Cancel keeps the reminder. The last recording folder survives restart. A failed writer retains its recovery entry.
- Changing the active show while recording could detach the recording from that journal. **Save As active show...** now explains that recording must stop first and points to **Save a copy**, which remains available.
- A missing camera previously became apparent only after submission. The picker now explains it immediately and offers **Refresh devices**, preserving entered settings. Irrelevant source fields are hidden.
- New browser video sources default to opaque capture at up to 30 fps. Transparent overlays remain an explicit option capped at 15 fps; existing sources retain their mode.
- The output page no longer repeats the **Desktop Studio Live** heading. Its description identifies Program and mixed audio when Mixer program is selected. **Audio encoding settings** makes clear that the checkbox expands settings. The preceding compact toolbar, icons and action colors remain in place.
- `VIDEO_MIXER_DATA_DIR` now redirects the default show without being treated as an explicit `--studio-show`. This makes isolated normal launches persist preferences like ordinary launches; it was an isolation-fidelity issue, not evidence that normal users' preferences were broken.

## Actual packaged application workflows

These were visible application workflows using pointer input, keyboard input and Windows UI Automation. Local control reads verified outcomes; actual saved media and screenshots verified output. Release compilation and whitespace/syntax checks are separate gates, not application testing.

The fresh sender profile was `native-qt/build-integration/user-journeys/20261107-104540/` (called `J` below). It launched normally with an isolated data directory and **without** `--studio-show`. No account was signed into this profile. A second packaged Studio instance received the actual VDO.Ninja viewing page in a browser source and recorded it independently.

Completed runs passed **138 checks** on the final executable. Failed harness attempts are retained separately below and are not included in that total.

| Journey | Passed | Evidence relative to J |
| --- | ---: | --- |
| Fresh launch, missing camera/refresh, new scenes, imported moving media with audio, private Preview and Take | 12 | `create-show/` |
| Normal recording, active-show guard, forced termination, reopening and MP4 recovery | 14 | `record-crash-20261107-104616/` |
| Open recovered video, seek to ten seconds, resume with audio, normal restart | 6 | `recovered-playback/` |
| Add real Twitch/YouTube channels anonymously and GO LIVE to VDO.Ninja | 3 | `live/start-results.json` |
| Separate receiver gets video/audio and starts its own recording | 4 | `receiver/receive/` |
| Select privately, feature, receive remotely and hide an actual live Twitch comment | 5 | `featured-twitch-20261107-105143/` |
| The same workflow with an actual live YouTube comment | 5 | `featured-youtube-20261107-105153/` |
| Finalize receiver recording and STOP publication | 2 | `live/stop-results.json` |
| Windows desktop-audio capture, record, mute, unmute and inspect saved audio | 6 | `desktop-audio/` |
| Comment privacy, malformed input, Unicode/long text, layout, scenes/sources, keyboard navigation and restart | 77 | `regression/workflows-20261107-105651-565/` |
| Restore the owner's existing Twitch session, return to chat, preserve channels and keep output idle | 4 | Separate owner profile: `native-qt/build-integration/ui-exploration/20261106-085441/saved-twitch-20261107-105823/` |

The crash journey deliberately force-terminated only the owned QA application while recording. Its workers exited, scenes/source identities and Studio mode returned, and neither recording nor streaming restarted unexpectedly. The recovery reminder survived cancellation, exported the interrupted recording, and remained dismissed after another normal restart. The original MKV's hash was unchanged. This simulates abrupt process death; it does not reproduce a native exception or power failure.

The VDO.Ninja publication used a random unlisted stream ID, no password and synthetic moving video/audio. The first-use firewall notice was acknowledged in the interface; no firewall rule was modified. The encoder reported **H264 Encoder MFT (Hardware)** at 1280x720, 30 fps, 3 Mbps. The receiver was another process on the same Windows PC, using VDO.Ninja signaling and its real viewing page. This does not qualify separate-network connectivity or packet-loss behavior.

Twitch `esl_dota2` was confirmed broadcasting **LIVE: Xipto vs PlayTime - DreamLeague Division 2 Season 5** using Twitch's public frontend metadata at 15:51:13 UTC. YouTube `@LofiGirl` resolved to video `4t4AQ15yj1M`, **christmas lofi radio — cozy ambience to relax to**, with `isLiveNow: true` at 15:51:17 UTC. Metadata is retained in `live/*-broadcast-state.json`. Both featured comments came from their actual feeds, with practice disabled. The exact selected comment was checked in state, then visually matched on the separate receiver. No messages were posted and no credentials were entered.

Earlier investigation found that Twitch chat can remain active while its broadcast is offline. An offline channel was not accepted as proof of the requested live-broadcast journey. A subsequently connected live but quiet channel was replaced through the UI with `esl_dota2` before the completed feature run.

## Saved media and resource measurements

The reference video has moving patterns, a 100 ms white flash and an 880 Hz audio pulse once per second. FFmpeg decoded all streams in each saved file without errors. Frame and pulse analysis inspected the actual recordings, not just application counters.

| File | Recorded video | Frames | Observed median audio minus flash | Maximum absolute pulse offset |
| --- | ---: | ---: | ---: | ---: |
| Normal MKV | 22.1 s | 664 | approximately 0 ms | approximately 0 ms |
| Interrupted MKV | 22.2 s | 667 | -13 ms | 13 ms |
| Recovered MP4 | 22.2 s | 667 | -13 ms | 13 ms |
| Independently received VDO.Ninja MKV | 315.1 s | 9,453 | -13 ms | 57 ms |

All four files had a maximum frame timestamp gap of approximately 34 ms and no gaps over 100 ms. This does not by itself prove there were no repeated pictures. Periodic pulse matching measures observed alignment, not absolute stream latency or a guarantee against whole-second timing errors. The recovered file also played, paused, sought and resumed inside the packaged app.

Desktop audio used a quiet 660 Hz tone played by a separate Windows SoundPlayer process, with the media tracks muted. The saved recording measured **-36.3 dB** before mute, **-91 dB** while muted and **-36.4 dB** after unmute. This verifies actual Windows loopback capture and the saved result, not physical microphone capture. The tone player was closed after the journey.

The PC has an Intel Core Ultra 7 265K with **20 logical processors**. CPU is the sum of Studio and its descendant workers divided by total host capacity. The receiver, tone player and automation are excluded from that sum. Memory is summed private process memory, not GPU memory. Samples were taken about once per second; they include workflow interaction and short startup transients.

| Scenario | Samples | Mean host CPU | p95 host CPU | Peak private memory | p95 local-control response |
| --- | ---: | ---: | ---: | ---: | ---: |
| Local recording before adding chat | 44 | 2.24% | 2.95% | 277 MiB | 31 ms |
| Live VDO.Ninja output with two chat channels | 327 | 3.96% | 5.47% | 1,643 MiB | 32 ms |
| Desktop-audio recording with two chat channels | 16 | 3.71% | 4.61% | 1,395 MiB | 32 ms |

The recording rows include only samples reporting recording active; the stream row includes every valid sample reporting output live. A leftover inspection helper briefly mislabeled 45 live samples as a build phase; no build ran then, and these samples are included in the live result. `metrics/samples.json` and `metrics/summary.json` retain the raw data and aggregates. CPU was low on this machine; chat memory remains substantial. This short run does not resolve earlier unexplained memory growth or qualify a long soak.

## Visual review and limitations

Reviewed screenshots of the fresh workspace, camera dialog, recovery reminder and export, recovered-file playback, output configuration, private comment selection, on-air cards, received cards and desktop-audio recording. The general walkthrough also captured 1024x768, 1280x720, 1280x900 and 1920x1080. The toolbar wraps at narrow widths; lower source/audio controls remain reachable by scrolling and keyboard focus. The recovery action is visible below the command bar. Both received comment cards were legible and matched the chosen comments. Long private comments wrap and scroll.

Useful screenshots under J:

- `create-show/camera-no-device.png`
- `record-crash-20261107-104616/after-relaunch.png`
- `recovered-playback/recovered-frame-at-ten-seconds.png`
- `live/destination-configured.png`
- `featured-twitch-20261107-105143/received.png`
- `featured-youtube-20261107-105153/received.png`
- `regression/workflows-20261107-105651-565/14-layout-1024x768.png`

No physical camera was detected, and the only audio input enumerated was **Stereo Mix (Realtek(R) Audio)**. Windows device enumeration also showed no camera. The no-device/refresh path was exercised; physical camera picture, microphone sound and hotplug remain untested. VDO.Ninja was chosen for this pass; Meshcast was not exercised. Installer behavior, a clean machine, long recording/memory soak, real broadcast rollover, gifting, other platforms and actual screen-reader operation were not requalified.

The first final-build Twitch-feature attempt, `featured-twitch-20261107-105035/`, had two failed assertions: the harness compared the on-air object to a message-ID string, waited through the auto-hide timer and then could not find Keep up. The corrected comparison uses `on_air.id`; both complete feature journeys passed afterward. Earlier intermediate builds exposed two other harness assumptions: hidden Qt controls may disappear from UI Automation, and first-use publication has a firewall notice. Both were corrected before the final full run. These failed attempts remain in ignored evidence; they are not silently counted as passes.

The owner's original QA profile and connected channels were restored on the final package. Twitch's actual account menu exposed **Log Out**, and returning to chat preserved the channels without starting output. No browser storage was cleared or account credentials/cookies read. Studio was left open and idle. Profile state, recorded media, public usernames/comments, viewing URLs and control credentials remain git-ignored.

## Repeating the journeys

Use a new disposable data directory and session file containing `exe`, `out`, `pid` and `control` (the discovery-file path). Launch the packaged executable with `VIDEO_MIXER_DATA_DIR` set to that directory, `--local-control-port=0` and `--local-control-discovery=<directory>/control.json`. Omit `--studio-show` for the normal-launch persistence journey. Create `<directory>/metrics` before starting the recording helpers. Never point these scripts at a production session: the crash journey deliberately terminates its selected application.

Run the following through `powershell -NoProfile -ExecutionPolicy Bypass -File native-qt/tools/<script> -SessionFile <session.json>`:

1. `studio-journey-fixture.ps1` creates the moving video/audio reference.
2. `studio-first-run-journey.ps1` expects a fresh show, no camera and no channels; it creates the scenes and media source.
3. `studio-recording-recovery-journey.ps1` records, terminates, relaunches and recovers.
4. `studio-recovered-playback-journey.ps1` plays and seeks the recovered file, then restarts normally.
5. `studio-publish-journey.ps1` adds anonymous Twitch/YouTube channels and publishes a random VDO.Ninja stream. Its initial example channels are not a promise of current broadcast activity. Verify live status and replace a quiet/offline channel through the UI.
6. In a separate isolated packaged instance, add the generated viewing URL as a browser source, verify received picture/audio and record it. Pass its session plus an actual current message ID to `studio-feature-live-comment.ps1 -ReceiverSessionFile <receiver.json> -MessageId <id>` for each platform. Stop the receiver recording and sender publication through their visible controls afterward.
7. `studio-desktop-audio-journey.ps1` runs after publication stops; it adds a loopback source, plays a quiet external tone and verifies the recorded mute/unmute result.

`studio-journey-metrics.py --session <session.json> --output <directory>/metrics` follows the app across relaunches and stops when `<directory>/metrics/stop` exists. It requires Python and psutil. `studio-journey-media.py --ffmpeg <packaged-ffmpeg.exe> <files...>` requires NumPy and writes decode logs and pulse/frame analyses. These helpers do not replace visual inspection or independently verifying that the chosen channel is broadcasting. Run `studio-ui-workflows.ps1` in another fresh isolated profile for its general practice-chat walkthrough.
