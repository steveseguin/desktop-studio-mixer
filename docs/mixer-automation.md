# Desktop Studio automation and workspace preferences

Version 0.7 adds a local mixer API, a JSON CLI, and a stdio MCP server (32 tools in 0.7.3).
They control the same mixer and isolated workers as the UI. Automation is opt-in
and does not start publishing or recording by itself.

## CLI and windowless operation

The portable package and installer include `tools/mcp/studio-cli.mjs`, `studio.cmd`,
`studio-mcp.mjs`, and the MCP dependencies. Node.js 22+ is required for these tools;
the native app does not require Node. No npm setup is needed in the shipped package.

PowerShell, using an independent show:

```powershell
$studio = 'C:\path\to\desktop-studio-portable'
$cli = "$studio\tools\mcp\studio-cli.mjs"
$session = node $cli launch --headless --show 'C:\shows\automation.mixer.json' | ConvertFrom-Json
$discovery = $session.discovery
node $cli --discovery $discovery status
node $cli --discovery $discovery schema
'{"command":"scene.add","name":"Intro","background":"#183657"}' |
  node $cli --discovery $discovery command --stdin
node $cli --discovery $discovery screenshot program --out 'C:\shows\program.png'
node $cli --discovery $discovery quit
```

In 0.7.3, `status` includes `monitor.devices` with stable endpoint IDs and
availability. `monitor.set` accepts `device`, `gain` (0–1), `enabled`, and
`refresh`. A refresh is asynchronous; wait for `monitor.refreshing` to become
false before selecting a newly connected output. It changes headphone Listen
output, not Program audio. Monitoring starts disabled on each launch. The MCP
tool is `desktop_studio_monitor_set`.

`launch` defaults to windowless; `--gui` opens the window. Processes keep running
after CLI/MCP disconnect. Quit explicitly to finalize recording and stop output.
A show lock prevents concurrent autosaves to the same show; use separate `--show`
paths for multiple instances. An explicit show also keeps output configuration
separate from saved GUI output profiles. Theme/language preferences are user-wide.

Direct native launch:

```powershell
.\desktop-studio.exe --studio-headless --studio-show=C:\shows\automation.mixer.json --local-control-discovery=C:\shows\control.json
```

`--studio-headless` runs indefinitely without a main window or tray icon. It still
uses Windows, Qt and the GPU drivers: windowless operation in a Windows user
session, not a Windows service or a promise of capture on locked/logged-out
desktops. The inherited `--headless` capture mode is separate; don't combine them.

To automate a visible app, launch it with `--local-control-port=0`. Its default
discovery file is `%LOCALAPPDATA%\VideoMixer\control.json`. CLI/MCP launches return
a unique discovery path. Select it using `--discovery`, `DESKTOP_STUDIO_DISCOVERY`,
or the MCP attach tool. `DESKTOP_STUDIO_EXECUTABLE` overrides executable discovery.

## MCP

Configure a **stdio** server in the client's MCP settings:

```json
{
  "mcpServers": {
    "desktop-studio": {
      "command": "node",
      "args": ["C:\\path\\to\\desktop-studio-portable\\tools\\mcp\\studio-mcp.mjs"]
    }
  }
}
```

Use `desktop_studio_launch` or `desktop_studio_attach` first. The packaged server
finds its adjacent executable; installer discovery also works. Named tools include
`desktop_studio_scene_select`, `desktop_studio_audio_set`, and
`desktop_studio_record_start`. `desktop_studio_screenshot` returns a native MCP PNG
image for vision-capable agents. `studio://state` is an MCP state resource.
The inherited source `server.mjs` is retained for legacy capture tools; the new
packaged entry point is `studio-mcp.mjs`.

## HTTP contract

The private discovery file contains port, PID and bearer token. Authenticate with
`Authorization: Bearer <token>` against its `http://127.0.0.1:<port>` address.
`GET /health` is the only unauthenticated endpoint.

| Route | Purpose |
|---|---|
| `GET /studio/schema` | Versioned JSON command/screenshot schemas |
| `GET /studio/state` | Stable IDs, scenes, layers, source/audio and output state |
| `POST /studio/command` | Command object, e.g. `{"command":"record.stop"}` |
| `POST /studio/screenshot` | `{"target":"program","max_width":1280}` → PNG base64 and dimensions |
| `GET /sources/cameras`, `/windows`, `/spout`, `/audio-inputs` | Available device IDs; each path starts `/sources/` |
| `POST /commands` | Inherited lifecycle commands including `{"command":"quit"}` |

Unknown fields, commands and wrong JSON types are rejected before side effects.
Errors have non-success HTTP status and an `error` string. CLI errors exit nonzero.
Use `command --file request.json` or `command --stdin` to avoid shell escaping and
credentials in command history. The live schema describes exact fields and limits.

| Commands | Operations |
|---|---|
| `scene.add/select/rename/remove/background/take` | Scenes and cut/fade transitions |
| `source.add/update/remove/reconnect` | Capture, browser, media, image, title and color sources |
| `layer.set/order` | Normalized geometry, crop, visibility, opacity, lock, fit and order |
| `audio.set` | Gain, mute, routing, monitoring, delay, effects and master gain |
| `media.set` | Pause, loop, restart |
| `canvas.set` | Resolution/frame-rate preset and CPU/GPU compositor |
| `record.start/stop` | Independent Program recording to a new absolute MKV path |
| `output.configure/start/stop` | VDO.Ninja, RTMP, SRT, WHIP, or file output |
| `show.save/open` | Explicit absolute `.mixer.json` paths |
| `workspace.set` | Single/Studio mode and workspace layout |
| `preferences.set` | Theme and next-launch language |

Source locators/URLs and output credentials are omitted from state. Show exports
contain private source/invitation URLs. Source changes affect all scenes using it.
In Studio mode, take sources off Program before deleting them. Scene selection
goes live immediately in single-canvas mode and stages Preview in Studio mode.
Theme/layout changes don't take scenes live.

`state.system` contains `cpu_percent`, `memory_percent`, `memory_used_bytes` and
`memory_total_bytes`, refreshed once per second independently of publishing.
CPU measures the whole machine (0–100%), including other applications. Before
the first sample, this object can be empty. `state.canvas.active_renderer`
reports `pending`, `passthrough`, `cpu` or `gpu`; `renderer_error` explains a GPU
composition fallback. `output.state=error` also covers a rejected encoder start.

Start/stop operations are asynchronous. Poll `output.state`, `output.live`, packet
counters and `recording.status`; acceptance is not proof of receiver playback.
`output.active` includes reconnecting sessions. Wait for `recording.busy=false`
before opening a recording. Recording and CLI screenshot files are never
overwritten. `show.open` loads into the current session; autosave continues at
the session's original show path. `show.save` exports to an explicit path.

Screenshots capture Program, Preview, or the app's widget tree, without OS desktop
capture. Program includes transitions without editing handles. Hidden canvases
refresh on demand. Width is bounded to 160–3840 pixels (default 1280), with one
request per 200 ms. Window screenshots may contain visible production information.

The server is loopback-only and token-protected, exposes no CORS opt-in, and rejects
browser-originated requests even with a token. Browser sources have no automation
bridge or discovery credentials. Authenticated local tools are trusted to change
the show, capture configured devices, write explicit show/recording paths and
publish to configured destinations. Don't forward the port or share discovery
files with websites or other users.

Camera and microphone listing runs outside the rendering thread. Concurrent
queries share an in-flight device scan, without returning a stale cached list.
Clients can disconnect while a scan is pending; no background polling is needed
unless the client wants to watch device availability.

## Accessibility, themes and languages

The default stylesheet, control geometry and existing focus indicators are
preserved. Accessibility adds descriptive names, label relationships, per-track
labels, keyboard-operable numeric layout controls and status announcements. VU
meters expose read-only dBFS values and clipping descriptions through Windows UI
Automation as indicators, without progress events on every frame. Ctrl+Shift+L
announces the selected audio track's current level, or Master if no audio track
is selected. Recording status changes and source-dialog validation errors are
announced without changing focus or the default styling.
Scene navigation supports the keyboard and Ctrl+1–9. Ctrl+Enter takes Preview,
Ctrl+Z/Ctrl+Y undo/redo, and Ctrl+Shift+M mutes the selected source. This is not a
completed assistive-technology conformance audit.

**Appearance** offers Original, Graphite, Rose, Neon and Warm sand. Layout choices
are Balanced, Simple, Podcast (larger audio mixer) and Production (larger source
panel). All retain bordered scrolling and all controls. These change the operator
workspace, not the broadcast design.

**Language**, next to View, offers English, Español and Français. Changes apply
at next launch to avoid rebuilding a live production. Spanish/French cover core
menus and mixer controls; advanced dialogs, technical diagnostics and some
status/help messages fall back to English. User names and API identifiers are
never translated. Catalogs are `native-qt/resources/studio-es.json` and
`studio-fr.json`.
