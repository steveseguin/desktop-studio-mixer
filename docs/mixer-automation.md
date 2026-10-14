# Desktop Studio automation and workspace preferences

Version 0.7 adds a local mixer API, a JSON CLI, and a stdio MCP server (40 tools in 0.9.0).
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

Version 0.8 adds `guest.invite` and `guest.set` for creating private invitations,
admission/backstage and return selection. These commands return a private invite
URL; keep it out of public logs. Normal state continues to omit credentials.
`production.set` explicitly sets private break, all-microphone mute, private
talkback and the master guest-return switch. A private break produces a silent
slate on Program while admitted guests retain their conversation.

`audio.set` adds `programAudio` and `channelMode` (0 stereo, 1 averaged mono,
2 left to both, 3 right to both). `source.add`/`source.update` add `browserWidth`
and `browserHeight`. `media.set` adds `restartOnTake`; source state includes
`positionMs`/`durationMs`. `record.export` accepts an existing absolute MKV
`input` and a new absolute MP4 `path`; poll `recording.exporting` and
`recording.export_status`. Export is isolated, preserves the original and does
not re-encode. The packaged CLI/MCP tools discover these additions from schema.

Version 0.9 adds `media.set.seekMs` (nonnegative integer milliseconds), including
paused picture cueing. `record.start` accepts optional unique `audioSources`
(up to 16 IDs) and `isoSources` (up to four video-source IDs); omitting them uses
the show's saved recording selections, while empty arrays request Program only.
Named audio tracks obey the source's privacy/mute controls and are pre-master.
Separate recordings stay active off-screen; see the README for routing details.
`recording.active`/`busy` aggregate all recordings; `program_active` identifies
Program alone. `audio_sources` and `separate_videos` identify the frozen recording
plan, including each source file's path, status and active/busy flags.

`show.saveAs` saves and activates an absolute `.mixer.json` path. `show.open` adds
`activate:true` for the same active-project behavior as the visible Open command;
without it, the original import-into-workspace behavior is retained. `show.collect`
takes a new absolute folder under an existing parent and copies local image/media
assets asynchronously; poll `show_collection.busy` and `status`. `show.relink`
takes an existing absolute folder and returns the number of missing local assets
relinked by exact filename. `project_path` reports the active autosave target.
Another Studio instance cannot save over an active project. Collection is a
snapshot and does not switch the active file.

SRT accepts `?mode=caller` (default) or `?mode=listener`; rendezvous is rejected.
`browser_resource_budget` reports whether the shared browser job is active, its
CPU allowance in logical cores and its committed-memory limit/peak in MiB.

`local.set` controls `spout` and `virtualCamera` independently and accepts
`unregisterCamera:true` to stop/remove the per-user camera. Enabling and
unregistering in the same request is rejected before changes. Registration is
asynchronous: poll `local_outputs.configuring` before the next camera command.
`local_outputs.spout`, `virtual_camera` and `status` show the requested states
and producer errors. They work in a windowless Windows user session and start
off on each launch. These are video outputs; choose audio separately in consumers.

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
| `audio.set` | Gain, mute, on-air and channel routing, monitoring, delay, effects and master gain |
| `guest.invite/set` | Private invitations, admission/backstage and guest returns |
| `production.set` | Private break, all-microphone mute, talkback and guest-return switch |
| `media.set` | Pause, seek, loop, restart and restart when entering Program |
| `canvas.set` | Resolution/frame-rate preset and CPU/GPU compositor |
| `local.set` | Isolated Spout/virtual-camera producers and per-user camera registration |
| `record.start/stop` | Program MKV with optional named audio tracks and separate source MKVs |
| `record.export` | Remux a completed MKV to a new MP4 |
| `output.configure/start/stop` | VDO.Ninja, RTMP/RTMPS, RTSP TCP/UDP, SRT caller/listener, WHIP, or file output |
| `show.save/saveAs/open` | Snapshot export, active Save As, or load an absolute `.mixer.json` path |
| `show.collect/relink` | Copy assets into a new folder or relink missing local assets |
| `workspace.set` | Single/Studio mode and workspace layout |
| `preferences.set` | Theme and next-launch language |

For RTSP, use protocol `rtsp` (TCP), `rtspt` (TCP alias), or `rtsp-udp`, with the server's publishing URL in `url`. RTSP authentication goes in the URL; a nonempty `credential` is rejected. See [RTSP and VRChat](rtsp-vrchat-output.md).

For WHIP, `output.configure` also accepts `whip_history_ms` (250-3000,
default 1500), `whip_playout_ms` (0-1000, default 500; 0 uses receiver timing),
and `whip_audio_red` (boolean, default true). Configuration describes a complete
destination: omitted recovery fields take these defaults. The same controls
appear in Output settings > Advanced and persist in output profiles.

`state.output` reports `video_nack`, `video_pli`, `video_rtx`, `audio_red`,
`video_playout_ms`, `retransmitted_packets`, and `repair_cache_misses`.
Negotiated values appear after connection feedback is sampled. The requested
values are `whip_history_ms`, `whip_playout_ms`, and `whip_audio_red_requested`.
Counters belong to the current WebRTC connection and reset on reconnect;
retransmitted packets are not proof of successful receiver repair. A zero
`video_playout_ms` means no negotiated hint, not zero network latency.
See [publishing recovery](../README.md#publishing-recovery) for mechanisms,
bandwidth costs, receiver dependencies and FEC limitations.

Source locators/URLs and output credentials are omitted from state. Show exports
contain private source/invitation URLs. Source changes affect all scenes using it.
In Studio mode, take sources off Program before deleting them. Scene selection
goes live immediately in single-canvas mode and stages Preview in Studio mode.
Theme/layout changes don't take scenes live.

`canvas.set` keeps its original preset IDs: 0=1280x720/30, 1=1920x1080/30,
2=1920x1080/60, 3=1280x720/60, 4=1080x1920/30. Version 0.7.4 adds
5=720x1280/30, 6=720x1280/60 and 7=1080x1920/60. Recording and streaming
use the selected canvas. Stop every output, including guest returns, before
changing it. `show.save` exports a snapshot; it does not switch the autosave
workspace. Local assets under the exported show's directory use relative paths.

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
overwritten. Without `activate:true`, `show.open` loads into the current session
and retains its autosave target. `show.save` exports to an explicit path without
changing that target.

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
