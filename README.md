# Desktop Studio

[Screenshots and demo videos](media-assets/README.md) | [Machine-readable media manifest](media-assets/manifest.json)

A native Windows production studio for scenes, sources, audio mixing and live output. **Desktop Studio** complements Web Studio and Web Mixer. It is based on [Game Capture](https://github.com/steveseguin/game-capture), with its own executable, settings, installer identity, browser profiles, logs, and update feed.

Development version: **0.9.0**. Fork baseline: `a76d44feaf01ad228aa99ed35531454be6fe1b4b` (Game Capture 0.2.62). The original project is independent and unchanged.

Previously named **Video Mixer**. Setup upgrades the existing installation and replaces its Start menu shortcuts. Existing shows, output profiles, camera settings, browser profiles and the layout preference carry over. The repository/build target, `%LOCALAPPDATA%/VideoMixer` data folder and `VDO.Ninja/Video Mixer` settings identity remain stable for compatibility. Older validation documents use the name of the version they measured.

## Automation and workspace preferences

Use the bundled CLI or stdio MCP server to control scenes, sources, audio, recording and publishing, including windowless operation. Capture Program, Preview or the app window directly as PNG. See [automation setup and API reference](docs/mixer-automation.md).

The **Language** menu adds English, Spanish and French core controls. **Appearance** offers five palettes and four workspace layouts. The default styling stays unchanged; accessibility adds descriptive labels, per-track names and keyboard access.

The bottom-right status bar shows **total system CPU and RAM usage**, refreshed every second even when idle or only recording. CPU is normalized across all logical processors: 100% means the whole machine. The same values are available in the local API. **Ctrl+Shift+L** reads the selected audio track's level (or Master when no audio track is selected) through a screen reader; meters do not send continuous progress beeps.

## Use the studio

1. Open `desktop-studio.exe`. Select a scene and choose **Add source**.
2. Add windows/games, webcams, Spout2 feeds, microphones, desktop audio, video/audio files, images, titles, colors, or browser URLs. Check a source to show it on the canvas. The default **single-canvas** mode sends scene changes and edits straight to output.
3. Drag sources or their corner handles. Use X/Y/W/H, cropping, Fit/Fill/Stretch, opacity, layer order, and layout presets. Lock protects a layer from layout edits; Snap aligns edges (hold Alt to bypass). Undo/Redo restores scene and layout edits, including removed sources. Shortcuts: Ctrl+Z / Ctrl+Y, Ctrl+1–9 for scenes, Ctrl+Enter for Take, and Ctrl+Shift+M to mute the selected source.
4. Enable **Studio mode** when you want separate Preview and Program panels. **Take / Cut** or **Fade** copies the staged layout to Program. Fade blends picture and scene-following audio over 400 ms. Turning Studio mode off takes the selected Preview scene to output and resumes direct editing. The layout preference survives restarts.
5. Adjust source gain/mute in **Audio mixer**. Each track and the master have a visual dBFS level meter with colored segments, peak hold and a clip indicator. **Scene** makes audio follow Program; microphones and newly invited guests default to always on. Admitting a guest enables their audio immediately unless muted, including in Studio mode before Take. A master fader and peak limiter control the final mix. Optional track effects include RNNoise speech noise suppression, a -45 dBFS gate, and 4:1 compression above -18 dBFS. Effects start off; leave speech suppression off for music.
6. Choose **Output settings**, leave **Mixer program** selected, and choose a destination. **Record to file (MKV)** writes H.264/AAC to a new file. Stop output to finalize it. Existing recordings are never overwritten.
7. Studio's **Record...** starts an independent H.264/AAC MKV recording of Program. Streaming and recording can run together and stop independently. Recording uses its own hardware encoder when available. A crashed encoder can restart with the same settings; persistent failure or low disk space stops recording with an error. **GO LIVE / STOP** controls the selected streaming destination.

Select a source and use **Edit source** to rename it, change its URL/file/device, or change browser capture mode. **Reconnect** restarts a source; windows prompt you to select a current window. These source changes apply to every scene, including Program. The source status shows errors and received frame rate.

**Audio delay** in Edit source adjusts Program audio by 0-500 ms for synchronization. Browser feeds start at 60 ms; cameras, networks and page rendering can need different values. Headphone monitoring stays immediate.

Scenes, sources and audio have separate bordered scroll panels with contrasting backgrounds. On smaller windows, their controls scroll while the canvas stays visible; the outer workspace also scrolls when necessary. Source name and audio-delay edits preserve the running capture session. Changing the device, URL, content or browser transparency restarts the affected source; **Reconnect** still explicitly restarts it. Unavailable saved devices stay selected until you choose a replacement.

Select a webcam source and choose **Camera settings...** for a live preview, capture resolution/frame rate, and its driver-supported controls. These can include zoom, exposure, focus, brightness, contrast, white balance, saturation, sharpness, gain, backlight compensation, pan, and tilt. Sliders and numeric inputs use the device's range and step size; **Auto** appears enabled where the driver supports switching modes. **Reset** uses that control's driver default, with automatic mode where supported.

Camera changes apply immediately, including to Program. **Apply capture mode** briefly restarts that source and shows the actual negotiated mode. The canvas format is selected separately in the top bar. Explicitly changed controls and the requested mode are saved with the source and reapplied on reconnect/show reload. Selecting a different physical camera clears those overrides. Opening the settings panel does not change the device. Exposure and low light can reduce delivered frame rate, and other applications may share the camera's hardware settings.

**Headphone monitor** has its own device and volume controls and starts off each launch. The output selection is remembered by its Windows endpoint identity. Use **Refresh** after connecting headphones or an audio interface. A missing saved device stays marked unavailable; it does not silently fall back to speakers. Monitoring and device discovery run in isolated workers, so a failed monitor does not stop the Program audio bus. A channel's **Listen** sends it to headphones, including while off air; **Mute** silences both monitoring and Program. Microphones start with Listen off. Desktop audio excludes Desktop Studio and its browser/monitor processes, preventing monitor feedback. Use headphones for guests and microphones.

**Program window** provides clean output (F11 for fullscreen). Shows initially autosave under `%LOCALAPPDATA%/VideoMixer/studio.mixer.json` (or the workspace selected with `--studio-show`). **Open show...** opens that file as the active project. The **Save a copy...** arrow offers **Save As active show...**, which changes where later edits autosave; its main button still exports a snapshot. Normal launches reopen the last active project. Active shows are locked against another Studio instance, including after Open and Save As. A rolling `.backup.mixer.json` preserves a previous valid version, updated at most once a minute when content changes.

The same menu offers **Collect show and assets...**: choose a new folder, and Studio copies local image/media files alongside a show with relative paths. The folder appears only after every copy succeeds. **Relink missing assets from folder...** matches exact filenames in the selected folder; it does not search recursively. Ordinary snapshot exports retain absolute references for assets outside their folder. Collections do not copy browser content, live devices or online services. Show files and backups include private source URLs and invitation links.

## Guests, phones, and chat

**Private break** replaces Program with a Be right back slate and silences its audio while recording/streaming continue. Admitted guests keep their mix-minus conversation, including unmuted local microphones. Click **Resume show** to return to Program. Per-track **On air** controls let admitted guests rehearse privately without changing their conversation or headphone Listen. **Mute mics** silences local microphone tracks in Program, returns and monitoring without changing their individual mute settings.

**Hotkeys...** offers opt-in, configurable global shortcuts for private break, microphone mute, Take, held private talkback and scenes 1–9. Scene shortcuts stage Preview in Studio mode. Conflicts disable the global set and identify the unavailable combination. Defaults are Ctrl+Alt+B/M/Enter/T and Ctrl+Alt+1–9. The original in-window shortcuts remain available. Hotkeys require an interactive Windows session.

Each audio track offers **Stereo**, **Mono (L + R)**, **Left to both** and **Right to both**. This maps the track's stereo input before its effects, recording, guest return and monitoring; it is not a selector for arbitrary channels on a multichannel interface.

**Recordings > Recording tracks...** selects up to 16 named stereo audio tracks inside the Program MKV and up to four separate source video MKVs. Program is always recorded. These selections are saved with the show. Separate audio is after source gain, channel mapping, effects and delay, before the master fader; it follows Mute, On air, Backstage, Mute mics, Private talkback and Private break. It remains available off-screen regardless of Scene routing. Separate video retains the source picture, without the Program layout; Backstage and Private break make it black. Removing a source leaves its existing recording silent/black. Source-video size is fixed at recording start; subsequent changes are scaled. Each extra video needs an encoder and disk bandwidth. One failed recording writer does not stop the others; Stop recording stops all of them.

**Recordings** also opens the recording folder or exports a completed MKV with all its audio tracks as MP4 without re-encoding. The original stays intact; existing MP4 files are never overwritten. The recording status estimates remaining disk time from free space and configured bitrate, conservatively accounting for separate videos. This is an estimate, not a storage reservation. **Playback** adds a seek slider with keyboard control, including silent picture cueing while paused, elapsed/remaining time, and optional restart when a clip enters Program. A clip shared by consecutive scenes keeps playing. Seeking is not a frame-accurate editing interface.

After an app crash, keep the interrupted `.mkv` files. Media already written can usually be recovered using **Recordings > Export recording as MP4...**, including the selected audio tracks; export each separate source MKV individually. An unfinished MKV can play without a known duration, so the Playback dialog explains how to restore seeking through MP4 export. Recording uses short MKV chunks to reduce the unwritten tail lost in a crash. Very early failures can precede the first video frame; disk damage or power loss can lose additional data. This is recovery of written media, not a guarantee that every frame survives.

Diagnostics are under `%LOCALAPPDATA%/VideoMixer/logs` and `crashes`. Each app instance keeps a current log and up to three previous logs, limited to 5 MiB each. Restarting preserves the previous session. A handled native crash also saves a copy of its last log beside its JSON report and minidump; those crash artifacts remain available after later log rotation. Worker failures record the component, process ID and failure reason without source URLs or command contents. Crash dumps can contain private process data; inspect them before sharing.

Browser source settings now include viewport width/height (160–3840 by 120–3840), allowing the web page itself to use a portrait layout. Existing shows keep 1280x720; new browser sources added through the interface use the current canvas size. The **Safe area** checkbox displays a generic 10% editing guide that never enters Program output.

- **Invite guest** creates a private VDO.Ninja room invitation and a receiving source. Guests choose and start their camera/microphone, then join **backstage**. Select the source and choose **Admit / Backstage**. Newly invited guests stay audible on Program and in the guest conversation when off-screen; Mute or Backstage silences them. Enable **Scene** to make their Program audio follow their picture instead. Existing shows retain their saved routing. The picture follows the selected scene in single-canvas mode and requires Take in Studio mode. Studio Preview can show backstage video. The room and invitations are retained in the saved show.
- **Wireless camera** creates a similar invitation with the rear-camera preference and no incoming return by default. The phone still sends its camera/microphone to Studio.
- **Social chat** accepts a Social Stream Ninja session ID or overlay URL. A session ID adds its featured-message overlay. Select it and choose **Open / Invite** to open its chat dock and feature messages. Connect social platforms in Social Stream Ninja first.
- Browser sources also accept existing VDO.Ninja viewing URLs. **Open / Invite** provides browser interaction.

**Guest Program return** sends guests the Program picture and mix-minus audio: their own voice is removed, other admitted guests remain audible even off-screen, and unadmitted backstage sources are excluded. Muting a guest also silences them in other guests' returns. Each invitation offers **No return**, **Audio return only**, or **Program video and mix-minus audio**. Changing that choice updates the link; reopen the new link on a connected device. Guests default to video/audio return; wireless cameras default to none to save resources. The choice and master return switch are saved with new shows. Existing shows retain their previous invite mode. Each connected video return currently uses a separate encoder; more guests increase resource use. Recreate invitations made before 0.5.0 to use Program return.

**Private talkback** sends the Studio microphone inputs only to guests while the button is active, removing those inputs from Program and recordings. Add your microphone as a source first and leave its channel unmuted. Turn talkback off to restore normal routing. This addresses all guests with Program return enabled; use headphones.

## Engine and current limits

The original controls remain in **Capture & output**: standalone window/camera/Spout capture, hardware encoders, codecs, transparency workflows, audio encoding settings, and VDO.Ninja, WHIP, SRT caller/listener, RTMP/RTMPS, RTSP TCP/UDP, UDP MPEG-TS and RIST outputs. The **SRT mode** selector switches between connecting to a receiver and accepting a receiver at the configured local address. Passphrase encryption works in both modes. Listener output retries after stalled connections; it is not a permanent multi-client server. Rendezvous remains unsupported by this integration. Studio supplies frames and audio to that same output engine. Use the Studio faders when Mixer program is selected.

- The canvas supports native **720p30/60, 1080p30/60, portrait 720x1280/30/60 and 1080x1920/30/60**. Mixer streaming and independent recording follow that format. Stop outputs before changing it. Existing window/Spout sources reconnect to negotiate the new size and rate; webcams retain their separately selected capture mode. **Stacked** places interview pictures above one another; lock backgrounds/overlays before applying layout presets to preserve their placement. **CPU composition** is the default, based on lower measured cost for the common 720p mix. Optional **GPU composition** uses Direct2D/D3D11 with CPU fallback. Source/output frames still pass through CPU memory, so this is not a zero-copy GPU pipeline.
- New browser sources and Social chat default to **Transparent overlay background**, using snapshots at up to **15 fps**. Overlay mode overrides the top-level HTML/body background to transparent on each navigation; message cards, embedded pages and other element styling remain intact. Social Stream Ninja can still supply its own custom CSS. Turn overlay mode off for opaque browser video using **Windows Graphics Capture targeting 30 fps**, avoiding PNG encoding/decoding. Existing saved capture modes are preserved. Actual cadence depends on hardware and page activity. The compositor still reads back to CPU memory.
- One streaming destination, plus independent recording, Spout output and virtual-camera output. **Spout output** publishes `Desktop Studio Program`. **Virtual camera** registers `Desktop Studio Virtual Camera` for the current Windows account without administrator access. It supplies video; select an audio device in the receiving app separately. This is a 64-bit DirectShow camera, verified in FFmpeg and Edge; applications requiring other camera interfaces may not support it. **Unregister camera** removes its registration. Local outputs start off each launch and are controllable through `local.set` in the CLI/API/MCP. Their producers run in separate workers, with bounded pending frames, crash/hang recovery and a restart limit. If an output pauses after repeated failures, turn it off and on to retry. Camera registration also runs outside the main app and UI thread.
- **Video / audio file** uses Windows Media Foundation codecs, including H.264/AAC MP4 and WAV. **Playback** offers pause, restart and looping. Local-file audio uses a 10 ms startup buffer and brief loop read-ahead to tolerate decoder jitter. Unsupported codecs report an error. Audio-only files contribute audio without occupying a video layout position.
- Cameras and native sources start asynchronously. Failed native captures retry with backoff; browser navigation failures and browser process crashes recover automatically. Windows restore by a unique executable/title match, including when their window reopens. Ambiguous or renamed windows require selecting the intended window through Reconnect. Missing devices/images/runtime report errors. Physical Logitech C920 unplug/reconnect recovered camera and microphone while recording and publishing continued; other camera drivers still need hardware coverage.
- Browser audio is routed from Web Audio/media streams to the native mixer. Browser speaker sessions are silenced; only enabled native monitoring plays locally. Ordinary Web Audio and WebRTC receivers are supported. Protected media, cross-origin media without CORS permission, and unusual page audio graphs may not expose audio; check the channel meter before going live. Chat overlays are visual sources.
- Application/desktop loopback audio needs Windows build 20348 or newer. Camera/mic capture and the browser audio bridge do not use that process-loopback API. Avoid capturing the same game's audio both individually and via Desktop audio.

## RTSP and VRChat output

Advanced output settings offer **RTSP / VRChat (TCP)** and **RTSP (UDP)**, alongside RTMP/RTMPS, SRT and WHIP. RTSP sends H.264/AAC to a publishing server; TCP also accepts `rtspt://`. Use the server's viewing link in VRChat. See the [setup and automation guide](docs/rtsp-vrchat-output.md).

## Publishing recovery

WHIP uses WebRTC media negotiation; it has no universal error-correction percentage. Desktop Studio offers recovery automatically and uses the mechanisms accepted by the destination. **Output settings > Advanced** exposes the following controls; stop publishing before changing them. Output profiles remember them.

| Mechanism | Behavior and control |
|---|---|
| **NACK** | The receiver asks for missing video packets. The sender retains **1500 ms** of history by default, adjustable **250-3000 ms**, with hard limits of 4096 packets / 8 MiB. Increasing history gives late requests more time; it does not increase the receiver's own buffer. |
| **RTX** | Offered automatically. When accepted, repairs have a separate RTP sequence space and SSRC. Otherwise negotiated NACK uses original-packet retransmission. |
| **PLI** | The receiver requests a fresh keyframe when it cannot recover a picture. Requests are limited to one every 250 ms to prevent a keyframe storm. |
| **Playback smoothing** | Requests a **500 ms** minimum video playback delay, allowing time for repairs before display. Adjustable **0-1000 ms**; **0 / Receiver default** omits the hint. Only sent when the receiver accepts the playout-delay extension. This is a best-effort receiver hint, not a guarantee of end-to-end latency. |
| **Audio RED** | **On by default as an offer.** When selected by the receiver, each Opus packet includes one previous packet when both fit within 1200 bytes. This can nearly double audio payload bandwidth; it does not double video bandwidth. Disable the offer in Advanced if necessary. Unsupported destinations receive plain Opus. |
| **Video FEC / Opus in-band FEC** | Not implemented in this sender. RED provides redundant audio, but there is no video parity/FlexFEC/ULPFEC or adjustable FEC percentage. An SDP answer advertising Opus `useinbandfec=1` does not mean this encoder generates it. |

Video packets are paced to reduce bursts. Repairs bypass that queue, with a bounded repair budget and duplicate-request suppression. A stalled pacing queue reconnects only that destination. Recovery status shows negotiated NACK/RTX/PLI, RED selection, playback hint, retransmission count and cache misses. The retransmission counter counts repairs handed to the transport; it does not prove the destination recovered them. The receiving service still determines its NACK timing, reorder window, decoding and final playback buffer.

These controls do not implement automatic bitrate adaptation. Leave upload headroom for repairs; increasing redundancy on a saturated connection can make loss worse. A larger sender history cannot fix a receiver that discards repairs before they arrive. The [recovery validation notes](docs/mixer-0.9.0-followup-validation.md#publishing-recovery-implementation-and-receiver-cross-checks) distinguish browser/independent-receiver results from the remaining MediaMTX loss and RTSP startup-timestamp issues.

SRT's latency control sets its retransmission window. After a prolonged connection setup or reconnect, Studio refreshes a stale media session once to avoid carrying several seconds of old video into the recovered stream. This retry is bounded; independent recording continues through it. No transport can recover an arbitrarily long outage without interruption.

Protocol references: [WHIP](https://www.rfc-editor.org/rfc/rfc9725.html), [NACK/PLI](https://www.rfc-editor.org/rfc/rfc4585.html), [RTX](https://www.rfc-editor.org/rfc/rfc4588.html), [audio RED](https://www.rfc-editor.org/rfc/rfc2198.html), and [playout-delay guidance](https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/playout-delay/).

## Browser runtime

Uses **Microsoft Edge WebView2 Evergreen**. No Chromium engine is bundled. Microsoft's runtime updates separately; users install it if missing. The SDK/loader is a build dependency, not the browser engine. [Microsoft distribution guidance](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution).

Profiles live under `%LOCALAPPDATA%/VideoMixer/browser/`. Receiving/overlay pages cannot access local cameras, microphones, screen capture or clipboard. Downloads, popups, external application handlers and non-HTTP(S) top-level navigations are blocked. The audio bridge validates the sending page, message size, type and rate; it exposes no native command or file API. Guests grant camera permissions in their own browser. Press Escape to park an interactive browser source.

Studio cameras, window/Spout inputs, microphones/desktop capture, media decoders, browser hosts, headphone playback, local Spout/virtual-camera producers, video encoders and each publishing/recording destination run in separate helper processes. Bounded frame/audio buffers prevent a slow consumer from building an unlimited backlog. A failed source reconnects; a failed publisher reconnects independently. A failed recording destination preserves its existing file and requires a new recording. Closing the app closes its helper process trees.

Still-image codecs also run in disposable helper processes, with at most two loads in progress, a 1 GiB memory limit per helper and a 15-second decode deadline. Loading a large or corrupt image leaves the interface and other sources running. PNG transparency is preserved; JPEG's runtime dependency is included in the portable package. Unsupported formats show a source error.

Each browser source and its Chromium children have a Windows CPU budget of at most half the machine, capped at four logical CPUs, plus a committed-memory limit of one quarter of physical RAM (minimum 1 GiB, maximum 4 GiB). All browser sources additionally share a CPU cap of half the machine or eight logical CPUs, whichever is lower, and a committed-memory cap of one third of RAM (minimum 2 GiB, maximum 8 GiB). CPU shares rebalance as sources enter/leave and retain shared headroom, so several busy pages cannot spend a healthy source's whole scheduling quota. Source health shows **CPU limit active** under sustained load. A page responsiveness watchdog supplements native worker heartbeats. After three automatic browser reconnects within a minute, a further failure pauses that source; **Reconnect** explicitly retries it. Complex pages and large guest panels may run more slowly when they reach a limit.

Browser audio routes into the source's mixer track by default. WebView2 retains its normal GPU rendering and compatible hardware video decoding, with software fallback for unsupported codecs/drivers. Transparent capture still uses CPU PNG snapshots at up to 15 fps; accelerated decoding does not make that capture path fully GPU-based.

On WebView2 152-154, a compatibility workaround avoids a WebRTC decoder reinitialization bug by not negotiating the RTP color-space extension. Enable **Preserve original video color metadata** in the source dialog if a source needs that metadata; this can cause software decoding on affected runtimes. Other runtime versions retain normal negotiation, and HTML video/HEVC support is unchanged. For VDO.Ninja, `&codec=h264` can request a codec with hardware decoding on compatible PCs; this was verified with a live feed. The source's codec selection is never changed automatically.

These boundaries contain user-mode failures in those components; they are not a guarantee against browser vulnerabilities, an OS/GPU-driver failure, or bugs in the main compositor. Compositing, image-buffer handling and some inherited non-Studio paths still include native code in the main process. Keep Windows, graphics drivers and Evergreen WebView2 updated.

## Build

Requires Windows x64, Visual Studio 2022 C++ tools/Windows SDK, CMake 3.24+, Ninja, and Qt 6 Widgets/Network/Concurrent. The script uses `VCPKG_ROOT`, or `C:/vcpkg` when unset.

```powershell
# Prepare FFmpeg for recording, RTMP/SRT and FFmpeg-dependent codecs:
powershell -ExecutionPolicy Bypass -File native-qt/tools/fetch-ffmpeg-lgpl.ps1

# Restore the WebView2 SDK, configure, build, and package Qt:
powershell -ExecutionPolicy Bypass -File native-qt/build-mixer.ps1 -Package

# Also build the per-user setup executable (requires NSIS 3):
powershell -ExecutionPolicy Bypass -File native-qt/build-mixer.ps1 -Installer
```

Portable executable: `native-qt/dist/desktop-studio-portable/desktop-studio.exe`.

Archives and setup: `native-qt/dist/desktop-studio-0.9.0-windows-x64.zip` and `desktop-studio-0.9.0-setup.exe`. Setup installs under `%LOCALAPPDATA%/Programs/Desktop Studio`, includes Qt/VC runtime DLLs, creates Start menu shortcuts, and preserves shows/recordings on uninstall. It does not bundle WebView2 or modify firewall rules. These development packages are unsigned. `installer-mixer.nsi` is the current installer; `installer.nsi` remains the inherited machine-wide installer.

The pinned SDK is fetched from NuGet; other native dependencies use CMake FetchContent. Downloaded dependencies, build outputs, local shows, and QA artifacts are excluded from Git.

## Validation and provenance

[Additional 0.9.0 browser-audio and recovery testing](docs/mixer-0.9.0-followup-validation.md) documents a fixed stale-audio bug when pages replace tracks or switch between live streams and files, plus actual app-crash, disk-full, guest and browser security workflows.

[0.9.0 project, recording and resource validation](docs/mixer-0.9.0-validation.md) covers active projects and asset collection, media seeking, named audio tracks and separate videos, SRT listener, aggregate browser limits, and the remaining engineering work.

[0.8.0 production controls and validation](docs/mixer-0.8.0-validation.md) covers private breaks, configurable global hotkeys, audio channel routing, guest automation, browser viewports, MP4 export, guest panels, accessibility, failure recovery, and the remaining equipment/account checks.

[0.7.4 workflow and timing review](docs/mixer-0.7.4-validation.md) maps the main show formats and P0/P1 flows, measures loop synchronization, and records portrait support and remaining production gaps.

[0.7.3 audio and isolation validation](docs/mixer-0.7.3-validation.md) covers browser audio, monitoring devices and failures, off-screen guest conversation, wireless return modes and concurrent browser overload.

[0.7.1/0.7.2 validation](docs/mixer-0.7.1-validation.md) covers interface workflows, production endurance, CPU readout, real NVDA workflows, hardware/compositor matrix, browser GPU/CPU failures, disk-space failures and concurrent MCP clients.

[0.7.0 automation and accessibility validation](docs/mixer-0.7.0-validation.md) covers packaged CLI/MCP control, windowless screenshots and production, private RTMP/SRT/WHIP receivers, browser recovery, keyboard access, themes and language menus.

[0.6.2 browser resilience validation](docs/mixer-0.6.2-validation.md) covers a live VDO.Ninja viewer, default audio, actual GPU decoding, resource exhaustion, hangs and repeated crashes during recording. [0.6.1 overlay background validation](docs/mixer-0.6.1-validation.md) covers default transparency, page security policies, reloads, source modes, composed recording and the real Social Stream Ninja workflow. [0.6.0 isolation, browser security and performance validation](docs/mixer-0.6.0-validation.md) records real publishing, recording, hardware, failure-injection and sustained workflows.

[0.5.0 production validation](docs/mixer-0.5.0-validation.md) covers the native canvas, independent recording, media playback, editing, audio effects, guest returns, local outputs, recovery and performance measurements.

[0.4.3 source-edit and layout validation](docs/mixer-0.4.3-validation.md) covers capture continuity, unavailable devices, show-file round trips and smaller windows. [0.4.1 performance validation](docs/mixer-0.4.1-validation.md) covers opaque-frame shortcuts, shared browser audio-session scanning, exact-pixel gates, and current application measurements. [Single-canvas and CPU validation](docs/mixer-0.4.0-validation.md) covers direct editing, layout persistence, SIMD color conversion and transparent-image caching. [Audio meters and scroll-panel validation](docs/mixer-0.3.2-validation.md) covers 0.3.2's visible track/master meters and distinct scroll surfaces. [Performance review and measurements](docs/mixer-performance-review.md) covers 0.3.1's scene caching, capture buffer ownership, audio-loop savings and canvas selection fixes, plus remaining camera-startup and browser-capture limits. [Camera-control validation](docs/mixer-camera-validation.md) covers 0.3.0. [0.2.0 installed-app validation](docs/mixer-0.2-validation.md) and [initial 0.1.0 validation](docs/mixer-validation.md) retain earlier workflow results and remaining gaps. Inherited gates remain under `native-qt/tests`, `qa`, and `e2e`. A successful build or gate is not end-to-end testing.

[Original Game Capture documentation](docs/game-capture-base.md) is historical reference; its release numbers and screenshots describe the base application.

The original GPL license and attribution are retained; see [LICENSE](LICENSE). Packages include FFmpeg/WebView2 notices and the RNNoise, OBS virtual-camera and libdshowcapture licenses. Vendored source, local changes and the pinned upstream commit are recorded in `native-qt/vendor/PROVENANCE.txt`.

## Streaming defaults, direct receivers and microphones

New streaming sessions default to **4,000 kbps video**. The encoder ceiling follows the selected target instead of allowing an extra 4,000 kbps. Saved bitrate choices and explicit CLI/API overrides are retained. This is a video encoding target, not a total upload cap: audio, transport headers, retransmissions and extra VDO.Ninja viewers add traffic. Independent Studio recording keeps its separate quality settings. Higher streaming bitrates remain in advanced settings.

**UDP / MPEG-TS (LAN)** and **RIST** are under advanced Output settings. Both carry H.264 video and AAC audio using the bundled FFmpeg worker. They also accept `--output=udp` / `--output=rist` and `--output-url=...` in headless capture. Start a receiver first, for example:

```powershell
# Sender URL: udp://RECEIVER-IP:5000
ffplay -i "udp://0.0.0.0:5000?fifo_size=8192&overrun_nonfatal=1"
# Sender URL: rist://RECEIVER-IP:8194; use matching 200 ms buffers
ffplay -buffer_size 200 -i "rist://@0.0.0.0:8194"
```

OBS Media Source and VLC can receive the MPEG-TS UDP stream. UDP provides no loss recovery or delivery acknowledgement; a Live status means the sender is transmitting. RIST uses Main profile and a configurable recovery buffer (20–8,000 ms, 200 ms UI default). Match the receiving buffer to the sender. This RIST integration does not configure encryption; use a trusted link/VPN or choose SRT with a passphrase. See the [FFmpeg protocol documentation](https://ffmpeg.org/ffmpeg-protocols.html#rist) for receiver options. The receiving application can add startup buffering of its own.

Standalone capture starts with **No microphone**, including when switching to Camera. Choosing a microphone enables it; the adjacent **Mute / Unmute** button stays usable while live and preserves the selected gain. Muting gates microphone samples before primary/additional routing, so it covers microphone-only audio and a microphone mixed with game audio. It does not stop the device; choose **No microphone** before the next capture to avoid opening it. Microphone selection and mute state are saved. Studio mixer microphones remain explicit sources with their existing per-track Mute buttons; the standalone microphone controls are disabled for Mixer program.

**AMD AMF** explicitly uses FFmpeg's AMD encoder instead of requiring an AMD Media Foundation encoder registration. Its defaults select low-latency CBR, balanced quality and a single queued frame. H.264 disables B-frame reordering, forces true IDRs at the requested GOP interval (at most one second in low-latency mode), and preserves AMD's live headers without injecting stale startup headers. HEVC uses AMF's own access-unit delimiters, avoiding a metadata filter that can reject incomplete AMD startup extradata. These policies are shared with Game Capture's AMD fixes. Settings follow [AMD's AMF guide](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/AMF-Encoder-Settings-and-Tuning-in-FFmpeg) and the [FFmpeg AMF implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/amfenc_h264.c). Explicit AMD selection fails visibly when unavailable. Desktop Studio's actual AMD GPU/driver encoding and OBS playback still require validation on AMD hardware.

Process audio capture prefers the default output device's sample rate when Windows does not report a process-loopback format. Missing or invalid device information uses 48 kHz. If Windows rejects another rate, capture retries once at 48 kHz with a fresh audio client. Mixed/output audio remains 48 kHz; no Windows device setting is changed.

Software H.264 on Windows uses Media Foundation, rather than x264. Its default now favors low encoding complexity and uses at most four encoder workers (at most half the logical processors, minimum one). Native H.264 explicitly requests CBR with a one-second encoder buffer; hardware-specific FFmpeg encoders keep their low-latency settings. Encoder rate control is not a network pacer or a total upload limit.

Fresh window-capture settings use **1280x720 at 60 fps** with hardware-first Auto encoding. Camera capture retains its separate **1080p30** default; Desktop Studio's new-show canvas remains **720p30**. Higher resolutions/frame rates and custom bitrates remain under Advanced; saved selections are preserved, including previously saved 12 Mbps values. **Standard (4000 kbps)** is the new streaming default, with **Economy (2500 kbps)** retained for smaller upload budgets. Software encoding cannot guarantee spare CPU during a demanding game; 720p30 is the lighter manual option. Independent local recording and guest-return streams add their own encoding work.

Packaged measurements, CPU contention results and remaining hardware limits: [encoding defaults validation](docs/encoding-defaults-validation-2026-10-14.md).
