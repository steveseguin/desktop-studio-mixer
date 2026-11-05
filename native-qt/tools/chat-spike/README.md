# Social Stream / WebView2 measurement spike

Standalone Qt6 Widgets + WebView2 application; no Studio target, Electron, extension installation, account, or sign-in. Reads Social Stream's manifest and source files without changing them. A short run is a diagnostic gate, **not completed testing of the multi-hour workflow**.

## Build (PowerShell, from this worktree root)

Requires Visual Studio C++ tools, CMake, Ninja, the existing WebView2 SDK, Evergreen WebView2 Runtime, and Qt6 in `C:/vcpkg/installed/x64-windows`. Node 22+ is needed only for the reference/analyzer scripts.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native-qt/tools/chat-spike/build.ps1
node native-qt/tools/chat-spike/sanity.mjs C:/Users/Steve/code/social_stream
```

The build script locates Visual Studio using `vswhere`, imports `vcvars64.bat`, then runs:

```powershell
cmake -S native-qt/tools/chat-spike -B native-qt/tools/chat-spike/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build native-qt/tools/chat-spike/build --parallel 4
C:/vcpkg/installed/x64-windows/tools/Qt6/bin/windeployqt.exe --release --no-translations --compiler-runtime native-qt/tools/chat-spike/build/chat-spike.exe
```

The executable and deployed Qt DLLs/plugins are in `build/`. Keep them together. `sanity.mjs` checks shim contracts, matching/timing, immutable payloads, manifest JavaScript parsing and analyzer fixtures; these are offline gates, not application testing.

## Run

Choose a channel that is live **when you run**. Run each command in a separate PowerShell terminal, starting IRC first, then hidden and visible close together. These commands are a 2.5-minute diagnostic; change durations to `240` for four hours (allow IRC an extra minute).

```powershell
node native-qt/tools/chat-spike/twitch-truth.mjs --channel xqc --out native-qt/tools/chat-spike/runs/hidden --duration-min 3
```

```powershell
native-qt/tools/chat-spike/build/chat-spike.exe --ssn-root C:/Users/Steve/code/social_stream --source twitch:xqc --source youtube:jfKfPfyJRdk --mode hidden --out native-qt/tools/chat-spike/runs/hidden --duration-min 2.5
```

```powershell
native-qt/tools/chat-spike/build/chat-spike.exe --ssn-root C:/Users/Steve/code/social_stream --source twitch:xqc --source youtube:jfKfPfyJRdk --mode visible --out native-qt/tools/chat-spike/runs/visible --duration-min 2.5
```

After all three finish:

```powershell
node native-qt/tools/chat-spike/analyze.mjs native-qt/tools/chat-spike/runs/hidden --control native-qt/tools/chat-spike/runs/visible
node native-qt/tools/chat-spike/analyze.mjs native-qt/tools/chat-spike/runs/visible --truth native-qt/tools/chat-spike/runs/hidden
```

Use a **fresh output directory** for every run. Existing profiles/logs are rejected, so a previous signed-in session cannot leak into a new run. IRC can create its files in the output directory before the executable starts. Without `--duration-min`, Ctrl+C stops the application and finalizes metadata; forced termination may leave an incomplete run. Window close does not end measurement. Logs contain real usernames; keep output under ignored `runs/`, outside git, and do not publish raw logs.

Generic source example (only captures if the supplied URL matches the SSN manifest):

```powershell
native-qt/tools/chat-spike/build/chat-spike.exe --ssn-root C:/Users/Steve/code/social_stream --source "url:https://www.twitch.tv/popout/xqc/chat" --mode offscreen --out native-qt/tools/chat-spike/runs/offscreen --duration-min 2.5
```

`url:` sources get a stable hash ID. Automatic IRC/YouTube comparisons use the typed `twitch:`/`youtube:` sources, so use those for comparison runs.

| Mode | Native window | WebView2 controller |
| --- | --- | --- |
| `hidden` | HWND created, **never shown** | `put_IsVisible(FALSE)` |
| `offscreen` | Shown beyond the left/top edges of the union of all monitors | Visible |
| `minimized` | `showMinimized()`, then native `SW_SHOWMINNOACTIVE` after controller creation | Visible |
| `visible` | Normal shown window | Visible |

Keep the visible control unobscured on a sufficiently large desktop. Occlusion, minimizing, desktop locking, RDP changes, power saving and monitor changes can change compositor behavior. Positions and HWND states are logged at controller creation. Offscreen position is computed at startup; do not rearrange monitors during the run.

## Injection and the compatibility boundary

`chrome-shim.js` is the readable compatibility implementation. The host registers one document-created bundle containing the shim first, probe/matcher second, then manifest entries. It waits for `AddScriptToExecuteOnDocumentCreated` completion before navigation. Each entry checks its current frame URL before executing its **unmodified source text**, in manifest file order. A manifest/script SHA-256 inventory, SSN version, settings and bundle size are saved in `run.json`; WebView2 version is in `events.log`.

- Match schemes, wildcard hosts/subdomains and paths, `exclude_matches`, include/exclude globs, `all_frames`, and same-origin `match_about_blank` are handled. A wildcard subdomain includes its bare host. `url:` needs no platform-specific C++ changes.
- `document_start` runs immediately; `document_end` at DOMContentLoaded; missing/`document_idle` runs on the next task after window load. The last is a documented approximation of Chrome's discretionary idle scheduling.
- WebView2 document-created injection applies to child frames. `all_frames: false` gates execution to the top frame; `true` matches every eligible frame. Child-frame packets relay through `window.top.postMessage` into the top WebView2 host channel. This includes cross-origin frames but is not an extension's authenticated messaging boundary.
- All scripts execute in the **page's main world**, including manifest entries that normally use extension isolation. CSS entries, extension-origin privileges, cross-origin `match_about_blank`, `match_origin_as_fallback` and exact extension world semantics are not emulated. Page CSP or extension-dependent background requests may prevent other platforms from working. This is a measurement harness, not a complete extension replacement.
- The bundle retains all manifest JS entry text (about 4.35 MB for the inspected checkout), with per-document URL gates and lazy function bodies. Its load/parse cost is part of the measurement. Only matching bodies execute. The host row makes harness overhead visible.

Survey of `sources/**/*.js` found `chrome.runtime.id`, `sendMessage`, `lastError`, `onMessage.addListener`, `getURL`, `storage.local.get/set`, `storage.onChanged.addListener`, and `tabCapture.capture`. The first group is implemented; `storage.sync` and callback/Promise get/set are also supported. Storage is **in-memory per document**, with no cross-frame synchronization or persistence. `getURL` maps to read-only local SSN files at `https://ssn-resources.invalid/` using WebView2 virtual-host mapping. `tabCapture` is deliberately unsupported; it is not needed by the selected Twitch/YouTube DOM capture scripts. Unknown background operations only receive an enabled-state acknowledgement and are logged by request keys.

The shim extends `window.chrome` and preserves `chrome.webview`. `runtime.sendMessage(request, callback)` and `runtime.sendMessage(runtime.id, request, callback)` both work; Promise callers also work. From the inspected `background.js` contract:

- `getSettings` / `cmd: getSettings` returns `{state: true, settings, streamID: '', password: ''}`. The capture scripts learn they are enabled from `response.state`; no extension switch UI is needed.
- Captured messages receive `{state: true, id}` when no ID is supplied. YouTube's `youtubeMessageRequestId` also gets its `youtubeMessageResponse` listener acknowledgement. IDs do not mutate the recorded request.
- Emote requests (`getBTTV`, `getSEVENTV`, `getFFZ`) acknowledge state but do not fetch/broadcast emotes. The associated settings default off. `runtime.lastError` is undefined for these successful local replies.
- No host-driven focus, input, chat sending, sign-in or permission grants occur. Popups/downloads are denied.

**Unmodified source does not mean an untouched browser environment.** The inspected SSN Twitch/YouTube scripts themselves spoof visibility and contain WebRTC/timer keep-alive behavior. The harness adds no frame pump, throttling-disabling flags, proactive reload or keep-alive option. The result measures those original scripts plus the shim and probe, not a bare page. `ssn_app/hidden-window-keepalive.js` was read as background evidence only; its Electron frame pump is not installed.

The user requested no simulated input. Some SSN scripts ordinarily click consent/points/menu buttons or dispatch synthetic scroll/focus events. The shim therefore blocks programmatic `HTMLElement.click/focus` and dispatch of input/focus/scroll events, recording a counter. It supplies `textonlymode: true`, `disableYoutubeAutoScroll: true`, `disableAutoLiveYoutube: true`; this avoids automatic menu/scroll interaction. These are settings/environment restrictions, **not source-file edits**. Consent barriers can consequently prevent chat. YouTube keeps the default chat filter (often Top chat); the visible control uses identical settings. These restrictions and upstream self-keepalives must be disclosed with results.

Renderer or frame-renderer exit triggers a logged `Reload()` of that source. A browser-process exit is logged as unrecovered with exit status 2; rebuilding environments is intentionally not an unlogged recovery mechanism. An unresponsive renderer is reported, not periodically refreshed. No forced crash was successfully exercised in this sandbox.

## Measurements

- `messages-<source>.jsonl`: host receive UTC timestamp/milliseconds, source/frame URL, and `payload` containing the exact serialized runtime request received. Startup/settings/deletion/event requests are preserved too, but only chat objects contribute to message counts. Batches are preserved as batches. No usernames/text are printed by the analyzer.
- `metrics.csv`: every 10 seconds, plus a final partial interval. Per source: chat count, last-chat age, rAF/s, 100-ms timer callbacks/s (nominal 10/s), native visibility, page-reported visibility, probe age, blocked-input count, whole-browser-tree CPU %, private working set MiB, accessible process count, process-read errors. Never-received ages and absent/stale probe rates are blank, not zero.
- The probe captures native rAF and the `Document.prototype.visibilityState` getter **before** SSN can spoof them. Cumulative counters are reported on a 1-second timer; the host calculates interval rates. A stale/no-progress probe yields missing samples, so a frozen page cannot look healthy. These probe timers add overhead; they do not synthesize compositor frames. Instrumentation can itself affect background scheduling and must be held constant across modes.
- Each source gets a separate WebView2 environment/profile and browser PID. Toolhelp snapshots enumerate that PID and all descendants; CPU uses deltas of kernel+user time, normalized so **100% means all logical processors**. Multiply by logical processor count for the one-core convention. Private working set counts resident nonshared pages with `QueryWorkingSet`, **not** private committed bytes. No accessible working processes means no valid cost estimate. Raw zeros accompanied by process errors are invalid samples; analysis excludes them.
- `TOTAL` sums the sources' browser trees only. `HOST` measures chat-spike itself. The Node reference/analyzer and OS-wide GPU use are outside these totals. Snapshots can miss processes born and terminated between samples; they can also lose descendants whose intermediate parent has exited. CPU is therefore a sampled estimate, not ETW accounting. Polling resident pages adds host overhead.
- `events.log` is JSONL with navigation, bundle registration, executed files, JS errors/rejections, first-seen runtime request keys, process kind/reason/exit codes and reload outcomes. `run.json` records provenance and start/end timestamps.

`twitch-truth.mjs` uses Node's global WebSocket with an anonymous `justinfan<random>` nick at `wss://irc-ws.chat.twitch.tv:443`. It sends only CAP/NICK/JOIN/PONG protocol commands, never PRIVMSG. It logs raw incoming IRC plus parsed user/text, records joined coverage intervals, and reconnects after disconnects. It does not assert that a channel is live; verify that separately. Moderation/deletions or differing platform filters can cause legitimate IRC/DOM differences.

`analyze.mjs <runDir> [--control visibleRunDir] [--truth truthDir] [--window-sec 15] [--warmup-sec 30]` reports JSON. It matches user+text with NFKC/whitespace normalization, case-insensitive usernames, optional leading `@`, and one-to-one nearest matching within ±15 seconds. Text remains case-sensitive. Both login and display-name aliases are considered; repeated identical messages are assigned only once. HTML fallback strips tags/uses image alt text and decodes common/numeric entities; text-only capture avoids that ambiguity.

Only concurrent joined reference coverage is scored, excluding the first 30 seconds of capture and a full matching window at either edge. It reports excluded counts, capture rate (null for no reference messages), misses, unmatched/false captures, and signed receive-time latency p50/p95/p99/max. Negative latency is possible because these are separate transports. YouTube's visible control is **not API truth**; messages missed by both runs cannot be detected. No reference, no overlap, failed injection, empty control and incomplete runs must not be interpreted as successful capture.

Metrics summaries include mean/p95/max rAF/timer/CPU/memory, missing/error sample counts, visibility states, and memory slope in MiB/hour plus first/last-quartile change. Longest silent gap includes initial and trailing silence across the entire run. No messages means the whole run is silent. Memory growth includes startup/cache effects; a few minutes cannot establish a leak or hours of stability.

## Session observations

On 2026-11-04 EST / 2026-11-05 UTC, the Release build/deployment and offline sanity gates passed. Qt deployment warned about its missing translation catalogs; this English CLI ran. WebView2 reported version `156.0.4314.58`; the inspected SSN manifest was `3.50.17`.

An initial 2.5-minute hidden/visible pair used `twitch:lofigirl` and the requested YouTube video. Twitch's live response showed 210 viewers at the check; anonymous IRC joined but received zero messages. Both WebView2 runs failed before script registration completed.

The active-channel diagnostic used the following exact commands from the worktree root (three concurrent processes, then analysis). `xqc` was confirmed live through Twitch's own read-only response, with 20,644 viewers at that check. Choose a currently live channel for future runs.

```powershell
node native-qt/tools/chat-spike/twitch-truth.mjs --channel xqc --out native-qt/tools/chat-spike/runs/diagnostic-hidden --duration-min 3
native-qt/tools/chat-spike/build/chat-spike.exe --ssn-root C:/Users/Steve/code/social_stream --source twitch:xqc --source youtube:jfKfPfyJRdk --mode hidden --out native-qt/tools/chat-spike/runs/diagnostic-hidden --duration-min 2.5
native-qt/tools/chat-spike/build/chat-spike.exe --ssn-root C:/Users/Steve/code/social_stream --source twitch:xqc --source youtube:jfKfPfyJRdk --mode visible --out native-qt/tools/chat-spike/runs/diagnostic-visible --duration-min 2.5
node native-qt/tools/chat-spike/analyze.mjs native-qt/tools/chat-spike/runs/diagnostic-hidden --control native-qt/tools/chat-spike/runs/diagnostic-visible
node native-qt/tools/chat-spike/analyze.mjs native-qt/tools/chat-spike/runs/diagnostic-visible --truth native-qt/tools/chat-spike/runs/diagnostic-hidden
```

| Observation | Hidden | Visible |
| --- | ---: | ---: |
| Duration | 150.132 s | 150.144 s |
| Captured Twitch / YouTube chat | 0 / 0 | 0 / 0 |
| Scored IRC reference messages after boundary/warmup exclusions | 735 | 733 |
| Misses / unmatched captures | 735 / 0 | 733 / 0 |
| Process-failure events (two browser trees) | 18 | 18 |
| Failed renderer reload attempts | 2 | 2 |
| Valid browser CPU/memory samples per source | 0 | 0 |

IRC received **1,404 messages in three minutes**. Capture rate in the scored Twitch windows is 0% because startup failed, **not** because hidden capture was observed starving. There are no latency samples. YouTube's empty failed control gives a null comparison rate. Each hidden source had 15 missing probe samples and 15 incomplete process samples. The longest hidden silent gap is the entire 150.132 s run. Only the mostly idle **host** had usable samples: mean CPU 0.00884% of the machine, mean private working set 13.568 MiB, p95/max 13.641 MiB. These are not working capture costs.

The failures occurred after environment/controller creation and before the injection-completed callback. GPU-process exits (SDK kind 6) reported `0xC0000022` / `STATUS_ACCESS_DENIED`, renderer exits reported code 49 / launch failure, browser exits reported `0x80000003`, and attempted reloads returned `0x8007139f`. This affects visible controls too. It is consistent with a sandbox startup restriction, not evidence about the multi-hour question. No sandbox-disabling flags were added.

Fifteen-second mode gates with `--source url:https://example.com` confirmed native states: hidden HWND/controller both invisible; offscreen HWND/controller visible with no monitor intersection (`x=-1516,y=-1759`); minimized HWND iconic/controller visible; visible HWND/controller visible. The initial minimized gate exposed controller creation restoring the window; applying native minimization after controller creation fixed that state. All those gates still had the same browser startup failure. A final 15-second IRC gate after shutdown/fragment-handling fixes received 107 messages and finalized its metadata.

**Application testing has not been completed.** The long-run capture, successful renderer-crash recovery, actual per-frame delivery, WebView2 shim execution, and working browser CPU/memory still require execution in an ordinary Windows session. Offline gates and these short diagnostic attempts do not answer whether chat stays alive for hours.
