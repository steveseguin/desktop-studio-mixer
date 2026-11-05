# Native chat integration: packaged validation

Updated 2026-11-05 UTC. Worktree: `C:\Users\Steve\code\video-mixer-chat`. Changes remain uncommitted. This report supersedes the earlier restricted-environment review: the runs below exercised real WebView2, the portable app, Windows accessibility controls, recordings, and an installed release.

## Result

The native chat workflow is usable with real Twitch, YouTube and Kick. It is **not yet qualified as a reliable all-platform release**: Rumble, TikTok, Facebook, Instagram and X remain unproven, memory is substantial, and real stream rollover needs qualification.

The newest results are under [Latest build](#latest-build). They were run on `native-qt/dist/desktop-studio-portable/desktop-studio.exe`, which is newer than the `desktop-studio-chat-validated` package and the 0.9.4 installer described below. The older sections remain as a record of those builds. No installer has been built or tested with the latest changes.

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
$env:VIDEO_MIXER_CHAT_EXE = "$PWD\native-qt\dist\desktop-studio-chat-validated\desktop-studio.exe"
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
python e2e/mixer-installed-e2e.py --report e2e/reports/chat-new-run --package dist/desktop-studio-chat-validated --archive dist/desktop-studio-chat-validated.zip --chat
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
