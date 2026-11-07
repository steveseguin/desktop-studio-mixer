# Recording, chat recovery, hardware and installer reliability

Validated on 2026-11-07 UTC in `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`. The two-hour recording/publication, local BRIO recording, failure recovery and installer workflows completed. Frame pacing and chat memory growth remain concerns. The main checkout's edits are preserved.

## Builds and changes

| Artifact | SHA-256 |
| --- | --- |
| Soak candidate, `native-qt/dist/desktop-studio-reliability-nov7/desktop-studio.exe` | `b9f7c6822cb2e1e34934dcb6967ed411a3ff7f54eb3069c2ebfe3c19a18eb8cf` |
| Final portable, `native-qt/dist/desktop-studio-reliability-final-nov7/desktop-studio.exe` | `3627893f203d73b35b5048c4a210cf00d9e5f1da2cbdfa821869119edb9b9187` |
| Final installer, `native-qt/dist/desktop-studio-0.9.4-reliability-final-nov7-setup.exe` | `1a79953c9de9f705bb2371d2dd185b12ecc8dd8a79e7c6db4fdcc0d5fd12a6f0` |

The candidate disables the chat browser inspector's unused response-body cache. Network inspection stays enabled for the existing relay block list, with total and per-resource body-buffer limits set to zero. The app never reads those cached bodies. This removes duplicate retention rather than lowering the chat worker's 3 GiB memory ceiling. [Chromium's inspector implementation](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/inspector/inspector_network_agent.cc) and the [Network protocol](https://chromedevtools.github.io/devtools-protocol/tot/Network/) explain these options; no particular upstream default is assumed for the installed WebView2 version.

The final portable adds two UI fixes found during the hardware journey: camera controls now have meaningful accessibility names, including distinct Auto/Reset actions; the source footer says **Reconnect requested** instead of indefinitely implying **Reconnecting**. The final executable is different from the one running the soak. Its general UI, hardware, writer-failure and support-event workflows were repeated separately; a candidate soak is not an exact-final-executable soak.

Both executables and the installer are timestamp-signed with the local Social Stream Ninja certificate. Windows trust is `UnknownError`; this is a development package, not a publicly trusted release. Runtime files and SSN 3.50.17 remain unchanged. The portable runtime files are hard links to existing packages; executable/docs are independent. Pack corruption checks must use independently installed assets, never those shared hard links.

## Completed packaged workflows

Evidence root `R` is `native-qt/build-integration/reliability/20261107-114311/`. UI workflows use pointer input, keyboard input and Windows UI Automation. Local control reads verify state; saved media and screenshots verify output. Compilation, whitespace, syntax and the staged secret scan are separate gates, not application testing.

| Executable | Workflow | Checks | Evidence relative to R |
| --- | --- | ---: | --- |
| Candidate | Reference recording and finalization | 2 | `candidate/recording-probe/` |
| Candidate | VDO.Ninja publication and simultaneous Program recording | 2 | `candidate/soak/start-results.json` |
| Candidate | Separate receiver gets picture/audio and records | 5 | `candidate/receiver/receive/results.json` |
| Candidate | Chat-worker death, unresponsive worker and network-service death during recording/publication | 14 | `candidate/worker-recovery-20261107-115441/` |
| Final | General UI, malformed input, privacy, scenes, source editing, keyboard navigation and restart | 77 | `final-regression/workflows-20261107-122832-440/` |
| Final | BRIO camera, camera settings accessibility, microphone, mute/unmute, recording and playback | 13 | `hardware-final-complete/hardware/` |
| Final | Reopen the saved hardware show and record camera/microphone for two minutes | 4 | `hardware-final-complete/performance-complete/` |
| Final | Failed recording writer, visible recovery, preserved original and next recording | 10 | `writer-final-package/writer-failure/` |
| Final | Support-event preview/output, three styles, literal metadata and gift streaks | 77 | `final-regression/support-20261107-123354-214/` |
| Final | Anonymous Twitch sign-in form, small window, repeated reload and return-to-chat focus | 14 | `final-regression/account-navigation-20261107-125301/` |

After the installer work, the owner's existing profile was restored on the final portable with all seven scenes, eight sources and four channels preserved (four checks). The real Twitch account menu still exposed **Log Out**, and returning to chat preserved channels and idle output (four more checks). No credentials or browser storage were read. These results are under `native-qt/build-integration/ui-exploration/20261106-085441/reliability-restored-20261107-140134/` and `saved-twitch-20261107-140139/`. Studio was left open and idle on this profile.

Chat-worker termination recovered automatically in **1.07 seconds**; deliberate thread suspension recovered through heartbeat detection in **4.49 seconds**. A killed, verified child WebView2 network service was replaced and comments resumed. Recording, live output and a held real YouTube comment survived all three faults. The received card was visually matched on the separate receiver, and Hide still worked afterward. Killing a network service is not a full Internet outage. Only processes verified as descendants of the isolated QA app were affected.

The writer journey terminates the owned recording FFmpeg process, not Studio itself. The app reports **Recording muxer exited unexpectedly. The existing file was preserved.** It offers recovery, exports to a separate MP4 without changing the original's hash, clears the reminder, and records again without an app restart. This is a writer-process failure, not a forced disk-full condition. The earlier [complete user-journey report](studio-user-journeys-2026-11-07.md) covers abrupt whole-app termination and reopening.

Support-event checks use synthetic practice input through the actual SSN engine, native panel and Program renderer. They verify presentation and explicit featuring, not real platform gift extraction or purchases. No chat posts or purchases occurred.

The final anonymous account-window run visibly loaded Twitch's current email/username login form. Back, Reload and Return to chat remained reachable at 650x600; repeated reloads produced no false failure banner and returning restored Studio focus. No credentials were entered. This checks navigation, not credential submission or MFA. The owner's separately preserved authenticated profile is not used for these anonymous checks.

## Actual camera and microphone

A Logitech BRIO became available during this pass. The final journey captured its actual 1280x720, 30 fps picture and explicitly selected **Microphone (Logitech BRIO)**. Camera controls retained their existing values; the journey read their names and navigated Capture mode, Apply capture mode and Escape without changing physical settings. Reconnect returned usable frames. The camera showed the desk/PC; no physical camera or microphone was published.

The recording contains approximately 15 seconds of room sound, five seconds muted, and another 15 seconds unmuted. It fully decodes and plays back inside Studio. Three-second saved-audio samples measured maxima of **-32.2 dB**, **-91.0 dB** and **-34.6 dB** respectively. This verifies room sound, the actual recorded mute, and resumed audio. It does not establish speech intelligibility, lip synchronization with a person, or physical unplug/replug recovery. Saved media analysis passed six checks in `hardware-final-complete/media/`.

A separate **120.4-second** recording after reopening the hardware show contained **3,613 frames**, audible room sound (maximum -14.7 dB), and no frame timestamp gaps above 34 ms. All streams decode completely. Across 121 active-recording samples, the camera/microphone Studio instance and its descendants averaged **3.35% host CPU**, with **4.22% p95**, **392 MiB peak private memory** and **32 ms p95** for a single local-control state read. This profile had no connected chat channels or live output. The recording counter rose from 4 to 15 during startup and then stayed unchanged, with zero repeats; this counter includes pending render updates replaced during startup, not just missing encoded frames.

Visual review covered the camera picker, settings, workspace, saved-file playback, small-window chat, long private gift metadata and the featured gift card. The command bar retains the compact icons and action colors from the preceding pass; the repeated workspace title remains removed. The full long support label remains readable in private preview. Camera settings expose separate names such as **Exposure automatic**, **Focus value** and **Reset white balance**. Actual screen-reader software was not run.

## Long recording and memory comparison

The candidate completed **7,203 seconds** of simultaneous Program recording and random unlisted VDO.Ninja publication, starting at **16:51:43.595 UTC**, with no early recording/output stop or disk-guard activation. Output was 1280x720, 30 fps, using the hardware H.264 MFT encoder. The picture was a simple moving reference with a once-per-second flash and audio pulse, not a high-motion game. Anonymous YouTube and Twitch chat remained connected. A separate packaged receiver on the same PC recorded **6,624 seconds**, starting about 9 minutes 26 seconds later and finalizing ten seconds before publication ended. This avoids interpreting an intentional end-of-broadcast slate as a freeze.

The prior executable (`9984300df07ed23ba5ed165ea13253317daabf83c982c08487e20259b2236774`) receives the same two channels in another isolated profile. The comparison uses chat-job private memory, not total application memory, because only the candidate is encoding. The baseline started earlier, and the candidate's browser was deliberately restarted during failure injection. Channel traffic and browser allocation are nondeterministic; this is a concurrent observation, not a controlled causal benchmark.

Resource sampling follows Studio and all descendant workers, normalized to the PC's 20 logical processors. Separate receiver, baseline, build, installer and UI automation processes are excluded from sender CPU/memory sums, though their activity can affect host scheduling. Memory is process private bytes, not dedicated GPU memory. Samples are approximately five seconds apart. Local-control timing is the combined duration of three reads, not a single-command latency.

Across the 1,320 samples after the first ten minutes, sender CPU averaged **3.15%**, with **3.72% p95**. Sender private memory averaged **1,428 MiB**, peaking at **1,485 MiB** in that interval; the entire run's peak, including planned faults, was **1,782 MiB**. Chat private memory averaged **977 MiB** versus **1,227 MiB** in the concurrent baseline. The candidate's first steady ten-minute mean was **921 MiB**, and its last ten-minute mean was **1,012 MiB**: memory is lower in this observation but still grows, and the earlier unexplained growth is not declared fixed. The three-read API duration was **63 ms p95**. No sampled chat resource-limit breach or unexpected browser restart occurred after planned failure injection. Minimum free disk space was **2.73 GiB**. Evidence: `candidate/soak/summary.json`, `samples.jsonl` and the visually reviewed `chat-memory.png`.

The sampler observed 631 additional Twitch and 1,210 additional YouTube panel IDs after its first valid sample. These counts include possible reconnect replay and are not delivery percentages. The receiver advanced frames in every sampled interval, averaging 29.99 fps. Its frame counter describes received browser frames, independently of its recording counter.

A quiet Twitch interval prompted an independent anonymous IRC read, with no chat posts or remote browser debugging. IRC stayed joined for three minutes and received only one comment. It arrived just before the extra native collector started; both builds subsequently received the same name/body about 4.6 seconds after IRC. Both had reported **No new comments in 5 minutes** and returned to **Reading comments** after this arrival. There were zero reference occurrences strictly inside the shared scored window, so no delivery percentage is claimed. This is a quiet-channel diagnostic, not a busy-chat comparison. Evidence: `candidate/soak/twitch-crosscheck/`.

Both long files fully decode, including all audio and **216,086 sender / 198,726 receiver frames**. They passed ten media checks each. All video packet timestamps are distinct; maximum presentation gaps are **34 ms**, with effective cadence 30 fps. The first/last approximately one-minute samples had maximum pulse offsets of **30/28 ms** locally and **62/36 ms** at the receiver. Exact 96x54 frame hashes found 19/40 identical adjacent frames, with no exact unchanged run longer than two frames. Lossy encoding can make repeated source pictures hash differently; a separate luma-difference motion check supplements these hashes. Evidence: `candidate/soak/media-complete/` and `candidate/receiver/receive/media-complete/`.

That additional check found a larger pacing limitation than exact hashes or recorder counters reveal. At a mean absolute luma-change threshold of **0.1 on a 0-255 scale**, **9,836 / 13,053 transitions** were near-static, approximately **4.55% / 6.57%** locally/received. The longest such run was **100 ms** in each file. The original 60-second reference had zero near-static transitions at either 0.1 or 0.25, with a minimum change of 0.313. These are near-repeat candidates after scaling and lossy encoding, not an exact source-frame delivery percentage. At the default 0.25 threshold, the sender's longest low-change interval was 0.9 seconds during the held-comment exercise, which obscures part of the reference; the receiver's was 0.1 seconds. Neither exceeded the three-second freeze threshold, even at the reported broader sensitivity thresholds. Smooth source/compositor/encoder pacing remains an open issue despite complete, synchronized files. Evidence: the two `motion-complete/` directories and `reference-motion/`.

The initial 30.4-second sender probe and 30-second receiver recording decoded completely, with maximum observed pulse offsets of 30 ms and 29 ms respectively. On the final writer-failure journey, interrupted and recovered files both contained 532 frames over 17.7 seconds and the same 21 ms pulse offset; the subsequent 10-second recording also decoded, with a maximum offset of 38 ms. No frame timestamp gaps exceeded 100 ms in these short files. Timestamp regularity alone does not rule out repeated pictures; periodic pulse matching does not establish absolute latency or detect whole-second ambiguity.

The long-run counters require a qualification: occasional paired pending-update drops and recording repeats began at approximately **minute 54**, ending at **134 drops / 123 repeats** locally (123 repeats is about 0.057% of encoded frames). The receiver's final recording counters were **1,371 drops / 1,360 repeats** (about 0.684%). These counters measure recorder scheduling and pending render updates, not missing packet timestamps; they differ from identical decoded-frame hashes. Earlier progress updates incorrectly assumed the sender startup counters remained unchanged. The cause is not established; the existing recorder/compositor clocks were inspected without changing the running build. Bringing the sender forward at minute 105.5 was followed by no additional sender repeats, including a five-minute minimized-window interval at minutes 110-115. This does not establish visibility as the cause. This pass must not be described as flawless frame pacing or a proven fix for that issue.

## Installer

The visible per-user installer, installed application, upgrade and uninstaller completed on this host. Both candidate and final installers also refused installation while the long recording/stream were running, explained that Studio must close first, left outputs active, and wrote no installation payload (three checks each in `candidate/installer/running-app-results.json` and `candidate/installer-final-guard/results.json`).

| Workflow | Checks | Evidence under `R/installer-final/` |
| --- | ---: | --- |
| Install the candidate into an explicit QA directory, with correct executable, registration and shortcut | 4 | `first/` |
| Installed candidate: add public channels, receive/feature real comments, record picture/audio | 8 | `application-first/` |
| Upgrade to final, retaining the proposed directory and retiring obsolete pack assets | 6 | `upgrade-135440/` |
| Final installed app: preserved scenes/sources/channels, real comments, private selection, featuring and recording | 10 | `application-upgrade-135949/` |
| Open the two-hour recording, keyboard-seek to 20 seconds before the end, resume picture/audio | 4 | `long-playback/` |
| Missing chat-pack asset: reject chat, explain reinstall, still record moving picture/audio | 5 | `application-missing-140021/` |
| Tampered chat-pack asset: same isolation and usable recording | 5 | `application-tampered-140038/` |
| Uninstall: completion, payload/registration/shortcut removal and retained user show | 5 | `remove/` |

The four qualified installed recordings fully decode, each containing 370 or 371 frames and audible pulse audio over about 12.4 seconds. Maximum A/V pulse offsets were **28, 4, 9 and 22 ms** respectively, and maximum timestamp gaps were 34 ms. The real comments featured in the installed-app runs were from YouTube; adding two channels and seeing aggregate comments does not independently qualify Twitch delivery in those short runs. The long run and earlier dedicated comparisons cover Twitch separately.

Damaged-pack checks modified only independently installed QA assets and restored their exact bytes afterward. Screenshots visibly explain **Chat is unavailable. Reinstall Studio to restore its chat files.** Recording continued with the moving reference selected. Uninstall preserved **13 shows and recordings byte for byte**, verified against hashes taken before removal; user data was outside the application directory. The temporary installation registration and Start menu shortcut are gone. This was a same-version build upgrade (both 0.9.4), with an explicit disposable data profile on a machine that already has WebView2; it is not a clean-machine or cross-version migration qualification.

## Remaining limits and retained failed attempts

Real broadcast rollover, a full Internet outage, physical device hotplug, forced disk-full behavior, a clean Windows machine, Meshcast and live gift extraction remain unqualified. This host is not elevated; no bounded test volume was mounted and the system disk was not intentionally filled. Existing accounts and browser storage are preserved. Rumble, TikTok, Facebook, X and Instagram are not newly qualified by this pass.

Failed harness attempts remain in ignored evidence and are not included in successful check counts. Early writer helpers assumed the muxer was a direct app child instead of an output-worker descendant and compared path separators literally; they were corrected to verify the entire owned process tree and normalized target path. An early hardware analysis used `Measure-Object` incorrectly on hashtables. Camera automation initially matched the text label instead of the named combo and a title-bar Close instead of the modal's action; typed selection and Escape completed the journey. The first final support helper saw discovery before a visible main window; waiting for both resolved it. The resource sampler's first unavailable sample was a zero-length interval calculation. Failed runs are retained separately from the completed runs.

The longer BRIO file exposed another checker problem: FFmpeg's default null-output encoder time base rounded distinct millisecond timestamps onto the same 1/30-second slot. The original analysis correctly failed on its warnings; inspecting all 3,613 decoded input timestamps found strictly increasing 33/34 ms steps. The checker now preserves the demuxer time base and passes every frame through, as described by [FFmpeg's encoding options](https://ffmpeg.org/ffmpeg.html#Advanced-options). The saved file was not modified. The original warnings/failed analysis are retained as `decode-rounded-output.log` and `analysis-rounded-output.json`. The short-media helper now exits unsuccessfully on decoding/timestamp failures; the long-media helper also checks the complete packet timeline, frame cadence and explicit first/last-minute pulse thresholds.

Installer automation initially compared paths literally and rejected the correct destination's trailing slash. A separate visible directory inspection confirmed the same registered path; the corrected upgrade accepted that proposed destination without editing it. Installed-recording checks initially inspected an active media worker while the visible Program scene was Opening. These runs are retained but do not qualify moving-picture/audio recording. The repeat explicitly selects Installed recording, captures its Program picture and analyzes the saved recording. A repeat after long-file playback also initially treated the newly added review scene/source as upgrade loss; original identities are now checked for preservation while allowing those intentionally added objects.

## Repeating focused journeys

Use fresh disposable profiles and packaged executables. `studio-camera-mic-workflow.ps1 -Exe <exe> -Out <new directory> -Microphone <exact device name>` runs only local hardware recording. `studio-hardware-media.py --ffmpeg <ffmpeg> --capture <capture.json> --out <new directory>` checks its actual saved picture/audio.

`studio-recording-writer-failure.ps1 -Exe <exe> -Out <new directory> -ReferenceFile <moving audiovisual reference>` deliberately terminates only its verified recording writer. `studio-chat-recovery-workflows.ps1 -SessionFile <isolated sender> -ReceiverSessionFile <isolated receiver>` requires an already recording/live sender and deliberately faults its chat children. Do not point either at a production session.

`studio-soak-media.py --ffmpeg <ffmpeg> --file <completed recording> --out <new directory> --expected-seconds <observed duration> --expected-fps 30` decodes the entire saved video/audio, hashes every decoded frame at 96x54 resolution, checks the full video-packet timeline, and analyzes pulse/frame alignment in its first and last minutes. It does not replace visually reviewing the actual received picture or checking source activity.

`studio-soak-motion.py --ffmpeg <ffmpeg> --file <completed moving-reference recording> --out <new directory>` supplements exact hashes with per-frame luma differences and threshold sensitivity. It streams decoded pictures and retains motion statistics, keeping no raw-video copy. A stationary camera scene is unsuitable for this check.

Recordings, screenshots, profile/account data, comments, control credentials and viewing URLs remain git-ignored.
