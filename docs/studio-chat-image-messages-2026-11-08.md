# Image-only comments and chat memory investigation

This follows the [avatar and Meshcast qualification](studio-chat-avatars-2026-11-08.md). Evidence root `M` is `native-qt/build-integration/memory-isolation-nov8/`. Raw public chat, screenshots, recordings and disposable profiles remain ignored. No chat posts, purchases or account credentials were involved.

## Change

Real Twitch GIF posts exposed a bridge bug: removing the embedded image left an empty body, which became **Membership update** for subscribers or disappeared for other authors. The bridge now preserves the image's available alternative description, capped at 256 characters, or displays **Shared an image**. A supplied caption is preserved. These remain plain-text cards; animated artwork is not rendered or downloaded by this change. Public profile photos continue to appear separately in the native feed.

Practice fixtures now pass through the same payload filtering as capture pages. Packaged UI coverage includes image-only posts with and without a subscriber badge, preserved captions, Thanks classification, private selection, and Bar/Bubble/Big output styles.

Optional `VIDEO_MIXER_CHAT_RESOURCE_DIAGNOSTICS=1` adds aggregate page/bridge counts and WebView2 process-to-host mapping to `chat.status.page_resources`. It is off by default and skips sampling while an account window is open. It records neither message bodies, credentials, storage, full URLs nor response bodies. Heap figures are approximate and cannot identify the owner of all native allocations. The normal WebView2 SDK supplies the process mapping; no browser debugging connection is opened.

## Artifacts

| Portable executable | SHA-256 |
| --- | --- |
| Final: `native-qt/dist/desktop-studio-avatars-image-final-nov8/desktop-studio.exe` | `739ab26501cf4c5ab3beffb8b29225e6c7f25027b63b806b77b9a68aec4466d0` |
| Earlier image fix, used for the independent Twitch comparison: `desktop-studio-avatars-image-text-nov8/desktop-studio.exe` | `28814e8399dc856dab7e2c9610859b94f8c9107f3561102c7a2f109ea662a9d1` |
| Rejected network experiment: `desktop-studio-avatars-network-memory2-nov8/desktop-studio.exe` | `d9cca6443a2142347dda4a3c15422171bc5a6f3a3776120c4b8c348654dd8baa` |

SSN checksum-file hash remains `9a20654758d70c36e0855ee1f65356d425b2923c70a49263d52abbdf134974ee`; bundled SSN sources are unchanged. Development signatures are timestamped, but Windows trust remains `UnknownError`. No new installer is qualified in this pass. The final source manifest is `native-qt/build-integration/release-qualification-nov8/artifact-avatars-image-final-source.json`.

## Packaged user workflows

The final executable passes **101/101** support/image workflow checks in `M/final-support/support-20261108-203212-945/`. The GIF descriptions are also exercised by real public Twitch messages, not only fixtures. Nine image descriptions independently matched the author's public reference message within 2,259–2,453 ms on the earlier image-fix build. This is description delivery, not GIF animation or a real gift purchase. Evidence: `M/candidate-twitch/image-description-comparison.json`.

The final package creates a scene with a moving picture and pulse audio, attaches Kick, YouTube and Twitch anonymously, features a real comment, starts recording, then selects and features a real Twitch image description while recording continues. The live image journey passes four checks in `M/final-kick/image-live-203335/`. Screenshots were visually reviewed: the real profile photo, readable description, private review area, subscriber badge and amber on-screen band remain clear. The fallback and all three styles were also reviewed in the support workflow. Initials remain the fallback when no usable profile photo is available.

The final package's **906.532-second** recording fully decodes, matches the 906.598-second observed window, has no video timestamp gap above 34 ms and no pulse-audio silence interval of 1.5 seconds. First/last-minute A/V offsets are at most **24 ms**; the independent moving-strip analysis finds no near-static interval longer than 67 ms. The extracted frame in `M/final-kick/image-live-203335/saved-recording-frame.png` was visually checked against the displayed real GIF description. Evidence: `M/final-kick/journey/media/` and `journey/motion/`.

Reopening passes six checks, preserving scene/source/channel identities and idle production, resuming comments, and confirming diagnostics are off by default (`M/final-kick/reopen-204801/`). The first attempt used a stale control address in the harness after restart; that harness error was corrected before repeating the full cycle. Twitch account navigation passes 14 checks, including repeated reloads, 650×600 controls and return to ready chat (`account-navigation-20261108-204825/`). The visible login form was reviewed; credentials and MFA were not entered.

The ten completed investigation recordings range from 7.2 to 35.1 minutes. All fully decode, match their observed duration, have video timestamp gaps no greater than 34 ms, and have no pulse-audio silence interval of 1.5 seconds. First/last-minute pulse A/V offsets are at most 74 ms. These are separate recordings, not one continuous soak. Evidence: `M/completed-media-summary.json` and each `journey/media/` and `journey/motion/`. A frame extracted from the network experiment's saved recording visibly contains the exact real GIF description shown on Program (`M/network-kick/image-live-200739/saved-recording-frame.png`).

A two-minute measurement of the entire final Studio process tree, after the comparison apps closed, averages **4.20% CPU** (p95 5.19%) and 1,623 MiB private memory on this 20-logical-processor host. All 24 samples show recording active, with no sampler errors. This is the moving-reference scene with three public chat channels, not a benchmark for arbitrary scenes or cameras. Evidence: `M/final-kick/cpu-observation2/`. The earlier five-minute observation is retained separately; its sampler skipped an erroneous initial zero-duration reading, which was corrected before the second observation.

## Delivery findings retained as failures

| Independent comparison | Result |
| --- | --- |
| Earlier avatar build, Twitch, 149.8 scored seconds | 1,260/1,262 matched; the two missing GIF descriptions became membership notices. Four surplus occurrences included those notices. Matched receipt differences were 2.09–2.65 seconds. |
| Image-fix build, Twitch, 569.7 scored seconds | 5,689/5,695 matched after explicitly mapping the individually verified image-description occurrences; six unmatched references and two surplus captures remain. No matched occurrence exceeded five seconds; median/max receipt differences were 2.344/2.890 seconds. |
| Network experiment, Kick, 282.3 scored seconds | 3,421 matched, nine missing, one matched late at 5.274 seconds, five moderator removals excluded, no observed stalls; 675/676 emote-bearing messages matched. |

Twitch matching consumes each author/text occurrence once, uses a five-second target with a separate fifteen-second late allowance, and polls native capture every 200 ms. Literal ordinary comments beginning with `Image:` are not globally normalized. Strict and image-aware scores are both retained in `M/candidate-twitch/full-occurrence-comparison.json`. One remaining Twitch mismatch is a clip URL with preview title and **Clipped by** text appended to it. The other unmatched occurrences are unresolved. The public-reference comparison alone does not locate every omission in Studio versus the platform's rendered page.

Kick evidence is `native-qt/build-chat/kick/2026-11-09T01-08-31-579Z/`. Its nine missing comments occurred in the first thirty-second bucket. Peak reference traffic was 78 comments per second, below the capture boundary's nominal 400-message-per-second limit; that observation does not prove a cause. No live gift event was independently observed.

## Memory investigation

The busy Kick renderer is now positively associated with the large growth seen in the earlier soak. One diagnostic interval rose from about 142 to 497 MiB while reported JavaScript heap and visible DOM counts stayed comparatively small. Browser-process private memory also grows. This narrows the investigation to the capture/runtime path; it does not establish a particular leak or exclude detached/native allocations.

Fresh copies on the same live Kick channel compare a common approximately eleven-minute wall-clock interval after warmup:

| Configuration | First/last 30-second mean chat private memory | Mean CPU cores |
| --- | --- | --- |
| Existing network inspector configuration | 720 → 878 MiB | 0.210 |
| Inspector removed, relay DNS rules substituted | 662 → 831 MiB | 0.207 |
| Same substitution plus hidden Kick artwork | 751 → 865 MiB | 0.217 |

All sampled recording intervals remained active and chat-ready, without a browser restart. The first two copies began 64 seconds apart; the artwork copy began earlier. These are not equal-age allocation experiments, and their absolute memory differences are not evidence of a fix. The larger earlier/newer three-platform comparison had the same confound. A seven-minute busy-Kick retry of WebView2's supported low-memory target also grew (484 → 573 MiB) during a quieter, shorter window; it establishes no improvement over a matched control. These experimental changes were removed. Evidence: `M/memory-comparison-summary.json`, `M/paired-ten-minutes.json`, and the per-run observations.

The [WebView2 memory target is explicitly best effort](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2_19?view=webview2-1.0.3537.50). Neither that setting nor a lower initial footprint is treated as a leak fix. The three-platform network candidate still reached about 1.7 GiB in 33 minutes; its planned two-hour qualification was stopped when the experiment failed to resolve growth. The final package retains the existing capture and network-isolation behavior.

After startup warmup, the final fifteen-minute recording averages 1,262 MiB of chat private memory, peaks at 1,356 MiB and averages 0.412 CPU cores. Its first/last two-minute memory means still rise from 1,151 to 1,319 MiB. Every sampled interval remains recording, chat-ready and on screen, with one browser PID throughout. Thus the final image fix does **not** resolve the memory issue. Evidence: `M/final-observation/summary.json`.

## Remaining qualification

Busy-chat memory growth, occasional missing/altered comments and Twitch clip-preview text remain open. A new continuous two-hour run on the final image-fix package is not completed by the shorter investigation recordings. Real YouTube rollover, fresh credential/MFA sign-in, independently verified gifts/purchases and the other unqualified platforms remain outside this pass. Build and capture-scope regression checks are gates only; the packaged workflows and decoded recordings above supply the end-to-end testing evidence.

All owned QA applications closed normally after the final workflows; two earlier baseline windows that minimized to the tray were terminated only after their recordings finalized and idle ownership was verified. The owner's original avatar-build window remains open, with seven scenes, eight sources, four channels and idle production verified. It was not replaced with this candidate. `M/final-audit.json` confirms that the final package matches its source manifest and no owned QA app remains running. Screenshots, browser profiles, raw chat and recordings are excluded from the commit.
