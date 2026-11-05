# Native chat integration: packaged validation

Updated 2026-11-05 UTC. Worktree: `C:\Users\Steve\code\video-mixer-chat`. The integration base is `90caa70` on `feature/studio-chat-spike` (07:56 local); the follow-up fixes and this report are recorded together in a later local commit on that branch. Main's application edits were not changed. The runs below exercised real WebView2, the portable app, Windows accessibility controls, recordings, and an installed release.

## Current package, qualification follow-up

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
