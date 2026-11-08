# Chat reconnection, gifts and recording qualification

Work in `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`. Evidence root `R` is `native-qt/build-integration/release-qualification-nov8/`. Raw chat, browser profiles, screenshots and recordings remain in ignored evidence directories. This report separates actual packaged workflows and saved-output inspection from build/syntax/secret-scan gates.

## Changes and exact artifacts

- Closing a capture now sends `tabs.onRemoved` to the Social Stream engine. Previously, removing/re-adding YouTube or signing out left its old tab in SSN's preferred-source cache. The replacement page could contain real comments and submit them to the native bridge while SSN silently discarded them. Three diagnostic packages isolated this: the last observed six accepted YouTube messages and zero delivered messages. A full worker restart restored delivery. The diagnostics were removed from the final source.
- **Save As active show** now explains the recording restriction before opening a file chooser. The existing command-level guard remains. Windows uses Qt file choosers so browsing can run the application's timer loop. [Qt documents that its native Windows file dialog loop does not dispatch QTimers](https://doc.qt.io/qt-6.12/qfiledialog.html); the canvas is timer-driven. See the intermittent-stall qualification below before interpreting this as a proven cause/fix for every stall.
- Short saved-media checks now include pulse A/V analysis. The previous checker skipped it for files of at most two minutes. Kick comparison now matches individual occurrences within five seconds, distinguishes delivery up to fifteen seconds late, and fails on missing, late or unexplained surplus messages. It verifies the exact public subscription and handles heartbeat/error events. Separate paid-gift tools observe the public channel topic and compare author/amount occurrences without purchasing or posting.

| Artifact | SHA-256 |
| --- | --- |
| Earlier memory package, `native-qt/dist/desktop-studio-memory-software-nov8/desktop-studio.exe` | `f0ac7045bbee7d0e6508ce05942ff29924e52fdeaf03e1f6a4b1b9944be5de0f` |
| Intermediate reconnect package, `native-qt/dist/desktop-studio-chat-lifecycle-nov8/desktop-studio.exe` | `f601f446a722a2a98f6b810f0c11e5ef647d8c667216aeb3803515a5524f4313` |
| Final portable, `native-qt/dist/desktop-studio-live-dialogs-nov8/desktop-studio.exe` | `992325d0f51c9c419d9daffe1dfb9dbe3784bf8c15fa76c76b03b90cc8048c15` |
| Final installer, `native-qt/dist/desktop-studio-0.9.4-live-dialogs-nov8-setup.exe` | `073f6cee75c660940f9792c347a81c13ab4dfd18a941a4302f9071083c7aa115` |

The final package includes both changes. Its SSN checksum-file hash is `9a20654758d70c36e0855ee1f65356d425b2923c70a49263d52abbdf134974ee`. SSN and shared runtime assets are unchanged; no third-party capture source was patched. Packages are timestamp-signed with the existing development certificate, whose Windows trust is still `UnknownError`, not a publicly trusted release signature. Temporary diagnostic packages are not release candidates.

## Final portable: actual user journeys

The `992325d0...` executable passed these workflows using real pointer/keyboard/UI Automation interactions and actual public chat pages. API reads verify state; synthetic media supplies the unlisted output. No public chat message, purchase, real webcam picture or microphone audio was transmitted.

| Workflow | Passed checks | Evidence |
| --- | ---: | --- |
| General visual/keyboard walkthrough, malformed inputs, Unicode/long comments, scene/source editing, transitions and restart | 77 | `R/dialogs-general-ui/workflows-20261108-122925-838/` |
| Three YouTube remove/re-add cycles, cancelled sign-out, completed sign-out, and separately required fresh Twitch/Kick/YouTube comments | 17 | `native-qt/build-integration/ui-exploration/release-account-nov8/chat-reconnect-20261108-120944/` |
| Twitch account navigation, repeated reloads, 650x600 controls and keyboard-focus return | 14 | Same profile, `account-navigation-20261108-121738/` |
| Fresh isolated show, moving picture/pulse audio and Studio mode | 2 | `R/dialogs-recovery/setup/` |
| Recording, early active-show warning, save-copy cancellation, forced main-app termination, autosave restore, cancelled recovery and MP4 recovery | 17 | `R/dialogs-recovery/record-crash-20261108-120018/` |
| Play recovered MP4, seek to ten seconds, resume picture/audio and restart without another recovery prompt | 6 | `R/dialogs-recovery/recovered-playback-20261108-121403/` |
| Fifteen-minute recording with anonymous Twitch/YouTube, held real comment, two background media sources and VDO.Ninja publication | 7 | `R/dialogs-sender/cadence/` |
| Independent VDO.Ninja receive with picture/audio and continuous recording | 5 | `R/dialogs-receiver/receive/` |
| Add live Kick without interrupting recording/publication | 2 | `R/dialogs-sender/kick-add/` |
| Select a real paid gift privately, feature it and preserve recording/live output | 3 | `R/dialogs-live-gift/` |
| Save a copy and hold save/file/folder choosers open for eight seconds each during recording/publication; cancel browsing safely | 7 | `R/dialogs-sender/live-file-dialog-20261108-120825/` |
| Repeat the three file choosers during recording with streaming off | 7 | `R/dialogs-recovery/live-file-dialog-20261108-122505/` |
| Installer refuses a running Studio, preserves recording/live output and writes no payload | 3 | `R/dialogs-sender/installer-final-guard/` |

The earlier `f601f446...` reconnect build separately passed the seventeen-check reconnect workflow and a live remove/re-add workflow which preserved the held YouTube comment while its capture was replaced. Its fifteen-minute sender and 681.933-second receiver recordings fully decode with no long audio silence and A/V maxima within 40 ms. Evidence: `R/final-sender/`, `R/final-receiver/`, `R/final-media-summary.json`. The `final-*` evidence prefix predates the later dialog change and must not be confused with the final portable above.

### Saved output and visual review

Both final long-form samples fully decode, have distinct 30 fps presentation timestamps with maximum gaps of 34 ms, and contain no pulse-audio silence interval of 1.5 seconds. All pictures are inspected for motion in the unobscured moving strip, crop `0,540,32,90`, luma-change threshold 0.1. Near-static candidates are measurements, not proof that an upstream frame was lost.

| Saved output | Duration / frames | Near-static transitions / longest interval | First/last-minute A/V maxima |
| --- | --- | --- | --- |
| Final local recording | 901.666 s / 27,050 | 6 / 67 ms | 39 / 38 ms |
| Final received recording | 810.603 s / 24,318 | 4 / 100 ms | 69 / 70 ms |

Evidence: `R/dialogs-media-summary.json` and both `full-media/` and `full-motion/` directories. This covers the open file dialogs and featured paid gift. The receiver ran for about thirteen and a half minutes, not the full fifteen minutes.

The normal short recovery recording has 29 audio pulses and 29 picture flashes, maximum A/V offset 40 ms and longest near-static interval 67 ms. The recovered MP4 fully decodes with picture/audio, maximum offset 39 ms and no presentation gap over 34 ms. Its interrupted MKV remains byte-for-byte unchanged after recovery. Evidence: the recovery recordings' `.analysis.json` files and `R/dialogs-recovery/normal-motion/`.

The separate recording-only file-browser run saves 38.310 seconds / 1,149 frames, with no long audio gap, 39 audio pulses and 39 flashes, maximum A/V offset 37 ms and longest near-static interval 67 ms. All three choosers were held open for eight seconds without a live publisher. Evidence: `R/dialogs-recovery/recording-only-dialogs/`.

Screenshots were visually inspected, including the actual saved local and received gift frames (`R/dialogs-live-gift/saved-sender-frame.png` and `saved-receiver-frame.png`). Both show the correct author, **Gift - 10 KICKs** and message body. The chat panel has a distinct amber on-screen band and separates private selection from showing a comment. The compact toolbar remains intact; the repeated workspace title is absent. Qt's save/file/folder choosers fit on screen and expose their editable path and cancellation controls. The folder chooser paints **Choose** but exposes **Open** through UI Automation; that accessible-label discrepancy remains.

The general walkthrough's 1024x768 and 1280x720 screenshots were reviewed: the toolbar wraps at the narrower size and fits on one row at 1280; chat remains usable and the source/audio panels scroll vertically. Unicode and literal markup stay readable in private review. Seventy keyboard tab stops remain inside the 1280x720 window. The idle status after **Resume show** still describes Program as "on air" even with streaming off; that wording can be clearer and should not be interpreted as evidence of public transmission. The workflow explicitly verifies that no stream or recording started.

One-second samples over the final recording window show no inactive recording or live-output sample. Whole-process-tree mean CPU was 6.16% of this 20-thread host for the sender and 2.94% for the receiver; peak private memory was 1,565/661 MiB. These ran concurrently with the long soak and other QA copies, so this is not an isolated benchmark. The separate twelve-minute chat observer saw 3,362 Kick, 105 YouTube and 69 Twitch messages with no read errors or browser restart; chat private memory peaked at 1,050 MiB. Counts alone are not a completeness comparison. Evidence: `R/resource-summary.json`, `R/dialogs-observer/`.

## Real paid gifts and ordinary Kick comments

The independent reference subscribes to both `chatrooms.ID.v2` and `channel_ID`. `ChatMessageEvent` alone does not contain paid KICK gifts. Comparison uses each unique gift transaction once and matches captured author/displayed amount within five seconds, including polling delay. It fails on zero eligible gifts, malformed reference data, lost subscriptions, capture read errors, missing or unexplained extra occurrences. It does not qualify artwork or gifted subscriptions.

- Final portable: **1/1** eligible real 10-KICK gift matched, zero surplus, maximum receipt-time difference 910 ms. It was featured through the actual UI and appears in both saved recordings. Evidence: `R/dialogs-gift-reference/`, `R/dialogs-observer/`, `R/dialogs-live-gift/comparison.json`.
- Earlier `f0ac7045...` package: **8/8** gifts in a completed ten-minute reference, amounts 1, 50, 500 and 1,000 KICKs. A larger overlapping capture/reference interval matched **15/15** occurrences. Repeated equal one-KICK gifts from the same person are separate real transactions, not duplicates. Evidence: `R/live-gift/completed-reference-comparison.json`, `complete-window-comparison.json`.
- The earlier real gift also appears in an 824.960-second local recording which fully decodes and stays within 25 ms in its first/last A/V samples. No purchases were made. An hour on another live channel and a later 580-second SXB observation produced no gift events; those windows do not qualify gift delivery.

Ordinary Kick capture is **not lossless**. Earlier five-minute observations retained failures: 81 missing of 3,988 eligible messages; a busier run with 107 still missing after separating 146 deliveries that arrived five to fifteen seconds late; another run with 11 missing of 1,780. The final corrected comparator's earlier run matched 2,309/2,310 eligible occurrences, including all 796 emote-bearing messages, with no stall or unexplained surplus. The remaining loss is still a failure. Evidence: `native-qt/build-chat/kick/2026-11-08T15-48-45-277Z/`, `2026-11-08T16-13-47-123Z/`, `2026-11-08T16-13-47-129Z/`, `2026-11-08T16-21-10-648Z/`.

The latest `992325d0...` package matched **1,143/1,143** ordinary Kick occurrences over a 282.023-second scored window: 765 plain comments and all 378 emote-bearing comments. There were no missing, late, unexplained extra messages or stalls; maximum receipt difference was 3,623 ms including three-second polling. Three gift events were counted separately from ordinary comments. Evidence: `native-qt/build-chat/kick/2026-11-08T17-18-53-245Z/`. This successful five-minute window does not erase the earlier measured losses or establish a loss fix.

A fresh three-minute Twitch IRC/capture comparison observed zero messages on both sides. The checker correctly rejects that as an empty qualification window; no new completeness claim follows from it. Evidence: `R/dialogs-twitch-comparison.json` and its reference/observer directories. The separate final reconnect workflow did require and receive fresh Twitch comments after sign-out.

The later installed-build comparison used an active public Twitch feed. Its first strict five-second checker matched 31/76 eligible comments. Diagnosis found every one of the 82 reference occurrences in the captured ledger, with receipt differences of 4.2-6.2 seconds including up to two seconds of polling. A second analysis distinguishes five-to-fifteen-second delivery from actual missing/surplus occurrences and uses a fifteen-second end margin: **75/75** eligible occurrences arrived, **45 late**, zero missing or surplus. The repeated installed workflow measured **35/35**, **15 late**, zero missing or surplus, maximum receipt difference 6,222 ms. **Both runs fail the five-second target.** The original strict result remains; the changed end margin explains the different eligible count. Neither analysis measures upstream page-arrival time, so it cannot attribute all delay to Studio. Evidence: `R/installer-dialogs/installed-twitch-comparison.json`, both `application-upgrade*/twitch-occurrence-comparison.json` files, and the first run's `twitch-delay-diagnosis.json`.

## A/V sync investigation

The prior 104 ms receiver sample now correctly fails the tightened short-file checker (`R/previous-short-recheck/`). Three fresh connections on `f0ac7045...` measured median audio leads of 87, 54 and 8 ms, with maxima 97, 66 and 8 ms. Their full saved outputs pass the 100 ms target. A same-connection UI adjustment of source delay from 60 to 100 ms shifted the measured median from -71 to -31 ms, exactly 40 ms, while preserving the source, browser and recording. Evidence: `R/sync-summary.json`, `R/sync-calibration-2/`.

No global delay default changed: the variation between connections does not justify moving every source by the same amount. The final receiver's maxima of 69/70 ms pass this run, but the earlier 104 ms result remains and universal sub-100-ms sync is not established.

## Two-hour baseline and installer

The earlier `f0ac7045...` package completed two hours of local recording, anonymous chat and unlisted VDO.Ninja publication. A second packaged instance continuously recorded the received picture/audio for 118 minutes. Both stopped and finalized normally, passing seven sender and five receiver workflow checks. This is a baseline qualification; the final executable above has its separate fifteen-minute run and targeted journeys.

| Baseline saved output | Duration / frames | Near-static transitions / longest interval | First/last-minute A/V maxima |
| --- | --- | --- | --- |
| Local | 7,201.737 s / 216,052 | 13 / 67 ms | 54 / 55 ms |
| Received | 7,101.799 s / 213,054 | 32 / 133 ms | 12 / 35 ms |

Both complete files decode without errors, retain distinct 30 fps presentation timestamps with maximum gaps of 34 ms, and contain no pulse-audio silence interval of 1.5 seconds. These results use the same motion crop/threshold as the final samples. The sender remained minimized for 82.38 minutes; all 247 checks preserved recording/publication and no external input restored it early. Its restored workspace and held comment were visually reviewed near the end. Evidence: `R/long-media-summary.json`, `R/long-sender/cadence/`, `R/long-receiver/receive/`, `R/long-sender/minimized-window.json`, `near-end-visual/`.

During the actual recording windows, 7,191 sender and 7,092 receiver resource samples had no read failure or inactive recording; the sender also had no inactive-publication sample. Mean whole-process-tree CPU was 5.82%/2.50% of this 20-thread host, with peak private memory 2,585/698 MiB. Other QA instances ran concurrently for portions of the window, so these are observed costs rather than isolated benchmarks.

The chat observer ran for 119.24 minutes with 3,577 successful reads, one unchanged browser PID, and no observed restart, resource-limit event, lost held comment or oversized-message counter. It counted 52,367 Kick, 1,674 YouTube and 694 Twitch comments; these totals do not measure completeness. Chat private memory started at 651 MiB before Kick was added, peaked at 2,061 MiB, and averaged 2,017 MiB in the last five minutes. The initial/final difference includes adding a channel and is not a controlled leak measurement. However, detailed process sampling over the final 90.93 minutes found one WebView renderer growing by 501 MiB and its browser process by 125 MiB, while Studio's main process grew by only 1.74 MiB. The renderer was not conclusively mapped to a platform. This confirms continuing browser-side memory growth without identifying its cause or fixing it. Evidence: `R/resource-summary.json`, `R/process-memory-growth.json`, `R/chat-observer/`.

The installer workflow used a fresh explicit QA directory with no pre-existing Studio installation registration. It installed the earlier memory package, created a real show/recording with anonymous chat, and then visibly upgraded that installation to the exact final executable. Both installers are version 0.9.4: this qualifies a same-version replacement, not migration from another version. WebView2 was already installed, so fresh runtime provisioning was not exercised.

| Installed workflow | Passed checks | Evidence under `R/installer-dialogs/` |
| --- | ---: | --- |
| First installation of earlier package, exact executable and per-user registration/shortcut | 4 | `first/` |
| Create scene/media, receive public comments, privately select/feature and record | 8 | `application-first/` |
| Final installer selects existing destination, replaces executable and retires obsolete SSN asset | 6 | `upgrade/` |
| Final installed app preserves scene/source/channel identities, receives Twitch and YouTube, features Twitch and records | 12 | `application-upgrade-125327/` |
| Missing chat-pack file is rejected while picture/audio recording remains functional | 5 | `application-missing/` |
| Tampered chat-pack file is rejected while picture/audio recording remains functional | 5 | `application-tampered/` |
| Visible uninstall removes only the QA application, registration and shortcut | 5 | `remove/` |

The first upgraded-app attempt passed eleven application checks but failed the helper's delayed process-exit check despite both observers writing completed results. The repeated helper retained process handles from launch and recorded both normal zero exits. The failed attempt remains in `application-upgrade/`; its saved recording also passes full media analysis. The comparison's latency failure above is separate and remains open.

All five installed-app recordings fully decode with no long pulse-audio silence. The earlier-install, missing-pack and tampered-pack files last 12.232/12.432/12.432 seconds, with maximum A/V offsets 33/23/24 ms. The two upgraded-app recordings last 130.356/130.462 seconds, with first/last-minute maxima 39/40 ms and 30/27 ms. Actual saved frames were extracted and visually reviewed, including the featured Twitch comment. The unavailable-chat explanation is readable and recommends reinstalling; the panel still displays channel/practice onboarding controls alongside that explanation. Evidence: `installed-media-summary.json` and each application's `media-analysis/` and `recording-frame.png`.

Uninstall preserved all thirteen snapshotted show/media files byte for byte (nine original saved files plus four derived analysis clips). The owner's final portable was restored with all seven scenes, eight sources and four channels unchanged; five restoration checks passed. Four subsequent visible account checks confirmed the existing Twitch **Log Out** menu, return to ready chat, intact workspace and idle production. This verifies saved authentication, not a new credentials/MFA login. Evidence: `saved-files-before-uninstall.json`, `saved-files-after-uninstall.json`, `native-qt/build-integration/ui-exploration/20261106-085441/chat-restored-20261108-125656/`, and `R/owner-auth-125657/`.

## Retained failures and limits

- An intermediate `f601f446...` normal recording had a four-second near-static interval during the active-show save workflow and missing picture flashes despite continuous audio. The final package's corresponding workflow passes. However, a repeat on the old executable holding both native choosers open did **not** reproduce the stall (longest near-static interval 333 ms, maximum A/V offset 32 ms), and its earlier live-streaming file-dialog run also passed. The original intermittent stall is therefore retained as unresolved root cause; the early rejection and timer-friendly dialog choice are qualified mitigations, not proof that every intermittent stall is fixed. Evidence: `R/final-recovery/normal-motion/`, `record-crash-20261108-115538/`, `native-dialog-repeat/`.
- A source-picker pointer selection missed during the final recovered-playback helper. Keyboard selection worked; focusing the actual combo before expansion made the repeated full playback workflow pass. The failed `recovered-playback/` and `source-picker-inspection/` remain. Earlier file-dialog helper attempts incorrectly searched top-level roots for owned Qt dialogs and expected a **Choose** accessible button. Screenshots and scoped UI Automation inspection established the mismatch; the corrected seven-check run passes. Failed attempts remain alongside it.
- The first delay-calibration helper focused a non-focusable label; the corrected helper focuses the actual editable control. One reconnect helper used the pre-resolution YouTube handle after the display title changed; it now refreshes the title. These are retained checker failures, not discarded runs.
- Rumble cookie-banner persistence/clearing automation is inconclusive. Later visible inspection lacked a fresh banner baseline. A completed sign-out notice and resumed anonymous chat do not prove that every provider's authentication was revoked. Owner Twitch authentication is checked through the visible **Log Out** menu, without inspecting credentials, cookies or tokens. Fresh credentials/MFA and real server-side session expiry remain untested.
- Real YouTube broadcast rollover still requires a controlled channel ending and restarting. A channel was requested, but none was provided during this run. Real Rumble/TikTok gift extraction, authenticated Facebook/X/Instagram workflows, full Internet loss, Meshcast, physical hotplug and forced disk-full behavior remain outside this qualification.
- Long-run memory growth, occasional Kick message loss and the measured Twitch receipt latency remain open. Synthetic support fixtures from earlier reports do not prove every platform's real gifting behavior. Development signing trust remains a release limitation.

## Repository handoff

The branch contains the packaged-app fixes and reusable reconnect, file-dialog, paid-gift and media-analysis tools. Build, PowerShell/JavaScript/Python syntax, capture-scope regression, whitespace and staged secret scanning are gates; the application workflows and inspected saved recordings above provide the testing. Raw usernames/chat, browser profiles, local-control discovery files, screenshots, recordings, SSN packs and build outputs stay outside the commit.

Main began at `26ea61cfc849c6ac5e06336c78ddb7e95d61d1f6`. Its uncommitted 192 kbps recording-AAC defaults and frame-rate GOP changes are already incorporated in the integration branch; reverse-applying the original patch passes its check. The original files, hashes and binary patch are preserved in `R/main-before-merge/`. The authorized local fast-forward procedure verifies those hashes again, retains a full original stash, and restores the user's separate deletion of `native-qt/e2e/reports/.gitignore`. It writes the exact target, retained stash and final status to `R/merge.json` after this report is committed. No push is part of this handoff.
