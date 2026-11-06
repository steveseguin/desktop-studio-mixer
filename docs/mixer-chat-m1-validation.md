# Native chat integration: packaged validation

Updated 2026-11-06 UTC. The latest work is in `C:\Users\Steve\code\video-mixer-chat-integration`; earlier sections identify results from `C:\Users\Steve\code\video-mixer-chat` and their original builds. Main's application edits were not changed. Results distinguish packaged application workflows from code/build gates.

## Capture rendering follow-up, 2026-11-06 UTC

Worktree: `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`, based on `eb8ee2b`. Main's existing edits remain untouched. This section supersedes the artifact selection below; historical results still apply only to their recorded builds.

Current review package: `native-qt/dist/desktop-studio-chat-memory-nov6/desktop-studio.exe`, SHA-256 `e1657afcd5d705d93a1589f192482f76d2a65865e75252fee08231588d1488a0`. The standard `desktop-studio-0.9.4-windows-x64.zip` now contains this package. The explicitly named integration ZIP and installer remain the November 5 build; the installer has not been rebuilt for this change. The same verified SSN pack was reused without capture-script changes. Signing still uses the local certificate with trust status `UnknownError`.

### Change and evidence

Capture pages previously set their native WebView2 controller to invisible. SSN's JavaScript reported the document as visible, but animation callbacks were suspended: zero callbacks in a five-second probe, versus 302 in an ordinary Studio browser source. The capture controller now stays visible inside its hidden owner window. The patched capture ran 302 callbacks in the same probe. Windows enumeration found no visible top-level windows for any of the three running patched chat workers. Engine/host visibility settings and resource limits are unchanged.

The old/new live YouTube comparison reproduced and stopped one source of retention. At 6.5 minutes both pages held about 35,000 DOM nodes and 250 visible chat rows. By minute 20, the old page held 70,616 nodes and a 69 MiB JavaScript heap; the patched page held 35,288 nodes and a 42 MiB heap. Across minutes 6.5–20 the patched page stayed between 34,796–35,680 nodes and 40–42 MiB of heap. The old run continued to minute 25, reaching 81,875 nodes and 77 MiB. An ordinary browser-source control without SSN also levelled off after its visible list filled.

**Total memory is not qualified as fixed.** In the patched run, overall chat private memory rose from 658 MiB at minute 6.5 to 717 MiB at minute 20, with a final-three-minute mean of 711 MiB. Rendering may add costs beyond retained JavaScript objects. This is diagnostic evidence, with DevTools attached, and does not replace the uninstrumented two-hour recording soak. The old run also used allocation sampling and a detached-node query. No forced garbage collection or heap snapshot was used in this comparison. Evidence: `native-qt/build-chat/page-metrics/2026-11-06T04-29-39-242Z/` (old), `2026-11-06T04-35-15-723Z/` (patched), and `native-qt/build-integration/youtube-control/` (ordinary browser).

Completed workflows on this exact package (all paths below are in this integration checkout):

| Workflow | Result | Evidence under `native-qt/` |
| --- | --- | --- |
| Live YouTube, ten minutes | 329/329 reference comments, including 155/155 containing emoji; reference held, no unscorable items, one page load | `build-chat/youtube/2026-11-06T04-35-25-910Z/` |
| Live Twitch plus YouTube, ten minutes | Twitch 2,748/2,748 eligible reference occurrences; four moderation exclusions. Show reached composition in 268 ms; chat recovered after browser termination in 1.941 seconds. Original workflow result was 19/20 because of the capped-count harness defect described below | `build-chat/live-e2e/2026-11-06T04-45-51-602Z/` |
| Corrected live workflow, two-minute comparison plus recovery/restart | 20/20; Twitch 556/556 eligible reference occurrences, one moderation exclusion; Show 266 ms, recovery 1.936 seconds, fresh comments after recovery, saved channels after full restart, recording decoded | `build-chat/live-e2e/2026-11-06T04-57-49-296Z/` |
| Live Kick, ten minutes, channel live with 41,144 viewers at start | Strict comparison failed: 1,834/1,894 eligible occurrences after five moderation exclusions; no stalls and reference held. All 744 plain-text occurrences matched; 60 eligible emote-containing mismatches remain. Eight reported examples were found in captured data with spacing differences; that diagnostic does not prove all 60 are formatting-only | `build-chat/kick/2026-11-06T04-45-51-602Z/` |
| Moving-picture recording with live YouTube capture | 60 seconds decoded for 60.615 seconds wall time; seven distinct repeating reference frames, no consecutive identical one-second samples, no decode errors | `build-chat/soak/2026-11-06T04-44-30-472Z/` |
| Windows accessible UI walkthrough | 22/22; screenshots visually reviewed at 1280 px. Chat selection and amber on-screen actions are legible; wider mixer source controls remain cramped | `build-chat/ui-walkthrough/20261105-234507/` |
| Forced chat-memory overload while recording | 8/8; recovery 16.392 seconds, peak 3,004 MiB against 3,072 MiB cap, worst control response 7 ms; recording fully decoded and covered recovery | `build-chat/overload/2026-11-06T04-49-29-788Z/` |

The soak harness now accepts `--record`: it generates a moving reference using the package's FFmpeg, records through the real application, checks recording status and disk space, then decodes the entire video and checks duration and frozen-picture intervals. A full 120-minute run requires 12.5 GiB free; its preflight correctly refused to start with about 1.4 GiB available. Two initial short harness attempts failed before validation (the LGPL FFmpeg lacks libx264; recording-active precedes file creation); the harness now uses MPEG-4 for the input fixture and waits for the recording file. The successful run above followed those corrections.

The busy Twitch run exposed another harness defect: recovery waited for the panel's comment count to increase even after reaching its 2,000-comment cap. The application was still receiving comments: a direct check during the failed wait found 16 new IDs in three seconds with both counts fixed at 2,000 (`capped-recovery-check.json` in that run). `chat-live-e2e.mjs` now checks for fresh IDs. The original 19/20 result is preserved; the independent ten-minute delivery comparison was unaffected. This passing delivery window does not establish that the previously intermittent Twitch loss is fixed.

The package and ZIP executable hashes match. All 28 chat code gates passed with exit code 0; JavaScript syntax and whitespace gates passed. These are gates, separate from the packaged application workflows above.

The full two-hour recording remains blocked by disk capacity. Cleanup inventory identified 6.42 GiB in 120 inactive QA browser profiles, but automatic approval review rejected both the bounded batch and a single explicitly named profile, despite user authorization, with only `blocked by policy`. No files were deleted. Reports, screenshots, recordings and profile data remain present. Real stream rollover, other-platform qualification, intermittent Twitch losses and Kick spacing fidelity remain open until supported by new evidence.

## Integration and sustained validation, November 5, 18:15 UTC

The new integration checkout is `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`. It combines current main (including the CLA) with the chat branch. Three existing local application edits from main were copied into this checkout for compatibility testing: the named 192 kbps recording AAC default, the file-output default, and the canvas-rate keyframe interval. The original main checkout and its unrelated deleted report ignore-file remain untouched.

**Try:** `C:\Users\Steve\code\video-mixer-chat-integration\native-qt\dist\desktop-studio-chat-integration-nov5\desktop-studio.exe`.

- Integration executable SHA-256: `2b0bca270ef351e8268897b0f791ae72651e77f3d74763d42df7fe87e4f9ce9c`.
- Integration installer `native-qt/dist/desktop-studio-0.9.4-setup.exe` SHA-256: `b3e6ac16bb041607c38264b18127afccfebc3bacffe4b297bf41d8b506676bba`.
- Complete integration ZIP: `native-qt/dist/desktop-studio-0.9.4-integration-windows-x64.zip`. The standard `desktop-studio-0.9.4-windows-x64.zip` in the integration checkout was replaced with an identical copy of this qualified archive; the incomplete pre-FFmpeg archive is no longer offered there.
- The existing qualified SSN pack and FFmpeg bundle were reused. No upstream capture script was changed. Signing uses the same local certificate, with trust status `UnknownError`.

### Findings that remain open

1. **Memory growth reproduced.** A two-hour three-platform run on the earlier `4ecfce8d...` executable kept receiving comments without a restart, but mean chat-process memory increased from **990 MiB** in minutes 2–5 to **2,172 MiB** in the final three minutes. Peak job memory was **2,207 MiB**, below the 3,072 MiB cap. This is not a memory-stability pass. CPU averaged 0.196 core. The run observed 13,054 Twitch, 6,494 YouTube and 9,345 Kick comments; each platform's longest observed gap was 31 seconds. One oversized capture packet was rejected/countable. This was a chat soak, not a two-hour recording. Evidence: original chat checkout, `native-qt/build-chat/soak/2026-11-05T14-34-16-283Z/`.
2. **Intermittent Twitch loss remains unresolved.** Ten-minute runs matched 822/823 eligible comments on `4ecfce8d...` and 749/750 on the integration executable. Each missed one ordinary comment, after documented moderation exclusions. The second comment reached the page DOM and capture API, narrowing the loss to later processing. A subsequent integration run matched 636/636 and passed 20/20 workflows, but it had diagnostic JavaScript wrappers in the capture/engine/host pages and does not erase the failures. Nothing was posted to Twitch. Evidence: original chat checkout, `build-chat/live-e2e/2026-11-05T14-34-36-676Z/`, `2026-11-05T14-48-36-674Z/`, and `2026-11-05T15-07-44-604Z/`.
3. **Kick spacing fidelity.** The 30-minute independent comparison on `4ecfce8d...` matched 2,174/2,179 author/content occurrences: all 946 plain-text and 1,228/1,233 emote-containing comments, with no stalls and a connected reference. Inspection found the five remaining comments in Studio with spaces between adjacent emotes removed. They are formatting mismatches, not evidence of five dropped comments; the strict check still fails. This does not resolve the previously observed intermittent losses. Evidence: original chat checkout, `build-chat/kick/2026-11-05T14-32-41-400Z/`.
4. **Real broadcast rollover and remaining platforms remain unqualified.** Controlled rollover below substitutes a local ended signal. Rumble's Newsmax popout was empty in both the actual page and Studio for four minutes. TikTok produced seven visible authored comments plus one fan-club notice in three minutes, but did not establish sustained viewer-comment delivery. A separate five-minute diagnostic with explicit scroll pulses produced four ordinary text comments, two empty/unsupported items, gifts and notices; timed source screenshots also stopped showing new viewer comments. Do not count its 29 items as 29 viewer comments. Facebook, Instagram and X still need sign-in support.

A separate integration per-page diagnostic showed growing YouTube DOM/listener counts despite a roughly 250-row visible list. Explicit garbage collection reduced 67,447 nodes to 67,111 and 19,661 listeners to 19,527. This is a lead, not a root-cause finding. A heap-snapshot attempt timed out and coincided with capture-page restarts, so that diagnostic cannot qualify natural stability. The two-hour soak above had no DevTools attachment or forced collection. Evidence: original chat checkout, `build-chat/page-metrics/2026-11-05T14-58-03-597Z/` and ignored `build-chat/youtube-gc-diagnostic.json`.

### Completed application workflows

| Workflow | Result | Evidence |
| --- | --- | --- |
| Original package, live YouTube, 30 minutes | 677/677 eligible plain-text comments; independent Live chat reference held; one page load. Non-plain content was not scored in this run. | Original checkout `build-chat/youtube/2026-11-05T14-35-25-691Z/` |
| Integration package, live YouTube including emoji, 10 minutes | 424/424 eligible comments, including 95/95 with emoji; reference held; one page load. One additional whitespace-only reference item was not scored. | Original checkout `build-chat/youtube/2026-11-05T15-06-19-928Z/` |
| Original package, forced chat-memory overload while recording | 8/8; chat recovered in 17.475 seconds, peak measured 2,963 MiB, worst control response 21 ms; full recording decoded and covered recovery. | Original checkout `build-chat/overload/2026-11-05T14-43-56-412Z/` |
| Integration package, controlled YouTube rollover | 7/7. Local old-page status changed to ended; native recovery reached another real live broadcast on the same channel in 11.751 seconds, preserved one channel and showed a real comment. Restart restored identity and comments on the original still-live video. | Original checkout `build-chat/rollover/2026-11-05T14-51-34-676Z/` |
| Integration package, Windows UI Automation | 22/22; screenshots visually reviewed at 1280 px. Amber on-screen band and actions remain legible. Wider Studio source controls are still cramped. | Original checkout `build-chat/ui-walkthrough/20261105-094608/` |
| Integration installer, installed chat | 21/21; 500/500 distinct tokens at 50/s; Show Bar/Bubble/Big 313/244/300 ms, worst command 8 ms; browser recovery 1,416 ms; recording fully decoded. | Integration checkout `e2e/reports/chat-integration-nov5-qualified/chat/` |
| Integration install/upgrade, damaged pack, CLI/MCP, recording, uninstall | Pass. Stale assets removed on upgrade; missing/tampered pack does not stop recording; portable/ZIP/installed executable hashes match; 12 distinct motion screenshots, maximum frame gap 34 ms; no owned descendants or installer registration left. | Integration checkout `e2e/reports/chat-integration-nov5-qualified/` |

Evidence paths in this table are relative to each checkout's `native-qt/`. Real usernames, screenshots and capture profiles stay in ignored evidence directories. The recorded live-comment frame was also visually reviewed. No public chat messages were sent. Public Rumble discovery used its [live directory](https://rumble.com/browse/live); qualification relies on the actual source-page observations, not search snippets.

### Fixes and harness corrections in this follow-up

- A fresh build exposed a stale relay-patch checksum after a branding-only comment change. Restoring the original dependency comment makes the existing pinned checksum valid; the integrity guard remains in place.
- Release packaging now refuses a missing FFmpeg bundle before signing or archiving. The fresh integration worktree initially lacked that ignored dependency; the qualified package uses the existing bundle.
- Live harnesses now wait for the control server's actual health response, rather than treating discovery-file creation as readiness. Soak/page diagnostics accept the intended executable, record artifact metadata, support Kick and preserve incremental evidence.
- YouTube comparisons now retain and score emoji accessibility text, including emoji-only comments. Unknown message runs fail qualification rather than silently becoming plain text.
- The recorded-output decoder check now uses timestamp passthrough and a microsecond time base. Its default null encoder had rounded distinct 33/34 ms source timestamps into duplicate DTS warnings. Inspection of all 200 source frames confirmed increasing timestamps; decoding at full precision succeeds. The installer workflow was repeated successfully afterward.
- The initial controlled-rollover harness wrongly expected a still-live original video to be treated as ended after restarting. That expectation was corrected explicitly; no product behavior was changed to satisfy it.

The integration remains on a separate local branch. The next release blocker to investigate is sustained memory growth, followed by the intermittent Twitch loss and remaining platform/real-rollover qualification.

## Earlier package, qualification follow-up

Use `native-qt/dist/desktop-studio-chat-qualification-nov5/desktop-studio.exe`. It replaces the review build as the current artifact, while both earlier portable folders remain available. The only application change since the 13:00 review is honest setup wording: Twitch, YouTube and Kick are named; other platforms are experimental and sign-in is unavailable. English, Spanish and French text was updated. The SSN capture pack's `SHA256SUMS` is identical to the review package.

- Executable SHA-256: `4ecfce8da1bd976c5b5404f87dcdd2fbfef1f331baf2a4ee9c60235e436917f9`.
- Installer `native-qt/dist/desktop-studio-0.9.4-setup.exe` SHA-256: `1e62e83b2982f3b9c455f68ad0ae2915d98054962c8ca1ca939d9331b802b73a`.
- The current ZIP contains `desktop-studio-chat-qualification-nov5`. Local-certificate signing still reports `UnknownError`; publicly trusted signing is not established.
- Windows UI Automation: **22/22** assertions on this executable. First-run and on-air screenshots visually reviewed at 1280 pixels: support copy wraps without clipping, and amber controls remain readable. Evidence: `native-qt/build-chat/ui-walkthrough/20261105-083732/`.
- Live Kick: **165/165** matching occurrences over two minutes on `absi` (18,785 viewers), including **104/104 emote-containing messages**, reference held, no stalls. Evidence: `native-qt/build-chat/kick/2026-11-05T13-37-51-556Z/`. The final comparator also scopes collection to the selected channel, requires it to be live and requires a successful reference subscription.
- Installer workflow: fresh install, upgrade with stale-asset removal, missing/tampered-pack recovery, installed chat, CLI/MCP, recording and uninstall all passed. Installed chat **21/21**, 500/500 tokens at 50/s, Show Bar/Bubble/Big **244/244/288 ms**, worst command 8 ms, browser-kill recovery 1,071 ms. Recordings fully decoded, 12 distinct moving-video screenshots, maximum frame gap 34 ms, portable/ZIP/installed executable hashes matched, and no owned descendants or installer registration remained. Evidence: `native-qt/e2e/reports/chat-qualification-nov5/`.
- Release/package/installer build, 28 chat gates, script syntax and diff-whitespace gates pass. These gates are not application testing.

No two-hour soak, forced-memory-overload, real broadcast rollover or live Twitch/YouTube comparison was repeated on this exact executable. Earlier results remain tied to their recorded artifacts. Rumble and TikTok observations below do not qualify those platforms.

## Further platform observations, 13:23–13:36 UTC

These observations used the 13:00 reviewed executable below. Nothing was posted to a public chat.

- **Kick including emotes:** 665/665 occurrences matched by author, text and emote names in five minutes on live `absi` (18,789 viewers): 314 plain-text and 351 emote-containing comments. The independent socket reference held; no stalled 30-second buckets. Evidence: `native-qt/build-chat/kick/2026-11-05T13-25-50-641Z/`. The comparator now decodes Kick's explicit emote tokens to their displayed names, preserving surrounding text. It still compares occurrence counts, not native message IDs. A successful five-minute interval does not resolve the earlier intermittent losses.
- **Rumble:** `https://rumble.com/c/RTNews/live` resolved to the real RT News popout (`/chat/popup/186945454`). Both the source page and Studio were empty for 90 seconds. The page offered sign-in to post; this alone does not establish that reading requires authentication. Capture remains unverified. Evidence: `native-qt/build-chat/platform-inspect/2026-11-05T13-27-04-215Z-rumble.com/`. The earlier `/c/RT/live` was an incorrect channel slug and reached a real 404.
- **TikTok:** three logged-out runs on a publicly live channel did not establish sustained viewer-comment capture. The first delivered 14 notices/like/gift items while viewer comments were visible; those 14 must not be reported as viewer comments. The next delivered five authored comments matching visible rows, one fan-club notice and one gift. Its Program screenshot shows the gift, not a verified viewer comment. A third run, with an explicitly recorded diagnostic scroll pulse, had no viewer rows visible and no captured items; it neither proves a fix nor isolates a defect. Evidence: `native-qt/build-chat/platform-inspect/2026-11-05T13-23-36-599Z-www.tiktok.com/`, `2026-11-05T13-31-53-306Z-www.tiktok.com/`, and `2026-11-05T13-33-50-002Z-www.tiktok.com/`. Source and Program screenshots were visually reviewed. TikTok remains unqualified.

`tools/chat-platform-inspect.mjs` now records source-page snapshots, captured items, native status/resources and composed output in an isolated packaged-app session. It deliberately reports **items requiring review**, since system notices and gifts must not qualify as viewer comments. Its optional `--pulse-scroll` modifies the observed page for diagnosis and must be disclosed in results; it is not a shipped fix.

## Follow-up review, 13:00 UTC

Reviewed executable at 13:00: `native-qt/dist/desktop-studio-chat-review-nov5/desktop-studio.exe`. The original portable directory was preserved. At that time the ZIP `desktop-studio-0.9.4-windows-x64.zip` contained the `desktop-studio-chat-review-nov5` directory and the installer was `desktop-studio-0.9.4-setup.exe`; those two shared filenames have since been replaced by the current package above. Historical artifact references below describe earlier builds.

- Executable SHA-256: `f73cda6cf27a3d153b54aedd9d6e8a5e7071b91704841965a4b697df16bb33de`.
- Installer SHA-256: `f386b42f04cd416a4d8d5399401dc3d9da0d5b171efdd9979c65dd104220e4a7`.
- Both were signed using the local Social Stream Ninja certificate. The tool reports trust status `UnknownError`; this does not establish publicly trusted signing.

Two additional findings were fixed:

1. Kick/YouTube comparison scripts returned success with up to 10% unexplained missing comments. They now fail unless every eligible plain-text occurrence matches. This does not invalidate the earlier measured 100% results, but closes a false-pass condition for future runs.
2. Oversized capture data was still discarded without an operator notice, and oversized reply text could be silently truncated. Native rejection now increments `chat.status.oversized_messages`, and the panel shows "Some chat data was too large to display. Comments may be missing." The count covers the current worker session and resets on restart. Limits remain intact. The warning does not cancel Show or mark the engine unavailable. English, Spanish and French copy is supplied.

Actual application testing on this executable:

| Workflow | Result | Evidence under native-qt/ |
| --- | --- | --- |
| Strict Kick comparison, live 3li_boltx, 13,928 viewers, 120 seconds | 130/130 eligible plain-text comments; reference stayed connected; no stalled bucket; 70 reference comments containing emotes excluded from content scoring | `build-chat/kick/2026-11-05T13-00-50-093Z/summary.json` |
| Strict YouTube comparison, rFZHOHl-L8A, 120 seconds | 23/23 eligible plain-text comments; Live chat reference held; one page load; 17 non-plain comments excluded | `build-chat/youtube/2026-11-05T13-00-50-093Z/summary.json` |
| Isolation and size boundaries | 6/6 checks. Oversized body, oversized reply and >32 KiB envelope rejected/countable; a valid comment with a 40 KB unused badge arrives. Spoof protection, emote alternative text and targeted deletion still pass. | `build-chat/isolation-review/2026-11-05T13-00-08-571Z/results.json` |
| Windows UI Automation at 1280 px | 22/22 assertions, including selection, Back to live, countdown, Keep, private break/resume and Hide | `build-chat/ui-walkthrough/20261105-080008/results.json` |
| Installed chat workflow | 21/21 checks; 500/500 tokens at 50/s, no missing/duplicate/unexpected messages; worst command 8 ms; Show Bar/Bubble/Big 242/233/286 ms | `e2e/reports/chat-review-nov5/chat/results.json` |
| Missing and tampered pack | Chat unavailable, recording continues and fully decodes in both cases | `e2e/reports/chat-review-nov5/integrity/results.json` |
| Install/upgrade/uninstall, CLI/MCP, recording | Pass. Upgrade removes stale SSN asset; portable/ZIP/installed hashes match; 12 distinct moving-video screenshots, max recording frame gap 34 ms, full decode; no owned descendants or installer registration remain | `e2e/reports/chat-review-nov5/installed-results.json` |

Visually inspected `build-chat/isolation-review/2026-11-05T13-00-08-571Z/01-capture.png`: the new warning wraps visibly below the on-air band while the normal comment is displayed. Also inspected `build-chat/ui-walkthrough/20261105-080008/03-on-screen.png`. The broader cramped source controls remain a known layout issue.

Release build/package/installer and 28 chat gates pass; the diff whitespace gate passes. These are gates, not application testing. The exact current build has not had another two-hour soak, forced-memory-overload run, real stream rollover, or Twitch-versus-IRC comparison. Those earlier results apply to their recorded builds. Other platform and emote-content limitations below remain open. No public chat messages were posted.

## Result

The native chat workflow is usable with real Twitch, YouTube and Kick. It is **not yet qualified as a reliable all-platform release**: Rumble, TikTok, Facebook, Instagram and X remain unproven, memory is substantial, and real stream rollover needs qualification.

The earlier 05:44-local results are under [Latest build](#latest-build). They were run on `native-qt/dist/desktop-studio-portable/desktop-studio.exe`. The follow-up above supersedes their installer/UI qualification gaps. The older sections remain as a record of their specific builds.

Older package: `native-qt/dist/desktop-studio-chat-validated/desktop-studio.exe` or `native-qt/dist/desktop-studio-chat-validated.zip`. Installer: `native-qt/dist/desktop-studio-0.9.4-setup.exe`.

- Executable SHA-256: `0ffe42d773c029264386820d5f6a376d6ea65aeff5b31770b42e4bad236966db`
- Installer SHA-256: `068826837802e29f10e243603906626dd433f83ef65ab15889f4cf848a41f7b1`
- Portable, ZIP and installed executable hashes match. Both executable and installer were signed with the local Social Stream Ninja certificate; signing-tool output reports trust status `UnknownError`, so this is not a claim of publicly trusted signing.
- The separate `desktop-studio-portable` directory was rebuilt by the pre-existing fix process and has a different hash. Use the explicitly named validated artifact for these results.

## Latest build

Unsigned portable build, `native-qt/dist/desktop-studio-portable/desktop-studio.exe`, SHA-256 prefix `42afc86f9abf787f`. Chat logs and reference captures under `native-qt/build-chat/` contain real usernames and must stay out of git. All channels were checked as live immediately before these runs. Nothing was posted to any chat.

| Workflow | Result | Evidence under native-qt/build-chat/ |
| --- | --- | --- |
| Live Twitch + YouTube | 20/20. Twitch compared with anonymous IRC: 147/147 occurrences by author and text. First Twitch comment 6.84 s, Show 269 ms, browser-kill recovery 1.93 s, worst control 11 ms, 773 MiB total | `live-e2e/2026-11-05T10-45-03-910Z/` |
| Kick against its public socket, `3li_boltx`, about 13,700 viewers, 5 minutes | 341/341 plain-text comments by author and text, no stalled interval. 373 of the 714 reference comments contain emotes and are not text-compared | `kick/2026-11-05T10-48-14-774Z/` |
| YouTube against its "Live chat" feed, 5 minutes | 71/71 plain-text comments, 1 page load | `youtube/2026-11-05T10-53-33-978Z/` |
| Memory overload | 8/8. Restart about 17 s, recording fully decodes, control stays responsive | `overload/2026-11-05T10-58-48-586Z/` |
| Comment review and isolation | No failures | `review-e2e/`, `isolation-review/` |

An earlier build of the YouTube comparison, 217/217 on a stream of about 11,000 viewers over 10 minutes, and 41/41 on `4xDzrJKXOOY` over 30 minutes, is under `youtube/2026-11-05T09-57-12-690Z/` and `youtube/2026-11-05T09-57-12-709Z/`. Those runs scored against text only, before the stricter comparison below.

**Two-hour soak** (`soak/2026-11-05T07-53-45-939Z/`), run on a build before the Kick, reply, status and message-size fixes, with Twitch and YouTube confirmed live: no restarts, one browser process, never not-ready, CPU mean 0.11 core, memory p50 872 MiB, maximum 1,019 MiB, job peak 1,030 MiB of a 3,072 MiB limit. Mean memory moved from 752 MiB in minutes 2-5 to 902 MiB in the last 3 minutes. Twitch delivered 14,888 comments with a longest gap of 91 s. YouTube delivered 215 with a longest gap of 632 s, which failed the soak's 300 s limit. The later 30-minute comparison on the same stream matched every plain-text comment, and YouTube's own feed went quiet for up to 446 s, so that failure is probably a quiet channel. It ran at a different time, so this is strong evidence, not proof.

An earlier two-hour soak grew to 1,462 MiB. The YouTube stream in that run had ended 23 s before it began, so it was effectively Twitch only, and it is not evidence for two live channels. The growth did not recur, but its cause was not identified, so it should not be called fixed. If it returns, take a longer per-process trace and compare with `--disable-gpu`.

### Defects found and fixed against live chat

- **Kick stopped capturing after 1-2 minutes** on busy channels: 39% matched with no fix. Hidden pages get no scroll events, and Kick's virtual list only renders new rows on one. Capture now dispatches one every 100 ms on Kick popouts. A timer standing in for animation frames did not help (24%).
- **Comments with large embedded images were silently dropped** at the 32,768-character message limit. A Kick moderator with a Founder badge, whose icon embeds an image, lost every comment. Rumble avatars are converted to base64 too, and TikTok, X and Instagram sources also contain image-to-data-URL conversion, so they may have been exposed. Capture pages now send only the fields the host reads. Oversized messages are still dropped without a report, and badges will need another route when the panel shows them.
- **Replies quoted the whole parent comment.** They now read `@name reply`. On the four sources that set these fields (Twitch, YouTube, Kick, Facebook) `reply` has the same format as `chatmessage`.
- **"Reading comments" never cleared.** It now changes to "No new comments in 5 minutes". Removing the comment list from a live Twitch page changed the status after 299.7 s. SSN also reloads quiet YouTube pages, which briefly shows "Waiting for comments". No comments were lost across those reloads.
- **YouTube's `endTimestamp` is present while live**, so `isLiveNow` now decides whether a stream has ended. Attaching to a page no longer resets the retry backoff; only a confirmed live stream does.
- **The memory limit was too low for real use.** It is now a quarter of system memory, clamped to 1.5-3 GiB, replacing the 1,024 MiB cap described below. Memory recovery triggers at 15/16 of the limit. `googlevideo.com` is blocked in capture pages, and the page probe's cached data is slimmer.

### Comparison tools

`kick-compare.mjs` and `youtube-compare.mjs` were added. After review, they and `chat-live-e2e.mjs` gather panel comments during the run instead of reading one final snapshot, and score only comments that arrived in the scored time. Kick and YouTube match on author and text, as Twitch does. They fail if the reference stops. A missed comment is excused only when a moderator removal follows within 2 s (Twitch timeouts) or 5 s (message deletions and Kick bans), and these are counted separately. `chat-soak.mjs` reports per-channel flow and no longer treats a failed read as new comments.

The strict Twitch check had failed on earlier builds. The 856/858 result traced to two comments from users timed out about 0.3 s later, which Twitch removed before the page showed them. The earlier 208/211 result's three misses were @replies.

### Not yet proven

- Real stream rollover, where one broadcast ends and the next begins.
- Rumble and TikTok on live channels. Facebook, X and Instagram need sign-in, which Studio does not have yet. The Kick scroll problem could exist on similar virtual lists.
- A two-hour soak on this exact build, and a long YouTube run on a busy channel.
- Emote-only comments are not compared by content. Panel totals stayed in line with reference totals, which is weaker evidence.
- A 5-minute Kick run at 100% is one window. Earlier runs on nearby builds lost a small number of ordinary comments, between 0.7% and 1.5%, without an identified cause, and the same may still occur.
- Very long emote walls could exceed the 8,000-character comment limit once converted to HTML. The largest seen in testing had 20 emotes, so this is calculated, not observed.
- The installer, the pack integrity workflow, and the Windows UI walkthrough were not re-run on this build. The new status text has not been inspected on screen.

## Fixes reviewed and exercised

- Chat now runs under whole-process-tree limits: 1,024 MiB hard commit cap (since replaced, see [Latest build](#latest-build)), recovery threshold 960 MiB, and CPU capped at two logical cores or 25%, whichever is lower. Removed CPU-saturation-triggered restarts: ordinary startup could legitimately hit the quota and previously restart forever. Memory recovery and heartbeat failure recovery remain active.
- Native frame/source validation rejects opaque advertising frames impersonating a channel. Capture resources and injected script groups are restricted. Settings and browser profiles have ownership locks; pack contents are verified before execution.
- Rich comment conversion now preserves image alternative text (emote names). The prior conversion silently lost it.
- Source deletions now remove the targeted comment from the feed and Program, preserving the author's other comments. Identity includes channel and capture session, preventing collisions across sources/restarts. Fixed a QJsonValueRef insertion bug that corrupted source IDs; invalid deletion IDs cannot broaden into author-wide deletion.
- Fixed the empty on-air object mutation that made Show cancel itself. Reduced overlay readiness/polling delays; actual composed-output timings are below.
- Corrected selection handling and programmatic-scroll pauses. Added an always-available **Back to live** action while paused, including when no new comments have arrived. Qt's stock SingleSelection accessibility provider refuses to remove its last selected row; the explicit accessible action is the verified route. See [Qt implementation](https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/widgets/accessible/itemviews.cpp).
- Native rows now have initial avatars, author/platform styling, donation badges and selection indication. On-air status is an amber band with countdown, Keep and Hide. Styles are three buttons. Scene controls fit at 1280 px.
- Reserved channel routes and unsafe inputs are rejected; nonexistent YouTube handles reach a terminal error. Saved YouTube channel resolution/backoff/rollover logic was added, but actual end-to-next-broadcast rollover remains untested.
- Explicit QSettings format/scope fixes isolated preference persistence. Spanish/French preferences survive restart. Test profiles now use their own data directories.

## Actual application testing

| Workflow | Result | Evidence under native-qt/ |
| --- | --- | --- |
| Original packaged workflow | Completed, 25 successful observations; 500 messages at 50/s; worst control response 6 ms | `build-chat/workflows/2026-11-05T04-10-15-777Z/results.json` |
| Installed chat regression | 21/21 checks pass; exactly 500/500 expected tokens, no missing/duplicate/unexpected messages; worst command 6 ms | `e2e/reports/chat-final-20261104/chat/results.json` |
| Installed style latency | Bar 243 ms, Bubble 237 ms, Big 258 ms, measured from command through composed state | same report |
| Live Twitch + YouTube | 305 Twitch and 6 YouTube messages observed; first Twitch 6.87 s; Show 268 ms; worst control 10 ms | `build-chat/live-e2e/2026-11-05T04-08-46-162Z/results.json` |
| Independent Twitch comparison | **FAIL**: 223/225 exact occurrences (99.11%); IRC reference covered the interval. One unmatched reply exists with a different quoted-reply format; one author's message is absent. No tolerance was used to turn this into a pass. | same report, `captured.json`, `truth/` |
| Live browser termination | Recovery 1.95 s; selected comment stays private; recorded real comment survives; recording decodes; channels and delivery return after full app restart | same report |
| Source/frame isolation | Opaque frame spoof blocked; emote alternative text preserved; targeted deletion removes on-air comment and preserves companion | `build-chat/isolation-review/2026-11-05T04-14-00-640Z/results.json` |
| Actual memory overload | Allocated/touched memory in the owned capture process; resource recovery reported, browser PID changes, recording stays active and fully decodes | same report |
| Windows UI Automation, 1280 px | All assertions pass: row selection/private preview, Back to live, Show, countdown, Keep, Private break/resume, Hide; controls checked for bounds | `build-chat/ui-walkthrough/20261104-230915/results.json` |
| Missing/damaged chat pack | Chat unavailable in both launches; video continues recording and fully decodes | `e2e/reports/chat-final-20261104/integrity/results.json` |
| Installer upgrade | Fresh private install, deliberately stale SSN asset, reinstall removes asset; installed chat regression passes | `e2e/reports/chat-final-20261104/` |
| Installer CLI/MCP/video/uninstall | Pass: 12 distinct portrait screenshots of moving video, decoded recording, max frame gap 34 ms, no owned descendants, shortcuts/registration removed, binary hashes match | `e2e/reports/chat-final-installed-20261104/installed-results.json` |
| Themes/language persistence | 17/17 setup and restart assertions; all five palettes plus Spanish/French screenshots | `build-chat/offline-workflow/2026-11-05T04-05-33-747Z/` |

The first installer run completed upgrade, integrity and chat checks, then encountered a missing **development harness** MCP dependency. It uninstalled cleanly. After `npm.cmd ci` in `tools/mcp`, a fresh install completed the remaining CLI/MCP/video/uninstall workflow. This was not an installed-product dependency failure.

The final release build/package/installer completed. The 27 chat gates pass, and the tracked diff whitespace gate passes with CRLF recognized. **Those gates are not application testing.**

## Memory and delivery limitations

The live two-channel snapshot was 757 MiB for the whole Studio tree, including 717 MiB WebView2. Chat job private commit was 728 MiB and peak 877 MiB at that point; later restart samples reached approximately 865 MiB. These are short observations, not a guaranteed maximum or proof of a large memory reduction. Limits prevent an unbounded chat job but do not make the cost small.

The independent live comparator preserves case, punctuation, emoji, whitespace normalization and emote names. It compares occurrence counts, not native platform IDs. Reply-context differences need a structured comparison; the absent message needs capture-stage tracing. Do not report lossless live delivery or use the older two-hour capture harness as qualification of this Studio build.

## Visual review

Inspected actual app screenshots and decoded recording frames, including all three overlay styles, populated 1280-pixel UI, private preview/on-air/break states, and original/rose/neon/sand/graphite setups. Overlays remain legible and untrusted markup appears as literal text. Amber on-air status is distinct from private selection.

Useful screenshots:

- `build-chat/ui-walkthrough/20261104-230915/02-selected.png`
- `build-chat/ui-walkthrough/20261104-230915/03-on-screen.png`
- `build-chat/ui-walkthrough/20261104-230915/04-private-break.png`
- `e2e/reports/chat-final-20261104/chat/02-bubble-program.png`
- `e2e/reports/chat-final-20261104/chat/02-big-program.png`
- `e2e/reports/chat-final-20261104/chat/06-recorded-comment.png`

Remaining experience improvements: the source/audio controls below the canvas need scrolling at 1280 px; practice mode leaves an empty channel-strip area; French setup introduces horizontal overflow; initial circles are not real profile photos. The setup's broad platform-support claim should be narrowed until the platforms below are qualified. These are recorded findings, not claims of pixel-perfect parity with the mockup.

## Reproduce

From the worktree root (all test messages are local practice/CDP inputs; these drivers do not post public chat):

```powershell
$env:VIDEO_MIXER_CHAT_EXE = "$PWD\native-qt\dist\desktop-studio-chat-qualification-nov5\desktop-studio.exe"
node native-qt/tools/chat-packaged-workflow.mjs
node native-qt/tools/chat-review-e2e.mjs --exe $env:VIDEO_MIXER_CHAT_EXE
node native-qt/tools/chat-live-e2e.mjs --twitch LIVE_CHANNEL --youtube LIVE_VIDEO_ID --minutes 2
node native-qt/tools/kick-compare.mjs --seconds 300
node native-qt/tools/youtube-compare.mjs --video LIVE_VIDEO_ID --seconds 300
node native-qt/tools/chat-soak.mjs --twitch LIVE_CHANNEL --youtube LIVE_VIDEO_ID --minutes 120
node native-qt/tools/chat-isolation-e2e.mjs --stress
node native-qt/tools/chat-platform-probe.mjs native-qt/build-chat/final-platform-cases.json
powershell -NoProfile -ExecutionPolicy Bypass -File native-qt/tools/chat-ui-walkthrough.ps1
```

Installer checks refuse an existing installation/shortcut and only uninstall their private QA destination. Run from `native-qt`, with a new report directory:

```powershell
python e2e/mixer-installed-e2e.py --report e2e/reports/chat-new-run --package dist/desktop-studio-chat-qualification-nov5 --chat
```

Pending qualification: long live runs through Studio, real channel rollover between broadcasts, supported live/logged-out Kick/Rumble/TikTok/Instagram channels, and sign-in flows. Replies, alerts, read-aloud and viewer tools remain outside this implementation.

## Platform probe on the validated package

Evidence: `native-qt/build-chat/platform-probe/2026-11-05T04-12-29-370Z/results.json`. Cases were observed for 15-25 seconds each; silence is not proof of a capture defect or proof the channel was live.

| Input | Observed result |
| --- | --- |
| Nonexistent YouTube handle | Terminal "We could not find that channel. Check the link." |
| YouTube @tablu_gamer | Resolves to TABLU GAMER; Waiting for your stream -> Waiting for comments -> Reading comments; two real messages |
| Kick stino_tv | Waiting for comments; zero messages; unproven |
| Rumble /c/Bongino/live | Ends at channel-not-found; zero messages. Live-route/offline classification needs follow-up before support is claimed. |
| TikTok @tiktok/live | Waiting for comments; zero messages; unproven |
| Instagram /nasa/live | Rejected as an unsupported chat link; this is not live capture validation |
| Facebook /nasa/live | Explicit sign-in-needed, sign-in unavailable in Studio |
| X /nasa | Explicit sign-in-needed, sign-in unavailable in Studio |

The sequential platform probe reached 1,010 MiB peak job commit during YouTube handle resolution. Although the hard cap/recovery works, that transient is close to the limit and should be investigated before increasing supported channel count.
