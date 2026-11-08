# Chat profile photos and Meshcast RTMP qualification

This follows [the reconnect, gift and recording qualification](studio-chat-qualification-2026-11-08.md). Evidence root `R` is `native-qt/build-integration/release-qualification-nov8/`; `A` is `native-qt/build-integration/ui-exploration/avatar-final-nov8/`. Raw public chat, screenshots, profiles and recordings remain ignored. No public chat messages or purchases were made. Meshcast received only a synthetic moving picture, pulse audio and an already-public featured comment at a random QA stream ID.

## Changes

The native feed previously always painted an initial, and three bridge boundaries discarded the photo URL. They now retain a bounded public HTTPS avatar URL. Visible rows request photos asynchronously, decode off the GUI thread and paint a circular image over the existing initial. An absent, rejected, failed or unsupported image retains the initial and comment text. Selection and featuring retain their existing behavior; this change adds photos to the feed, not the on-air template.

The loader accepts approved platform image hosts, revalidates redirects, does not reuse sign-in cookies or credentials, limits bodies to 256 KiB and decoded dimensions to one megapixel, permits four active downloads/decodes, and limits cached pixmaps to 4 MiB. Failed URLs are temporarily cached to avoid a repaint retry loop. PNG, JPEG, GIF first frames and WebP are supported. Packaging now includes Qt's WebP plugin, its runtime DLLs and license notices; `build-mixer.ps1` fails with an actionable dependency message if it is absent.

Hidden capture pages still do not download remote images. Approved public avatar image requests receive a local transparent pixel, preserving the URL that YouTube otherwise removes after an image error. Only the visible native rows download the actual photo. No bundled SSN source was modified.

RTMP destinations now accept a server root plus a separate stream key. Previously validation required an application path even when the destination service, including Meshcast, uses `/<stream key>`.

## Exact artifacts

| Artifact | SHA-256 |
| --- | --- |
| Final portable, `native-qt/dist/desktop-studio-avatars4-nov8/desktop-studio.exe` | `ea1ed443285e64720567249513e95715c1c0f577fd4c42e01a699665f430abf2` |
| Final installer, `native-qt/dist/desktop-studio-0.9.4-avatars-nov8-setup.exe` | `1b1b372e4e7038c605997387832d25b551879549316f1de715bbf0d70f1fe76c` |
| Earlier avatar/RTMP candidate used for Meshcast transmission, `desktop-studio-avatars3-nov8/desktop-studio.exe` | `8a282d043e2fb8c6526af6dbcb3a6292219bc8ba3ff69395a221588fb73a1f19` |

The final executable adds the hidden-page placeholder fix to the Meshcast candidate. Both contain the same RTMP validation change and native image loader. SSN checksum-file hash remains `9a20654758d70c36e0855ee1f65356d425b2923c70a49263d52abbdf134974ee`. Development signing is timestamped, but Windows trust remains `UnknownError`; these are not publicly trusted release signatures.

## Final portable: actual workflows

- `A/avatar-workflow-20261108-132739/`: real YouTube, Twitch and Kick profile pictures were visually inspected in filtered feed rows. Each platform's comment remained private until explicitly featured, and reached Program when shown. Seven absent/malformed avatar fixtures retained their text, displayed initials and started no image requests. Leaving practice removed those fixtures. Cache and concurrency bounds passed.
- `R/avatar-network-fallback/`: four checks passed with an unavailable public image. The request completed, repaints did not retry it, and its comment remained selectable and featureable. The initial and saved Program screenshot were inspected.
- `A/chat-reconnect-20261108-132918/`: 17 checks passed across three YouTube remove/re-add cycles, cancelled and completed sign-out, preserved channel identities, and newly arriving anonymous Twitch, YouTube and Kick comments.
- `R/avatars-general-ui/workflows-20261108-133023-630/`: all 77 checks passed, including scene/source editing, malformed inputs, Unicode and long comments, private selection, output transitions, 70 keyboard tab stops at 1280x720, and restart persistence.
- `native-qt/build-chat/kick/2026-11-08T18-30-13-206Z/`: a five-minute independent public-socket comparison matched all 1,557 eligible occurrences, including 437 with emotes. The channel had 148,186 viewers at the initial reference query. One moderator-removed message was excluded; there were zero missing, late, unexpected or stalled occurrences, and the maximum absolute receipt difference was 4,405 ms. No gift event occurred in this window. Earlier intermittent losses remain unresolved.

Early helper failures remain in evidence: the first avatar walkthrough failed to resume the paused review feed before waiting for a different platform, then invoked an unavailable Keep up control. A later helper lost its target comment to busy-feed eviction while waiting for a screenshot; it now freezes the review list before searching. Another screenshot refused a different foreground app; the helper now refocuses before capture. The first reconnect repeat missed the channel-entry control; it now focuses Add and waits for the entry before typing. These failed runs are not counted as passes.

The first avatar package dropped URLs at the host relay. The second displayed Twitch photos but lacked the WebP plugin and still rejected a Meshcast root URL. The third fixed those, while YouTube still lost its URLs after hidden-page image errors. These intermediate artifacts are not the final candidate.

## Meshcast: independently received output

`R/avatars3-meshcast/` records the actual GUI workflow: create a show, add the moving picture/pulse source, add public chat without sign-in, select/feature a real comment, start local recording, enter the Meshcast RTMP server root and generated stream key, go live, save the recording and stop normally. All ten workflow checks passed.

- Local saved file: 425.058 seconds, 12,752 frames, complete video/audio decode, no pulse silence of 1.5 seconds, and first/last A/V offsets within 36/28 ms. All saved-media checks passed.
- Independent direct RTMP receiver: 180.011 seconds, 5,390 frames, complete decode, no long audio silence, A/V offsets within 38/31 ms, and maximum video timestamp gap 34 ms. Video starts at 0.354 seconds while audio starts at zero. Consequently the strict whole-container frame-coverage check **failed**; its result is retained. The video span itself is approximately 179.667 seconds at 30 fps with no duplicate presentation timestamps or midstream gap over 100 ms. This is not reported as an unconditional all-checks pass.
- Independent browser receiver: the Meshcast WHEP viewing route displayed the actual moving picture and featured comment. Its 40.366-second saved recording has 1,211 frames and silent audio; pulse/audio checks correctly failed. [Meshcast documents that RTMP-to-WHEP currently supports video without audio playback](https://meshcast.io/). This route is not qualified for a broadcast requiring browser audio.
- The transmitting Studio process tree averaged 4.48% CPU on this 20-logical-processor host, with peak private memory 1,669.73 MiB; other QA apps were open. One initial sampler measurement was unavailable. Across 209 active samples the avatar cache stayed at or below 4,088 KiB and four in-flight jobs; the held comment stayed on air. The final avatar count was 1,889 loaded, zero failed. This short observation does not resolve the earlier long-run browser memory growth.

## Installer, saved files and owner workspace

`R/installer-avatars/` contains a visible per-user installation of the preceding `992325d0...` build, followed by a same-version upgrade to the exact final avatar executable. The original install passed four checks, its first-use recording journey eight, and the upgrade six. The final installed app's repeated journey passed fifteen checks: preserved scenes/sources/channels, visibly inspected photos from all three platforms, private selection, a featured real comment and a saved recording.

Missing and tampered chat-pack files were each rejected while the installed app continued producing and recording picture/audio (five checks each). All four completed recording journeys pass full saved-media decoding, cadence, pulse-audio continuity and A/V checks: the original installed file is 12.373 seconds with maximum A/V offset 31 ms; missing-pack 12.432 seconds/21 ms; tampered-pack 12.460 seconds/32 ms; final upgrade repeat 12.428 seconds/38 ms. Saved frames, including the real featured comment, were visually reviewed. Evidence is `installed-media-summary.json` and the corresponding `application-*/media-analysis/` and `recording-frame.png` files.

The first final-installed attempt, `application-upgrade/`, passed fourteen checks and completed both independent observers, then failed when its final screenshot found a different foreground app. Cleanup finalized and closed Studio. That retained recording fully decodes, but has no completed wall-timing record and is excluded from the four timing-qualified files above. Refocusing before capture fixed the helper; the repeated finalization journey used a short recording because the independent longer observation had already completed. The failure remains recorded.

The longer attempt's independent Twitch comparison matched 114/114 eligible occurrences with no missing or unexplained surplus, but 63 arrived beyond the five-second target. Receipt differences were 4,245–6,143 ms, including two-second capture polling. Its latency check remains a failure; upstream page delay was not measured. Evidence: `application-upgrade/twitch-occurrence-comparison.json`.

Uninstall passed five checks, removed the owned application/registration/shortcut, and preserved all nine snapshotted shows and media files byte for byte. The final portable restored the owner's seven scenes, eight sources and four channels, leaving production idle; five restoration checks passed at `native-qt/build-integration/ui-exploration/20261106-085441/chat-restored-20261108-134059/`. No fresh WebView2 runtime provisioning was exercised.

Four visible account checks then passed in `R/owner-auth-134100/`: the existing Twitch session displayed **Log Out**, returned to ready chat, preserved the workspace and left production idle. No credentials, cookies or token values were inspected. This proves retained sign-in, not fresh credential/MFA entry.

## Scope

Build, URL-validation and secret checks are gates, not substitutes for the actual workflows above. Prior two-hour, crash-recovery, gifting and account evidence remains bound to the earlier artifacts in the linked report. Real broadcast rollover, fresh credential/MFA entry, unqualified platforms, intermittent ordinary Kick losses, Twitch delivery latency and long-run browser memory growth retain their earlier limitations. No new two-hour soak or real stream rollover is claimed for this avatar package.

## Local handoff

The earlier reconnect/recording work is committed as `a36a2c5`. The avatar and root-RTMP changes follow in a separate commit. The authorized local fast-forward uses the audited procedure documented in the preceding report: preserve a full stash of the original main edits, verify the user's application changes are already included, and retain the separate `.gitignore` deletion. Exact resulting commit and retained stash are recorded in `R/merge.json`. There is no push in this handoff.
