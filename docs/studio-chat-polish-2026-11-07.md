# Chat navigation, background recording and Twitch channel isolation

Work in `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`. Completed 2026-11-08 UTC. Results below identify the exact package used and the limits of each workflow. The main checkout's existing source edits are preserved.

## Package

| Artifact | SHA-256 |
| --- | --- |
| Two-hour recording package `native-qt/dist/desktop-studio-chat-final3-nov7/desktop-studio.exe` | `f520a1e2de9b30681e114563f1fc980d16279078fbd83f8a7369b326a060853f` |
| Earlier installer `native-qt/dist/desktop-studio-0.9.4-chat-final3-nov7-setup.exe` | `1ab0c62eb7d20c8861fe2dfc3c5df144d026c0b6e179de3990d956126b4ada69` |
| Latest portable `native-qt/dist/desktop-studio-chat-final4-nov7/desktop-studio.exe` | `b50d2b5966d3dfa587e83d6fe5e7a4487f5bebc5454de7479fb0698018d33b98` |
| Latest installer `native-qt/dist/desktop-studio-0.9.4-chat-final4-nov7-setup.exe` | `6fcdbff77f3331b8a0dce77378df0f42aa83252e4c7e451f093267da0fa3a907` |

The latest package adds a Twitch channel-scope fix discovered during the preceding package's long run. Video scheduling and rendering are unchanged between these two packages. The two-hour run is **not** an exact-latest-executable soak.

The executable and installer are timestamp-signed with the existing local development certificate. Windows trust reports `UnknownError`; these are not publicly trusted release artifacts. The verified SSN pack and shared runtime files are unchanged.

## Changes

- Connected channels occupy a compact vertical list, with platform colours and visible, named remove buttons. The list shows two rows and scrolls vertically for additional channels. Long labels elide to the available width, with full names and connection status in accessibility descriptions and tooltips.
- Connection-status changes update existing controls rather than recreating them and discarding keyboard focus. Focusing a channel scrolls it into view. Accessible toggle actions now actually filter the comment list.
- Removing the last capture connection clears stale readiness and memory status after the chat worker stops. A held comment stays on Program until Hide is used.
- Studio now retains precise canvas timers when its normal window is minimized or covered. The existing policy previously applied only to headless Studio. It does not raise process priority or change machine settings.
- Twitch capture stays scoped to its configured channel; cross-channel raid navigation and messages are rejected, with a periodic check for SPA route changes.
- The long recording harness accepts a Twitch channel choice, journals chat samples incrementally, records its start time and executable hash, and stops its owned outputs if disk headroom falls below 750 MiB.

## Memory investigation and disk space

Evidence root `R` is `native-qt/build-integration/chat-memory/`. The baseline executable was the previous qualified `7b9d8f7e...` receiver build. Fresh profiles were observed for 90 seconds each with no channels, Twitch alone, then Twitch plus YouTube. The reported means below use the final 15 samples of each phase.

| State | Baseline private chat memory | Experimental 480-pixel capture viewport |
| --- | ---: | ---: |
| No connected channels | No chat worker | No chat worker |
| Twitch | 530 MiB | 521 MiB |
| Twitch and YouTube | 1,000 MiB | 942 MiB |

The sequential windows had different real-chat activity. This small difference does not establish a durable memory reduction, so the viewport experiment was discarded. The final package retains the original capture dimensions. Its corrected idle status is not presented as a fix for connected-chat memory growth. Evidence: `baseline2/measure/` and `candidate/measure/`.

The baseline harness failed when it attempted Keep up before the featured card appeared. Its preceding connection measurements remain observations, but the complete baseline journey did not pass. The harness now waits for the actual on-screen state. Experimental packages and failed runs remain distinct from final qualification.

A separate six-minute paired diagnostic ran two copies of the earlier `19c8db41...` executable against Twitch/Lirik and YouTube, one with a process-local `--disable-gpu` override. Over the final two minutes, private chat memory averaged 977 MiB normally and 797 MiB with software rendering; total-machine CPU averaged 0.46% and 0.53%, respectively. Both copies had 392 observed comments and visually matching featured-card images. This was controlled through the local API, not a full user-workflow qualification. The flag is not added to the package or persistent machine settings: [Microsoft documents it as a development diagnostic and advises against relying on browser flags in production](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/webview-features-flags). Evidence: `gpu-diagnostic/comparison.json`. Both diagnostic apps closed normally. The two-hour recording uses the normal rendering path throughout.

Twenty-nine obsolete, ignored generated ZIP archives were removed, reclaiming approximately 3.5 GiB. Source trees, saved shows, account profiles and original QA recordings were preserved. Automatic approval review rejected expanded-package directory cleanup; those directories were left intact.

Another 21 obsolete generated installers, versions 0.2 through 0.9.2, were individually verified as ignored files and removed from the main checkout's `native-qt/dist`. This reclaimed 1.456 GiB; 0.9.3/0.9.4 installers and every integration candidate were retained. The exact paths, sizes and hashes are in the ignored `chat-memory-old-installer-cleanup.json`. A post-deletion display calculation failed on PowerShell hashtables; rereading the saved manifest verified all 21 removals and the actual free space. No source or saved recording was removed.

## Completed application workflows

These workflows use the packaged application, real pointer/keyboard input or Windows UI Automation, local-control reads for verification, screenshots, and saved media. Compilation, PowerShell syntax, whitespace and secret scanning are separate gates, not application testing.

The following earlier application workflows identify the `f520a1e2...` executable in their result files; media analyses identify the actual saved recordings from those workflows. The earlier installer guard identifies the `1ab0c62e...` setup file. These are individual checks within workflows, not a count of independent users or sessions.

| Workflow on the long-run package | Evidence relative to R |
| --- | --- |
| General UI, malformed input, privacy, scene/source editing, keyboard navigation and restart: 77 checks | `final2-ui/workflows-20261107-182626-829/` |
| Connected-channel focus, vertical scrolling at 1024x768 and 1280x720, accessible filtering/removal, worker release and held-comment preservation: 20 checks | `final2-ui/channels-20261107-182732/` |
| Layout at four window sizes, source crop controls, audio mute/unmute, recording and private break; complete saved audio/video decode: 34 checks | `final2-ui/layout-20261107-182752-828/` |
| Synthetic donations, memberships, gifts, gift-streak updates and three display styles: 77 checks | `final2-ui/support-20261107-182828-292/` |
| Anonymous Twitch sign-in-window navigation, small-window controls, repeated reload and return to chat: 14 checks | `final2-ui/account-navigation-20261107-182919/` |
| Visible, minimized, covered and restored recording, 45 seconds per phase: 11 checks | `gpu-diagnostic/default/background-recording-20261107-181256/` |
| Chat-worker termination, suspension and network-service failure during recording/publication: 14 checks | `soak-final3/worker-recovery-20261107-182144/` |
| Installer refuses installation while Studio is live/recording, explains why and writes no payload: 3 checks | `soak-final3/installer-guard/` |
| Real Twitch resubscription: private selection, Show on screen, Keep up, received-picture review, Hide and restoration of the original comment: 7 checks | `soak-final3/real-subscription/` |
| Actual BRIO picture/microphone, camera settings, reconnect, mute/unmute, local recording and playback: 13 checks | `hardware-final3/hardware/` |
| Complete saved hardware decode, mute/unmute audio and extracted pictures: 6 checks | `hardware-final3/hardware/media/` |
| Fresh launch, two new scenes, private Preview and Take: 5 checks | `crash-final3/create-show/` |
| Forced application termination, restored show, visible recovery, cancellation, MP4 export and original preservation: 14 checks | `crash-final3/record-crash-20261107-190838/` |
| Recovered video playback, seeking, resumed audio and normal restart: 6 checks | `crash-final3/recovered-playback/` |
| Anonymous Kick capture, private selection, featuring, local recording and Hide: 7 checks | `kick-final3/setup/` and `kick-final3/feature/` |

Visual review found the third connected channel did not scroll into view through accessibility focus. The accessible toggle also changed button appearance without applying the filter. Both were fixed and the focused workflow repeated successfully. The failed first implementation is retained under `final-ui/channels-20261107-171947/`; its executable is not the final artifact above.

The layout recording measured silence at -91 dB during mute and audible audio at -18 dB after unmute. The file fully decodes and includes the private-break picture. Synthetic support events exercise display and state handling through SSN; they do not establish live gift extraction or real purchases.

The physical BRIO recording contains the available room picture and room sound, with no publication. Saved microphone maxima were -53.9 dB before mute, -91.0 dB while muted and -52.2 dB after unmute. The complete file decodes, and its extracted picture and playback workspace were visually reviewed. Camera settings retained their values. This establishes low-level room audio, not speech intelligibility or physical hotplug recovery.

The forced-crash workflow ran in a separate fresh profile, leaving the long recording and owner workspace untouched. The recovery banner was visible after restart. Cancelling preserved the offer; completing recovery made a separate MP4 and retained the interrupted MKV byte for byte. Both interrupted and recovered files fully decode with 665 frames, 22.17 seconds of audio, no video timestamp gap above 34 ms and measured pulse offset within 17 ms. The normal control recording also fully decodes with 667 frames and offset within 38 ms. Keyboard seeking reached ten seconds, resumed picture/audio worked, and another restart preserved the show without bringing back a completed recovery prompt.

Chat recovered from a terminated worker in 1.06 seconds and from a deliberately unresponsive worker in 4.24 seconds. The verified child network service was replaced and incoming comments resumed. Recording, live output and the held real comment survived all three faults. Program and received screenshots were visually compared. This is not a full Internet outage or a platform broadcast rollover.

After the planned faults, anonymous IRC stayed joined throughout a 290.002-second scored Twitch/Lirik window. Every one of its 167 occurrences matched a distinct Studio occurrence by normalized author/text within five seconds, including all 13 occurrences carrying Twitch emote tags. This includes 154/154 without those tags; the presence of tags does not identify emote-only messages. Median/max observed delay was 1,195/2,282 ms, including the native collector's two-second polling interval. Five seconds at each boundary were excluded. This is one finite comparison, not a general delivery guarantee. Evidence: `irc-final3-start/comparison.json` and the ignored raw ledgers; the reference reader sends only IRC handshake/join/keepalive commands, never chat posts.

The initial separate receiver recorded 301.505 seconds and 9,045 frames during the chat faults. Its complete audio/video decode and ten media checks passed. All packet timestamps were distinct and gaps were at most 34 ms; first/last-minute maximum pulse offsets were 46/48 ms. The initially selected long comment obscured most of the moving gradient, invalidating a whole-picture motion threshold. The retained `motion-start/` result is therefore not evidence of a freeze. Scoring an uncovered 32 by 90 pixel reference region found 24 near-repeat candidates, with a longest interval of 67 ms. The original reference produced zero candidates with the same crop and threshold. These are heuristic candidates, not an exact source-frame delivery rate. Evidence: `receiver-final3/receive/media-start/`, `motion-start-uncovered/` and `reference-motion-uncovered/`.

A short actual comment was selected and featured to restore a useful motion fixture; this API fixture correction is not counted as a UI workflow. A further 120.073-second receiver recording fully decodes, has a maximum pulse offset of 51 ms, no missing pulse interval of 1.5 seconds, and one whole-picture near-repeat candidate across 3,602 frames. Evidence: `receiver-final3/uncovered-motion-check/`. The receiver stays connected for the remaining long run.

An actual 74-month Prime resubscription was matched to an independent Twitch `USERNOTICE`, selected in Thanks through the UI, featured and visually verified in the independent received picture. The previously held comment stayed on Program during private review. This establishes a real subscription workflow; synthetic gift/streak checks do not establish live gift extraction or purchases. The corresponding frame extracted from the finalized two-hour recording was visually reviewed and contains the actual subscription card. Evidence: `soak-final3/real-subscription/`.

Kick/Buddha was confirmed live through its public channel API, with approximately 18,574 viewers at the comparison's start. Its public socket remained connected throughout a five-minute comparison: 299/299 scored comment occurrences matched, including 182 carrying emote tokens, with no detected stalls, duplicate extras or unexpected comments. A stricter follow-up required distinct author/text/emote-name matches within five seconds and trimmed the initial boundary: 297/297 matched, comprising 116 plain, 13 mixed and 168 emote-only comments. Emote-only means removing explicit Kick emote tokens leaves only whitespace. Median/max observation delay was 1,783/3,497 ms, including the native collector's three-second polling interval. These are finite windows, not a platform-wide delivery guarantee. Evidence: `native-qt/build-chat/kick/2026-11-08T00-11-57-818Z/summary.json` and `timed-comparison.json`.

The real Kick comment was selected privately, featured, recorded and hidden through the visible application. The saved 15-second file fully decodes with 450 frames, audio/video pulse offset within 36 ms and no timestamp gap above 34 ms. Its extracted frame was visually reviewed. This journey made no public output; no comments or gifts were sent.

The earlier `19c8db41...` candidate passed a Rumble journey in a separate anonymous QA profile. A real RTNews comment arrived, stayed private when selected, appeared on Program, and was visually confirmed in the saved recording. All 447 video frames and the audio fully decode; pulse offset was at most 19 ms and timestamps had no gap over 34 ms. Evidence: `rumble-journey/`. The `f520a1e2...` executable's retry received no Rumble comments within 120 seconds and did not complete that journey; an older still-open copy also reported no recent comments. This is inconclusive, not a current-package Rumble pass or proof of a regression. Evidence: `rumble-final3/`. Both attempts were local, with no publication.

## Latest package: repeated workflows and real raid finding

The latest `b50d2b59...` executable passed 222 repeated application checks:

| Workflow | Checks | Evidence relative to R |
| --- | ---: | --- |
| General UI, input fuzzing, privacy, scenes and restart | 77 | `final2-ui/workflows-20261107-200000-311/` |
| Three-channel navigation, filtering, removal and worker release | 20 | `final2-ui/channels-20261107-200437/` |
| Four window sizes, crop navigation, mute/unmute and saved recording | 34 | `final2-ui/layout-20261107-201024-354/` |
| Synthetic support events, gifts/streaks and display styles | 77 | `final2-ui/support-20261107-201059-490/` |
| Anonymous Twitch account navigation and reloads | 14 | `final2-ui/account-navigation-20261107-201150/` |

The latest installer also passed its three running-app guard checks: it explained that Studio must close, wrote no payload, and preserved the active recording and publication. Evidence: `soak-final3/installer-guard-final4/`. Reviewed screenshots show consistent command heights, one command row at 1280 pixels, vertical channel navigation and fully visible crop controls when focused.

Retained incomplete attempts are not counted: `channels-20261107-200106/` timed out waiting for comments from quiet fixtures; the repeat used active Kick/Buddha. `layout-20261107-200454-767/` failed four crop checks because its source had not been selected after the preceding workflow cleared selection. The first harness correction attempted to focus the nonfocusable Qt list and stopped (`layout-20261107-200955-815/`). Explicit row selection through accessibility fixed the setup; all 34 checks then passed. No product change was needed for those crop failures.

During the long run, Twitch actually raided from LIRIK to LIRIK_247 at **2026-11-08 00:03:51 UTC**, approximately 45.7 minutes into recording. The preceding package followed the navigation while retaining the original channel's label. A copy of the anonymous QA capture profile's locked SQLite history, including its WAL, passed `quick_check` and recorded `referrer=raid` followed by the destination popout URL. No owner browser history or credentials were accessed. An independent three-minute public destination-channel reference subsequently matched 28/28 scored comments being incorrectly labelled as LIRIK. Evidence: `soak-final3/capture-history-review/public-twitch-redirect.json` and `irc-redirect-target/comparison.json`.

The latest worker binds incoming Twitch messages to the originally configured channel, rejects full-page cross-channel redirects, and returns to that channel when a periodic probe detects SPA navigation. It keeps explicitly added channels independent. The code/origin/account regression gates pass; normal packaged chat and account journeys pass as shown above. **A second real raid has not been observed on the fixed package**, so prevention under a real raid remains unqualified. This is distinct from testing YouTube stream rollover.

Before the raid, a continuously joined public Twitch reference matched 1,788/1,789 occurrences over 1,406.143 seconds, including 1,628/1,629 without emote tags. Median/max observed delay was 1,209/2,318 ms. The remaining occurrence has no identified capture counterpart; it remains an unexplained mismatch. The larger reference continued through the raid, but destination-channel comments must not be treated as successful LIRIK delivery. Evidence: `irc-final3-long/comparison-pre-raid.json`. An apparent reference stall inferred from an open file's stale filesystem timestamp was disproved by record timestamps and overlapping independent IRC messages; it is not a reported application fault.

A separate half-hour Kick/Buddha observation matched 1,390/1,390 distinct comments within five seconds over 1,788.464 scored seconds: 702 plain, 69 mixed and 619 emote-only. Median/max delay was 1,166/2,548 ms with two-second native polling. The public connection stayed subscribed. No live gift or subscription event arrived, so the observation does not qualify live gifts. Evidence: `kick-gifts-final3/reference/summary.json` and `kick-gifts-final3/timed-utf8/timed-comparison.json`. The retained first analysis incorrectly decoded UTF-8 logs using Windows' default code page, producing 28 apparent mismatches; explicit UTF-8 reads resolved all of them without changing the app or raw ledgers.

## Long recording and installer

The earlier `19c8db41...` run began at 22:27:38 UTC and was deliberately stopped after 2,985.7 seconds. It exposed background-window scheduling trouble after about half an hour. Bringing Studio forward yielded zero repeated recorder frames in 40 seconds; minimizing it yielded 24 in 45 seconds. The retained recording fully decodes, has distinct 30 fps timestamps and A/V offsets within 40 ms, but motion analysis found 307 near-repeat candidates. Its coarse three-second freeze threshold passes; that does not mean its pacing was acceptable. This is a retained failed qualification attempt, not a completed two-hour soak. Evidence: `soak-final/cadence/intentional-stop.json`, `media/`, `motion/` and the two visibility observations. Six separate brief GUI-thread interruptions recovered without ongoing repeats, so the speculative render-clock phase explanation was not used as a fix.

[Microsoft documents Windows 11's timer-resolution throttling for occluded windows and the process-specific opt-out](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation). The existing headless clock policy was extended to the normal Studio canvas. The replacement package passed visible, minimized, covered and restored recording phases with zero repeated frames in each 45-second observation. Its 189.1-second saved file fully decodes, has zero near-repeat candidates across 5,675 frames and A/V offsets within 20 ms. Evidence: `gpu-diagnostic/default/background-recording-20261107-181256/`. This workflow uses no live chat and makes no public output.

The exact replacement executable began its two-hour recording/publication at 2026-11-07 23:18:07 UTC. It uses a moving picture/pulse-audio reference, two additional media scenes, anonymous YouTube and Twitch/Lirik capture, and a held real comment. The show/evidence directory is fresh; the disconnected anonymous QA browser cache is reused to avoid unnecessary disk consumption. Only synthetic picture/audio and public comments are published to a random unlisted VDO.Ninja destination. No webcam or microphone is broadcast and no chat posts or purchases are made. The sender is deliberately minimized for an extended part of the run.

On this replacement, all 14 chat-failure workflow checks passed during recording/publication. Recovery took 1.06 seconds after worker termination and 4.24 seconds after a hang; the child network service also recovered. Evidence: `soak-final3/worker-recovery-20261107-182144/`. The independent receiver now stays connected between its initial and final saved samples, with continuous per-second frame/error observations.

The run completed normally after 7,201.170 seconds of observed recording. The saved sender file is 7,200.778 seconds long with 216,023 frames. All eleven media checks passed: full video/audio decode, continuous pulse audio with no silence interval of 1.5 seconds, distinct presentation timestamps with a maximum 34 ms gap, and first/last-minute pulse offsets within 29/25 ms. The calibrated uncovered reference region has ten near-repeat candidates, with a longest interval of 67 ms. These are image-difference candidates, not an exact network frame-loss count. Recorder counters ended at 14 dropped updates and three repeats; the run is not claimed to have zero drops. Evidence: `soak-final3/cadence/media-final/`, `motion-final-uncovered/` and `capture.json`.

The window remained minimized through two verified intervals of roughly 26 and 76 minutes, with periodic checks confirming that the actual window was iconic and recording/publication were active. One isolated repeat occurred in the longer minimized interval, with no sustained repeat burst. Both intervals restored normally. Evidence: `soak-final3/minimized-window.json` and `minimized-window-200813.json`.

After the first twenty minutes, mean/p95/max Studio CPU was 4.79/6.02/12.89 percent of this 20-thread host. Studio and its child processes averaged 1,590 MiB private memory; chat averaged 1,075 MiB and peaked at 1,282 MiB. Chat's first and last steady ten-minute means were 1,064 and 1,074 MiB. This includes a real Twitch redirect and concurrent QA work; it is not a controlled memory benchmark or a leak fix. No resource-limit event, oversized-message report or unplanned chat-browser restart occurred. All sampled recording/live states remained active, and chat was ready throughout the post-fault observation. Native observer errors were zero and its maximum polling gap was 2.065 seconds. Aggregate chat counts include restarts and the incorrectly labelled raid destination, so they are not delivery percentages. Evidence: `soak-final3/cadence/summary.json`, `memory.png`, the raw metrics and observer ledgers.

The independent receiver stayed connected for 7,019.980 sampled seconds, with 7,016 observations, no source error, no unavailable sample and no interval without an advancing source-frame counter. This sampled observation does not prove every picture or audio sample throughout the entire connection. Only the initial, additional two-minute and final samples were saved. The final saved sample fully decodes: 289.387 seconds, 8,681 frames, no pulse gap of 1.5 seconds, maximum timestamp gap 34 ms, first/last-minute A/V offsets within 44/46 ms. It has four uncovered-region near-repeat candidates, longest 67 ms. Its saved picture was visually reviewed with the held real YouTube comment intact. Evidence: `receiver-final3/receive/media-final/`, `motion-final-uncovered/`, `saved-end-frame.png` and `results.json`.

## Installer, saved files and owner restoration

The visible installer journey first installed the older `b9f7c682...` 0.9.4 build into an explicitly owned QA directory, created a show and recorded anonymous chat, then upgraded that same directory to the latest `b50d2b59...` executable. This qualifies same-version upgrade, not a fresh-machine latest-version installation. The upgrade selected the registered directory, installed the exact portable executable, removed an obsolete SSN-pack sentinel, and preserved saved scene, source and channel identities.

| Installed workflow | Checks passed | Evidence relative to R |
| --- | ---: | --- |
| Initial older installation | 4 | `installer-final/first/` |
| Initial installed chat and recording | 8 | `installer-final/application-first/` |
| Upgrade to the latest package | 6 | `installer-final/upgrade/` |
| Latest installed picture/audio, anonymous Twitch and YouTube, private selection, featuring and recording | 11 | `installer-final/application-upgrade/` |
| Missing chat-pack file is rejected while recording works | 5 | `installer-final/application-missing/` |
| Modified chat-pack file is rejected while recording works | 5 | `installer-final/application-tampered/` |
| Visible uninstall, registration/shortcut removal and saved-show retention | 5 | `installer-final/remove/` |
| Shows and recordings survive removal byte for byte | 8 files | `installer-final/preserved-files-after.json` |

All four installed-app recordings fully decode with 367, 368, 371 and 371 frames, respectively. Maximum A/V offsets were 1, 39, 15 and 29 ms; every maximum video timestamp gap was 34 ms. The latest installed recording's extracted frame visibly contains its selected real Twitch comment. The damaged-pack screenshots visibly explain that reinstalling restores chat; picture/audio recording remains available. Original pack bytes were restored after each fault.

The existing owner workspace was closed normally only after the two-hour sender/receiver finished. It was reopened on the latest portable with all seven scenes, eight sources and four channels preserved, verified against both its saved show and its live pre-install state. Five restoration checks passed. Four additional checks verified the existing Twitch session through the visible **Log Out** menu, returned to ready chat, preserved channel identities and left production idle. No credentials or owner browser storage were read. Evidence: `native-qt/build-integration/ui-exploration/20261106-085441/chat-restored-20261107-202150/` and `saved-twitch-20261107-202152/`.

Compilation and the targeted Qt channel/origin/account regression gates passed. PowerShell parsing, Python syntax, whitespace and staged secret scanning passed; these are gates, not application testing. Raw chat ledgers, account profiles, screenshots, recordings, SQLite QA-history copies, the SSN pack and generated packages remain outside the commit. Automatic approval review also blocked deleting two generated Python cache files; they remain untracked and excluded. The main checkout's existing edits were not changed.


## Remaining qualification limits

- The latest Twitch channel-scope fix has code gates and repeated packaged journeys, but no second observed real raid and no new exact-latest-executable two-hour run.
- Connected chat still uses roughly 1 GiB. Ending all capture connections releases its worker; that does not solve memory use while connected.
- One pre-raid Twitch occurrence was unmatched. Finite successful Twitch/Kick windows do not establish universal lossless delivery.
- Actual subscription capture and synthetic gifts/streaks were exercised. No live gift event or purchase was qualified.
- Real YouTube broadcast rollover, a full Internet outage, Meshcast publication, TikTok and the other sign-in-dependent platforms remain unqualified. The latest Rumble retry was inconclusive.
- BRIO evidence covers room picture/sound, settings and reconnect, not spoken lip sync or physical hotplug. These runs do not qualify an actual screen reader or OS DPI changes.
- Installer checks use an existing WebView2 runtime and a same-version 0.9.4 upgrade on this machine. Local certificate trust remains `UnknownError`.
- The API's `composed_ms` value can describe the previous held card during replacement; it was not used to claim display latency.
