# SSN engine in WebView2 — isolated harness

This sibling of `chat-spike` builds a Release Qt6 Widgets/WebView2 host. It does not link Studio, install an extension, run Electron, modify SSN files, or require a cloud relay. The code is a prototype, **not a demonstrated hosting solution yet**: the sandbox has prevented its renderers from starting.

## Build and run from the worktree root

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native-qt/tools/engine-spike/build.ps1
node native-qt/tools/engine-spike/protocol-gates.mjs
node native-qt/tools/engine-spike/run-scenarios.mjs
```

The first command builds/deploys `build/engine-spike.exe` and Qt DLLs/plugins using the same SDK/toolchain as `chat-spike`. Keep the build directory together. The second command runs **offline contract gates, not application testing**. The third launches the packaged executable, owns only that child process, drives actual pages through DevTools, and writes a fresh timestamped `runs/<timestamp>/results.json`, native logs, source audit, DOM observations and screenshots. No npm packages are needed; use Node 22 or later. A failed or blocked workflow sets exit code 1. `not-run` is never a pass.

For a live source, use a currently active Twitch channel (the example does not assert that xqc is live):

```powershell
node native-qt/tools/engine-spike/run-scenarios.mjs --twitch xqc --live-seconds 90
```

The script prints the results path. Send back `results.json`, `source-audit.json`, `native-*/events.jsonl` and any screenshots. Logs may contain real chat/usernames. Runs and profiles are ignored by git.

Optional driver arguments: `--ssn-root PATH`, `--exe PATH`, `--out FRESH_PATH`, `--profile PATH`, `--port PORT`, `--browser-args "FLAGS"`. Port defaults to an available loopback port; the driver refuses to bind an occupied requested port. Each run defaults to its own profile. Scenario 10 restarts the whole native process with that same profile, then observes the loyalty leaderboard again. An explicitly reused profile retains settings and databases, so use a fresh default profile for comparable runs.

Standalone host (replace the output directory on each run):

```powershell
native-qt/tools/engine-spike/build/engine-spike.exe --ssn-root C:/Users/Steve/code/social_stream --out native-qt/tools/engine-spike/runs/manual-001 --remote-debugging-port 19437 --room engine-spike-room --duration-sec 600
```

Standalone also supports `--twitch CHANNEL`, `--profile PATH`, and `--browser-args "FLAGS"`. Use `--room`, **not `--session`**: QApplication consumes `--session` for Qt session management. Native storage refuses a conflicting room on an existing profile. The DevTools endpoint is `http://127.0.0.1:19437/json/list`. The local host page exposes `__engineSpike.rpc('host.open', [{role:'poll', url:'https://socialstream.ninja/poll.html?session=engine-spike-room'}])`, `host.close`, `host.peers`, `host.metrics`, and `host.quit`. These controls are for a trusted local scenario driver.

## What was actually run

On November 4 EST / November 5 UTC, 2026, the Release build/deployment and offline protocol/syntax gates completed. WebView2 was `156.0.4314.58`; SSN's manifest reports `3.50.17`.

| Run | Flags beyond fixed host flags | Observed result |
| --- | --- | --- |
| `runs/boot-gpu` | `--disable-gpu --in-process-gpu` | Three controllers shared browser PID 3700; all renderers failed to launch, exit `0x31` (49), reason 4. No document injection completed. |
| `runs/boot-no-gpu` | `--disable-gpu --disable-software-rasterizer` | Controller creation failed with `0x80070578`. |
| `runs/scenarios-gpu` | `--disable-gpu --in-process-gpu` | Full driver attempted; renderer launch failures before page navigation. Also exposed the Qt `--session` naming collision, subsequently fixed. |
| `runs/scenarios-final` | `--disable-gpu --in-process-gpu --use-angle=swiftshader` | Correct room argument, one browser PID 18736 for three controllers; renderer launch failures before navigation. |
| `runs/release-scenarios` | Default `--disable-gpu --in-process-gpu` | Final packaged build and driver; browser PID 34828 shared by all controllers, three renderer launch failures, zero started documents/requests/screenshots. Final `results.json` marks 1–10/12 blocked and 11 not-run. |

All workflow scenarios were blocked by browser startup; live Twitch was not selected. No screenshots, processed messages, actual `chrome.*` calls, `ninjafy`/`electronApi` calls, outside page requests, or valid six-page memory samples were obtained. Empty host/API lists **do not demonstrate offline operation**. Application testing has not been completed. These failures do not distinguish an SSN incompatibility from a harness defect; browser execution outside this sandbox remains necessary.

Only this harness's own child processes were stopped. No process-name termination or tree kill is used. The running `chat-spike`, Node and WebView2 measurement processes were not touched. No commits were made.

## Engine loading and extension APIs

`manifest.json` names `service_worker.js`. That worker manages a persistent `background.html` tab and forwards requests as `{type:'toBackground',data:...}`; it is not the engine itself. The native host owns that permanent page and performs the message routing directly. It does **not** execute the service worker. The unchanged `background.html` loads `loader.js`; that unchanged loader currently loads 37 scripts, including `background.js`, Event Flow, database, points, giveaway and monetization modules.

The document-created `chrome-shim.js` supplies `chrome.runtime` before SSN executes. This selects the branch opposite `background.js:738`'s Electron fallback. No `ipcRenderer`, `ninjafy`, `electronApi` or `ssappFallback` implementation is installed. Optional desktop bridge reads are logged while returning `undefined`, so feature detection is preserved. No source functions are replaced or wrapped. The optional Twitch source is the original `sources/twitch.js` text, injected at load into its capture WebView; this is not a repeat of the capture performance measurement.

| Boundary | Implementation |
| --- | --- |
| `runtime.sendMessage`, `onMessage` | Native asynchronous routing; original engine listener; sender tab identity; callback or Promise; listeners returning `true` retain the response channel; explicit timeout errors. Engine broadcasts reach other top-level pages. |
| `runtime.getURL`, `getManifest`, `lastError` | Real local asset URL/manifest; callback-scoped lastError; observed reads logged. Synchronous accessors remain synchronous, as in Chrome. |
| `storage.local`, `storage.sync` | Separate persistent JSON areas, atomic writes, get/set/remove/clear, defaults, async callback/Promise, `onChanged` broadcasts. Browser localStorage and IndexedDB persist in the WebView2 profile. `sync` is local persistence, not Google synchronization. |
| `tabs.query/get/getCurrent/create/remove/update/sendMessage` | Actual hosted pages, stable tab IDs, runtime delivery, URL navigation, removal/update events. Active state is bookkeeping; windows stay hidden. Options such as frame-targeted delivery are not fully emulated. |
| `debugger.attach/detach/sendCommand` | **Logged dry stub**, restricted natively to the local `test-source` page. Calls targeting a real capture tab are rejected. No DevTools input/evaluation command is executed by this mapping. |
| `windows.update/getCurrent`, `notifications`, `action/browserAction` | Logged asynchronous UI no-ops. |
| Other Chrome APIs | Not claimed supported. Missing namespace/property probes, exceptions and native unsupported calls are recorded. Identity/OAuth, scripting, tabCapture, extension isolation, permission grants and extension CSP are not implemented. |

This is the source-derived boundary, not a measured list of APIs required at runtime. `results.json.chromeCalls` records actual hits. `source-audit.json` separately inventories static references, hashes and origin checks, including optional identity APIs.

## The local iframe compatibility page

Review **`bridge.html`** and `Host::views/connect/deliver/leave` plus `bridge.join/bridge.send` in `main.cpp`. The page is a deliberately small transport adapter, not a VDO implementation.

1. Native interception serves `bridge.html` for document requests at the original `https://vdo.socialstream.ninja/` URL, including child frames. The iframe's URL and origin remain unchanged. Other requests to that host are blocked.
2. It registers room, password, label, publisher stream ID, viewer subscription, scene/exclude fields, and a fresh UUID with the native host. Password strings separate local rooms; this is not VDO authentication or encryption.
3. The native room graph emits directional `push/view-connection` and `push/view-connection-info` messages with labels and UUIDs. SSN's own handlers then initialize poll, ticker, hype, waitlist and giveaway state. Navigation/close disconnects old peers; rejoin gets new identities and connection notifications.
4. `pcs` sends publisher-to-viewer; `rpcs` sends viewer-to-publisher; an optional UUID narrows delivery. Room/password and subscription edges constrain both directions. Blank dock publishers auto-view room publishers; explicit viewer IDs and scene exclusions are respected. Arbitrary `sendData` payloads, including `overlayNinja:false`, remain intact.
5. Incoming native packets are forwarded **by the actual iframe window** to its parent. Thus upstream `event.source === iframe.contentWindow` guards pass naturally. The parent-to-iframe fixed VDO target origin also remains valid. Cross-frame host packets use a separate envelope; this is not a broadcast substitute for SSN's protocol.

`host.html` acts as a local dock peer: it views the engine stream and publishes selected comments on its own stream. Native `feed.jsonl` records only engine-to-host deliveries from this transport, downstream of SSN's `sendToDestinations`. It does not equate captured input with processed output. Two featured instances can be addressed by their UUIDs; the scenario requires distinct visible comments and a targeted clear. No private room semantics are inferred from an arbitrary URL parameter.

Not covered: audio/video, RTC negotiation/STUN/TURN, actual VDO signaling or SDK mode, cryptographic authentication, RTC statistics, remote peers, complete VDO command/API parity, crash recovery, durable message replay, or overlay-specific storage. Unknown iframe commands are logged, not faked into passing. This harness has no local WebSocket server; it exercises the iframe path. `shared/overlay-control-transport.js`'s local `server`/`server2` URLs are a possible alternative for compatible pages, but this run does not establish their parity. That shared module uses channel 7 for targeted control, 1/3 for requests/acks, deduplicates control IDs, and reconciles state revisions; forwarding the original payload preserves those fields.

The inspected checkout has **14** legacy-required declarations: `actions`, `confetti`, `credits`, `dock`, `events`, `hype`, `map`, `poll`, `reactions`, `spotify-overlay`, `ticker`, `tipjar`, `waitlist` HTML, plus `multi-alerts.js`. The driver's required subset is ticker/hype/waitlist/multi-alerts; it does not claim all fourteen are validated.

## Network, origins, observations and limitations

All controllers share one WebView2 environment/profile/browser process. HWNDs are never shown; controllers are hidden. The three previously measured anti-throttling flags are always set. Default extra flags are `--disable-gpu --in-process-gpu`. No sandbox-disabling flag is used.

`socialstream.ninja` is mapped read-only to the SSN checkout with `SetVirtualHostNameToFolderMapping`. `engine-spike.invalid` serves only the two host-owned pages. Mapping the original SSN host avoids optional `custom.js` lookup in `featured.html:2064`; that check does not reject private hosts. Several pages' `file:` checks affect URL/session setup, which this HTTPS mapping avoids. Dock also gates some media controls on desktop/OBS/file/GitHub environments. Origin checks discovered statically are retained in the audit. Hosting on the original HTTPS name is a compatibility choice, not extension-origin isolation.

Defense layers are DNS rules denying `io.socialstream.ninja`, `vdo.socialstream.ninja`, `vdo.ninja` and its subdomains; all-source request interception/filtering; DevTools URL blocking of cloud WebSockets/VDO; and an instrumented WebSocket constructor. The VDO document interceptor supplies local bytes before any network access. Ordinary outside resources, including fonts and avatars, are allowed. Request and response records retain page IDs/URLs; WebSocket attempts and handshakes are logged. Browser housekeeping traffic and OS-level DNS/packet accounting are outside this page instrumentation. The final source audit is not an observed network inventory.

Memory sampling closes host and capture WebViews temporarily, leaving exactly engine + featured/poll/managed-giveaway/ticker/hype. It samples private **resident working set** of the owned browser PID and descendants, using `QueryWorkingSet`, not private commit. Samples include accessible process count and errors. It then restores host/capture pages. Measurements with errors or a failed browser are invalid; no valid samples exist from this session.

The driver checks real page DOM/text/counts and writes screenshots. It never calls overlay rendering functions to manufacture a pass. Synthetic chat enters through `test-source.html`'s `chrome.runtime.sendMessage({message:...})`; poll/giveaway/Event Flow control calls use the engine's existing APIs. Points use the real capture → database → points path and the unmodified loyalty leaderboard before/after a full native restart. Reply validation observes the host's actual debugger-command log rendered as DOM and verifies the target remains local and commands were not executed.

Source inspection found a likely **poll reload limitation**: votes/results/totalVotes are local variables in `poll.html`, with no storage persistence; reconnect sends engine settings/control revisions, not vote totals. The scenario deliberately fails if votes vanish after reload. This is a source-derived finding, not an observed run result; the bridge must not conceal it by replaying synthetic votes.

Other production concerns remain unmeasured: main-world shims are accessible to capture pages, all local SSN tools share an origin/profile, the native control API trusts locally hosted content, and recovery/backpressure/long-run memory behavior are unproven. Before using this design in Studio, establish trusted control boundaries, isolate untrusted capture code, validate all exercised APIs and lifecycle behavior, and complete the real workflows outside the sandbox. This spike alone does not justify a hosting decision.
