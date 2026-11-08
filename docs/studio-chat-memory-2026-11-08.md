# Isolated chat rendering and memory

Work in `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`. This pass reduces connected-chat private memory by 29.9% and 33.5% in two paired observations. The exact new package completed real UI, recording, publication, worker recovery and installer workflows. Evidence root `R` is `native-qt/build-integration/chat-memory-nov8/`. This is a bounded qualification, not a new two-hour soak or a proven leak fix.

## Change and artifacts

The chat worker now requests software rendering in its own WebView2 environment. Its platform pages supply DOM data and its featured cards are static text. This reduces the GPU process's buffer/cache overhead. The main video compositor, encoder and browser sources retain their existing rendering configuration. No machine-wide browser setting or environment variable changes.

| Artifact | SHA-256 |
| --- | --- |
| Previous portable `native-qt/dist/desktop-studio-chat-final4-nov7/desktop-studio.exe` | `b50d2b5966d3dfa587e83d6fe5e7a4487f5bebc5454de7479fb0698018d33b98` |
| New portable `native-qt/dist/desktop-studio-memory-software-nov8/desktop-studio.exe` | `f0ac7045bbee7d0e6508ce05942ff29924e52fdeaf03e1f6a4b1b9944be5de0f` |
| New installer `native-qt/dist/desktop-studio-0.9.4-memory-nov8-setup.exe` | `544970c5cecd0155392442eee525ecf9b0de953101978527da350ab98b99272e` |

Artifacts are timestamp-signed with the existing development certificate; Windows trust remains `UnknownError`. This is not a publicly trusted release signature. The SSN pack and shared runtimes are unchanged.

The setting is `--disable-gpu`, applied only through the chat environment's browser arguments. [Microsoft describes WebView2 flags as diagnostic controls whose availability can change](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/webview-features-flags). This is therefore a measured runtime-specific optimization, not a guaranteed memory ceiling. A runtime update needs requalification. The existing chat worker resource limits and failure isolation remain. Unlike the earlier six-minute diagnostic, this pass also exercises packaged user journeys, sustained recording/publication, sign-in navigation and the installed app.

Actual process inspection found the argument on the recording app's chat browser, and absent from the independent receiver's browser-source browser. Evidence: `rendering-scope.json`.

## Paired resource observations

Two packaged copies connect anonymously to the same Twitch/LIRIK_247 and YouTube/Lofi Girl channels and feature a real comment. Their local control APIs supply resource samples and comment ledgers every two seconds. These paired resource diagnostics are separate from the user-workflow tests below. The reported values use the final two minutes of each five-minute run. The second run swaps the two disposable QA browser profiles between executables to check for a profile/cache confound. No owner profile is used.

| Run | Previous chat private memory | New chat private memory | Previous/new CPU cores | Comments observed |
| --- | ---: | ---: | ---: | --- |
| First software-rendering comparison | 855.8 MiB | 600.2 MiB | 0.046 / 0.064 | Both: 20 Twitch, 38 YouTube |
| Profiles exchanged | 919.0 MiB | 611.4 MiB | 0.097 / 0.052 | Both: 25 Twitch, 32 YouTube |

The first comparison reduces private chat memory by 29.9%. Its CPU cost is about 0.23% versus 0.32% of this 20-thread host. Equal totals alone are not a delivery guarantee. An independently joined public Twitch reference matched all 12 scored occurrences over 251.169 seconds, with median/max observed delays of 1,534/1,917 ms including two-second polling. This was a quiet window. Evidence: `software-pair1/summary.json`, its artifact files, `candidate/twitch-comparison.json` and `software-truth1/`.

The exchanged-profile comparison reduces private chat memory by 33.5%. CPU is about 0.49% versus 0.26% of the host. Its independent Twitch reference matched 25/25 occurrences over 279.680 seconds, median/max observed delays 1,002/2,063 ms. Both pairs stayed ready with their held comment present in every sampled final-two-minute observation. Evidence: `software-pair2/summary.json`, its artifact files, `candidate/twitch-comparison.json` and `software-truth2/`. These short windows establish neither universal delivery nor a long-term memory ceiling.

Private memory includes the chat worker and its complete WebView process tree. Summed working sets also include shared mappings and are not presented as physical RAM savings. The recording workflow runs concurrently with other QA copies; its whole-Studio CPU observations are not an isolated benchmark.

## Packaged application workflows

The exact `f0ac7045...` executable passed 111 targeted checks:

| User workflow | Passed | Evidence relative to `native-qt/build-integration/chat-memory/` |
| --- | ---: | --- |
| Real connected chat, narrow-window navigation, accessible filters/removal, worker release and held-comment preservation | 20 | `final2-ui/channels-20261107-235451/` |
| Synthetic support events, gifts/streaks, private review and three display styles | 77 | `final2-ui/support-20261107-235507-389/` |
| Twitch account window, reloads, narrow controls and return to ready chat | 14 | `final2-ui/account-navigation-20261107-235557/` |

These operate the packaged application's actual UI. Synthetic support events qualify display/state behavior, not live gift purchases or extraction. Saved owner authentication passed separately after restoring the workspace.

The new package completed a fifteen-minute local recording and unlisted VDO.Ninja publication with a moving reference, pulse audio, two additional media scenes, anonymous Twitch/YouTube and a featured real comment. A separate packaged receiver recorded the beginning and end and observed incoming frames/errors between them. Only synthetic picture/audio and public comments were published; no webcam, microphone, chat post or purchase was involved. Sender and receiver UI workflows each passed seven checks.

Kick/Buddha produced no comments during the initial 90-second attempt; its public API confirmed it was offline. That incomplete attempt is retained as `record/kick-add/`. A retry connected to confirmed-live Kick/Zavalahimself (about 27,036 viewers). Actual comments arrived and adding the third platform preserved recording and live output (two checks, `record/kick-add-000215/`). Reviewing and featuring a real Kick comment then passed three checks (`record/kick-feature-001021/`). The actual saved local and received frames both contain that comment.

An independent public Kick socket reference matched **1,976/1,976 distinct occurrences within five seconds** over a 281.741-second scored window: 554 plain comments, 121 mixed text/emotes and 1,301 emote-only comments. Median/max observation delays were 1,919.5/3,785 ms, including three-second panel polling. Evidence: `native-qt/build-chat/kick/2026-11-08T05-02-23-104Z/timed-comparison.json` and its unchanged ledgers. This does not qualify emote artwork or future delivery rates.

The original aggregate comparator exited with failure despite matching 2,023/2,023 occurrences: it labelled 16 captured gift events as unexpected ordinary comments. Kick's `ChatMessageEvent` reference does not cover these gifts. `kick-compare.mjs` now counts `event == "gift"` separately and excludes only those events from ordinary-comment scoring, while retaining all raw rows. Replaying that exact corrected scoring block over the saved ledgers yields zero missing, duplicate or unexpected ordinary comments and 16 observed gift events (`gift-aware-replay.json`). This is post-run analysis, not a new live run; the original failed `summary.json` remains. A separate two-minute public event observer saw ordinary comments/deletions but no gift event, so gift amounts, authors and live delivery completeness are **not independently verified**.

During recording/publication, deliberately terminating the isolated chat worker, suspending it and terminating its browser network child passed 14 UI checks. Automatic worker replacement took 1.51 seconds after termination and 4.31 seconds after suspension. Comments resumed, the held card stayed visible, and recording/publication continued. Hide still worked afterward. Evidence: `record/worker-recovery-20261108-001211/`. This is worker fault recovery, not a full Internet outage or a main-app crash test.

## Saved output and resources

All three recordings finalized and fully decode with 30 fps presentation timestamps, no duplicate presentation times, no presentation gap over 34 ms, and no pulse-audio silence interval of 1.5 seconds. Motion analysis uses the unobscured moving strip at crop `0,540,32,90`, threshold 0.1; near-repeat candidates are not assertions of lost upstream frames.

| Saved file | Duration / frames | Near-repeat transitions / longest interval | Measured A/V offsets |
| --- | --- | --- | --- |
| Sender `record/cadence/recording.mkv` | 901.737 s / 27,052 | 3 / 67 ms | First/last-minute maxima 17/26 ms |
| Initial receiver `receiver/receive/received-vdo.mkv` | 60.227 s / 1,806 | 4 / 100 ms | Median audio lead 84 ms; maximum absolute offset **104 ms** |
| Final receiver `receiver/receive/received-end.mkv` | 287.477 s / 8,624 | 1 / 67 ms | First/last-minute maxima 68/74 ms |

The initial receiver sample slightly exceeds the 100 ms A/V target. Its full-file follow-up pulse analysis exposed this after the shorter-file decode/timeline checks passed; those checks do not assert sync. Received sync is therefore not uniformly qualified within 100 ms. The later sample covers the worker faults. No continuous fifteen-minute receiver recording was made. Evidence: each recording's `media-*`, `motion-*` folders and the initial receiver's `.analysis.json`.

One-second sender observations show no inactive recording, lost live state or unavailable API before the 900-second target. After the first two minutes, whole-Studio CPU averages 5.69% of this 20-thread host (p95 7.03%); private memory averages 1,422 MiB. Other QA applications ran concurrently, so this is workload observation, not an isolated CPU comparison. Recorder counters finish at 17 dropped and three repeated frames; decoded saved-frame analysis is reported separately above.

Before the deliberate faults, the chat browser retained one PID, stayed ready and exceeded no resource limit. The mixed three-channel phase, including the initial offline Kick attempt and then live Kick, averages 902 MiB of chat private memory (maximum 1,000 MiB). Thus the paired two-channel result of roughly 600 MiB must not be generalized to three busy platforms. The observer's connection errors occur only after normal app shutdown. No oversized-message count was observed. Evidence: `summary.json`, `record/cadence/metrics/` and `record-observer/`.

Continuous receiver observation covers 855.467 seconds with 856 samples, no interval without advancing frames, no source errors and no unavailable samples. Mean receiver CPU is 1.72%, including periods with and without recording. This observation complements the two saved samples; it does not prove the unrecorded interval's complete A/V continuity.

## Installer, saved workspace and visual review

The visible installer installed into the explicit owned QA directory `R/installer-final/installed-app/` (four checks), and its executable hash matched the qualified portable exactly. The installed app created a scene with moving picture/pulse audio, attached public Twitch/YouTube channels without signing in, received real comments, privately selected and featured a comment, and saved a recording (eight checks). Missing and tampered chat-pack variants each rejected chat while preserving moving picture, audio and recording (five checks each); original pack bytes were restored after each run. All three installed recordings fully decode with pulse A/V offsets within 37 ms.

Visible uninstall passed five checks. Seven saved shows/recordings outside the installed payload remained byte-identical, including three actual recordings. Registration and the Start menu shortcut were removed. Evidence: `installer-final/first/`, `application-first/`, `application-missing/`, `application-tampered/`, `remove/` and `preserved-files-*.json`. This pass exercises fresh per-user installation on a machine with WebView2 already present; same-version upgrade and WebView2 bootstrap were not repeated.

The owner's seven scenes, eight source identities and four channels match their pre-install live state. Five restoration checks passed and the latest portable remains open with recording and publication idle. The saved Twitch account still showed **Log Out** in its visible account menu; return to ready chat, channel preservation and idle output passed four checks. No credentials or browser storage were read. Evidence under `native-qt/build-integration/ui-exploration/20261106-085441/`: `chat-restored-20261108-001948/` and `saved-twitch-20261108-002002/`. This verifies an existing authenticated session, not a fresh credential/MFA flow.

Screenshots were visually reviewed on this executable: the 1280x720 chat layout and platform/remove controls, synthetic Kick gift bar, narrow Twitch sign-in window, actual saved Kick card locally and at the receiver, installed-app workspace, worker recovery output, and the restored owner workspace. Text/cards remain readable and the reviewed narrow account controls fit. At 720 px height the channel list scrolls; long public comments are clipped in feed previews. The existing title-free workspace command bar remains. Screenshots are retained locally with their workflows rather than committing real commenters' names.

Release compilation, package hash verification, JavaScript/PowerShell syntax, whitespace and the repository's staged secret scan passed as gates. Only source, harness corrections and these reports belong in the commit; binaries, profiles, recordings and raw chat ledgers remain ignored.

## Retained experiments and limitations

- WebView2's supported low-memory target for capture/routing pages produced 898.2 versus 896.3 MiB, an insignificant difference. That source change was removed. Evidence: `pair1/`. [The API preserves scripts/connections but offers only best-effort memory reduction](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2_19?view=webview2-1.0.3537.50).
- Suppressing painting with root opacity while preserving layout/animation frames produced 866.2 versus 859.4 MiB. That experiment was also removed; no injected opacity change ships. Evidence: `paint-pair1/`. Its capture/support checks passed. One account screenshot attempt lost foreground when a comparison app opened; an isolated repeat passed all 14 account checks. This was an automation overlap, not a demonstrated sign-in regression.
- A temporary recording wrapper initially failed its PowerShell parse gate before launching anything. Its corrected wrapper parsed and then started the actual recording workflow. Build, syntax, whitespace and secret checks are gates, not application testing.
- The first real-Kick feature attempt and first fault-recovery setup attempted to select a new API-observed comment while the visible feed was paused. Both failed before their intended action; the recovery attempt injected no fault. The feature wrapper now resumes **Back to live** and verifies exact selection before showing; the tracked recovery harness also resumes the feed before selecting. Successful repeats and failed originals are retained. A post-run scorer replay initially failed its syntax gate because it included a live status `await`; replacing that read with the saved status allowed the actual scoring block to run unchanged.
- This pass is not a new two-hour soak. The earlier two-hour result belongs to its explicitly identified earlier executable.
- Real raid repetition, YouTube broadcast rollover, a full Internet outage, independently verified live gifts/purchases and unqualified platforms remain outside this memory fix. Lower memory use does not establish lossless capture on every platform or a leak fix. Actual BRIO capture and main-app crash recovery were not repeated on this executable.
- Raw ledgers, profiles, screenshots, packages, recordings and experiment helpers remain git-ignored. Main's existing edits are preserved.
