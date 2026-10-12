# Desktop Studio

A native Windows production studio for scenes, sources, audio mixing and live output. **Desktop Studio** complements Web Studio and Web Mixer. It is based on [Game Capture](https://github.com/steveseguin/game-capture), with its own executable, settings, installer identity, browser profiles, logs, and update feed.

Development version: **0.6.2**. Fork baseline: `a76d44feaf01ad228aa99ed35531454be6fe1b4b` (Game Capture 0.2.62). The original project is independent and unchanged.

Previously named **Video Mixer**. Setup upgrades the existing installation and replaces its Start menu shortcuts. Existing shows, output profiles, camera settings, browser profiles and the layout preference carry over. The repository/build target, `%LOCALAPPDATA%/VideoMixer` data folder and `VDO.Ninja/Video Mixer` settings identity remain stable for compatibility. Older validation documents use the name of the version they measured.

## Use the studio

1. Open `desktop-studio.exe`. Select a scene and choose **Add source**.
2. Add windows/games, webcams, Spout2 feeds, microphones, desktop audio, video/audio files, images, titles, colors, or browser URLs. Check a source to show it on the canvas. The default **single-canvas** mode sends scene changes and edits straight to output.
3. Drag sources or their corner handles. Use X/Y/W/H, cropping, Fit/Fill/Stretch, opacity, layer order, and layout presets. Lock protects a layer from layout edits; Snap aligns edges (hold Alt to bypass). Undo/Redo restores scene and layout edits, including removed sources. Shortcuts: Ctrl+Z / Ctrl+Y, Ctrl+1–9 for scenes, Ctrl+Enter for Take, and Ctrl+Shift+M to mute the selected source.
4. Enable **Studio mode** when you want separate Preview and Program panels. **Take / Cut** or **Fade** copies the staged layout to Program. Turning Studio mode off takes the selected Preview scene to output and resumes direct editing. The layout preference survives restarts.
5. Adjust source gain/mute in **Audio mixer**. Each track and the master have a visual dBFS level meter with colored segments, peak hold and a clip indicator. **Scene** makes audio follow Program; microphones default to always on. A master fader and peak limiter control the final mix. Optional track effects include RNNoise speech noise suppression, a -45 dBFS gate, and 4:1 compression above -18 dBFS. Effects start off; leave speech suppression off for music.
6. Choose **Output settings**, leave **Mixer program** selected, and choose a destination. **Record to file (MKV)** writes H.264/AAC to a new file. Stop output to finalize it. Existing recordings are never overwritten.
7. Studio's **Record...** starts an independent H.264/AAC MKV recording of Program. Streaming and recording can run together and stop independently. Recording uses its own hardware encoder when available. A crashed encoder can restart with the same settings; persistent failure or low disk space stops recording with an error. **GO LIVE / STOP** controls the selected streaming destination.

Select a source and use **Edit source** to rename it, change its URL/file/device, or change browser capture mode. **Reconnect** restarts a source; windows prompt you to select a current window. These source changes apply to every scene, including Program. The source status shows errors and received frame rate.

**Audio delay** in Edit source adjusts Program audio by 0-500 ms for synchronization. Browser feeds start at 60 ms; cameras, networks and page rendering can need different values. Headphone monitoring stays immediate.

Scenes, sources and audio have separate bordered scroll panels with contrasting backgrounds. On smaller windows, their controls scroll while the canvas stays visible; the outer workspace also scrolls when necessary. Source name and audio-delay edits preserve the running capture session. Changing the device, URL, content or browser transparency restarts the affected source; **Reconnect** still explicitly restarts it. Unavailable saved devices stay selected until you choose a replacement.

Select a webcam source and choose **Camera settings...** for a live preview, capture resolution/frame rate, and its driver-supported controls. These can include zoom, exposure, focus, brightness, contrast, white balance, saturation, sharpness, gain, backlight compensation, pan, and tilt. Sliders and numeric inputs use the device's range and step size; **Auto** appears enabled where the driver supports switching modes. **Reset** uses that control's driver default, with automatic mode where supported.

Camera changes apply immediately, including to Program. **Apply capture mode** briefly restarts that source and shows the actual negotiated mode. The canvas format is selected separately in the top bar. Explicitly changed controls and the requested mode are saved with the source and reapplied on reconnect/show reload. Selecting a different physical camera clears those overrides. Opening the settings panel does not change the device. Exposure and low light can reduce delivered frame rate, and other applications may share the camera's hardware settings.

**Headphone monitor** has its own device and volume controls and starts off each launch. A channel's **Listen** sends it to headphones, including while off air; **Mute** silences both monitoring and Program. Microphones start with Listen off. Desktop audio excludes Desktop Studio and its browser processes, preventing its monitor output from feeding back into the mix. Use headphones for guests and microphones.

**Program window** provides clean output (F11 for fullscreen). Shows autosave under `%LOCALAPPDATA%/VideoMixer/studio.mixer.json`; Save show as/Open show transfer layouts and source settings. Show files include private source URLs and invitation links.

## Guests, phones, and chat

- **Invite guest** creates a private VDO.Ninja room invitation and a receiving source. Guests choose and start their camera/microphone in their browser, then join **backstage**, off air. Select the source and choose **Admit / Backstage**. In single-canvas mode admission goes live; in Studio mode it prepares Preview and requires Take. Studio Preview can show backstage video. The room and invitations are retained in the saved show.
- **Wireless camera** creates a similar invitation with the rear-camera preference for a phone.
- **Social chat** accepts a Social Stream Ninja session ID or overlay URL. A session ID adds its featured-message overlay. Select it and choose **Open / Invite** to open its chat dock and feature messages. Connect social platforms in Social Stream Ninja first.
- Browser sources also accept existing VDO.Ninja viewing URLs. **Open / Invite** provides browser interaction.

**Guest Program return** sends each invited guest the complete Program picture and a mix with their own audio removed. Creating an invitation enables it. After reopening a saved show, enable it when ready to send return video/audio. Each connected guest currently uses a separate encoder; more guests increase resource use. Recreate invitations made before 0.5.0 to use Program return.

**Private talkback** sends the Studio microphone inputs only to guests while the button is active, removing those inputs from Program and recordings. Add your microphone as a source first and leave its channel unmuted. Turn talkback off to restore normal routing. This addresses all guests with Program return enabled; use headphones.

## Engine and current limits

The original controls remain in **Capture & output**: standalone window/camera/Spout capture, hardware encoders, codecs, transparency workflows, audio encoding settings, and VDO.Ninja, WHIP, SRT, and RTMP/RTMPS outputs. Studio supplies frames and audio to that same output engine. Use the Studio faders when Mixer program is selected.

- The canvas supports native **720p30/60, 1080p30/60 and portrait 1080x1920/30**. Mixer streaming and independent recording follow that format. Stop outputs before changing it. **CPU composition** is the default, based on lower measured cost for the common 720p mix. Optional **GPU composition** uses Direct2D/D3D11 with CPU fallback. Source/output frames still pass through CPU memory, so this is not a zero-copy GPU pipeline.
- New browser sources and Social chat default to **Transparent overlay background**, using snapshots at up to **15 fps**. Overlay mode overrides the top-level HTML/body background to transparent on each navigation; message cards, embedded pages and other element styling remain intact. Social Stream Ninja can still supply its own custom CSS. Turn overlay mode off for opaque browser video using **Windows Graphics Capture targeting 30 fps**, avoiding PNG encoding/decoding. Existing saved capture modes are preserved. Actual cadence depends on hardware and page activity. The compositor still reads back to CPU memory.
- One streaming destination, plus independent recording, Spout output and virtual-camera output. **Spout output** publishes `Desktop Studio Program`. **Virtual camera** registers `Desktop Studio Virtual Camera` for the current Windows account without administrator access. It supplies video; select an audio device in the receiving app separately. This is a 64-bit DirectShow camera, verified in FFmpeg and Edge; applications requiring other camera interfaces may not support it. **Unregister camera** removes its registration. Local outputs start off each launch.
- **Video / audio file** uses Windows Media Foundation codecs, including H.264/AAC MP4 and WAV. **Playback** offers pause, restart and looping. Unsupported codecs report an error. Audio-only files contribute audio without occupying a video layout position.
- Cameras and native sources start asynchronously. Failed native captures retry with backoff; browser navigation failures and browser process crashes recover automatically. Windows restore by a unique executable/title match, including when their window reopens. Ambiguous or renamed windows require selecting the intended window through Reconnect. Missing devices/images/runtime report errors. Physical USB unplug recovery still needs a dedicated hardware run.
- Browser audio is routed from Web Audio/media streams to the native mixer. Browser speaker sessions are silenced; only enabled native monitoring plays locally. Ordinary Web Audio and WebRTC receivers are supported. Protected media, cross-origin media without CORS permission, and unusual page audio graphs may not expose audio; check the channel meter before going live. Chat overlays are visual sources.
- Application/desktop loopback audio needs Windows build 20348 or newer. Camera/mic capture and the browser audio bridge do not use that process-loopback API. Avoid capturing the same game's audio both individually and via Desktop audio.

## Browser runtime

Uses **Microsoft Edge WebView2 Evergreen**. No Chromium engine is bundled. Microsoft's runtime updates separately; users install it if missing. The SDK/loader is a build dependency, not the browser engine. [Microsoft distribution guidance](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution).

Profiles live under `%LOCALAPPDATA%/VideoMixer/browser/`. Receiving/overlay pages cannot access local cameras, microphones, screen capture or clipboard. Downloads, popups, external application handlers and non-HTTP(S) top-level navigations are blocked. The audio bridge validates the sending page, message size, type and rate; it exposes no native command or file API. Guests grant camera permissions in their own browser. Press Escape to park an interactive browser source.

Studio cameras, window/Spout inputs, microphones/desktop capture, media decoders, browser hosts, video encoders and each publishing/recording destination run in separate helper processes. Bounded frame/audio buffers prevent a slow consumer from building an unlimited backlog. A failed source reconnects; a failed publisher reconnects independently. A failed recording destination preserves its existing file and requires a new recording. Closing the app closes its helper process trees.

Each browser source and its Chromium children have a Windows CPU budget of at most half the machine, capped at four logical CPUs, plus a committed-memory limit of one quarter of physical RAM (minimum 1 GiB, maximum 4 GiB). Source health shows **CPU limit active** under sustained load. A page responsiveness watchdog supplements native worker heartbeats. After three automatic browser reconnects within a minute, a further failure pauses that source; **Reconnect** explicitly retries it. Limits apply per source, not to the combined total of all browser sources. Complex pages may run more slowly when they reach a limit.

Browser audio routes into the source's mixer track by default. WebView2 retains its normal GPU rendering and compatible hardware video decoding, with software fallback for unsupported codecs/drivers. Transparent capture still uses CPU PNG snapshots at up to 15 fps; accelerated decoding does not make that capture path fully GPU-based.

On WebView2 152-154, a compatibility workaround avoids a WebRTC decoder reinitialization bug by not negotiating the RTP color-space extension. Enable **Preserve original video color metadata** in the source dialog if a source needs that metadata; this can cause software decoding on affected runtimes. Other runtime versions retain normal negotiation, and HTML video/HEVC support is unchanged. For VDO.Ninja, `&codec=h264` can request a codec with hardware decoding on compatible PCs; this was verified with a live feed. The source's codec selection is never changed automatically.

These boundaries contain user-mode failures in those components; they are not a guarantee against browser vulnerabilities, an OS/GPU-driver failure, or bugs in the main compositor. Headphone playback, local Program outputs and inherited non-Studio paths still include native code in the main process. Keep Windows, graphics drivers and Evergreen WebView2 updated.

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

Archives and setup: `native-qt/dist/desktop-studio-0.6.2-windows-x64.zip` and `desktop-studio-0.6.2-setup.exe`. Setup installs under `%LOCALAPPDATA%/Programs/Desktop Studio`, includes Qt/VC runtime DLLs, creates Start menu shortcuts, and preserves shows/recordings on uninstall. It does not bundle WebView2 or modify firewall rules. These development packages are unsigned. `installer-mixer.nsi` is the current installer; `installer.nsi` remains the inherited machine-wide installer.

The pinned SDK is fetched from NuGet; other native dependencies use CMake FetchContent. Downloaded dependencies, build outputs, local shows, and QA artifacts are excluded from Git.

## Validation and provenance

[0.6.2 browser resilience validation](docs/mixer-0.6.2-validation.md) covers a live VDO.Ninja viewer, default audio, actual GPU decoding, resource exhaustion, hangs and repeated crashes during recording. [0.6.1 overlay background validation](docs/mixer-0.6.1-validation.md) covers default transparency, page security policies, reloads, source modes, composed recording and the real Social Stream Ninja workflow. [0.6.0 isolation, browser security and performance validation](docs/mixer-0.6.0-validation.md) records real publishing, recording, hardware, failure-injection and sustained workflows.

[0.5.0 production validation](docs/mixer-0.5.0-validation.md) covers the native canvas, independent recording, media playback, editing, audio effects, guest returns, local outputs, recovery and performance measurements.

[0.4.3 source-edit and layout validation](docs/mixer-0.4.3-validation.md) covers capture continuity, unavailable devices, show-file round trips and smaller windows. [0.4.1 performance validation](docs/mixer-0.4.1-validation.md) covers opaque-frame shortcuts, shared browser audio-session scanning, exact-pixel gates, and current application measurements. [Single-canvas and CPU validation](docs/mixer-0.4.0-validation.md) covers direct editing, layout persistence, SIMD color conversion and transparent-image caching. [Audio meters and scroll-panel validation](docs/mixer-0.3.2-validation.md) covers 0.3.2's visible track/master meters and distinct scroll surfaces. [Performance review and measurements](docs/mixer-performance-review.md) covers 0.3.1's scene caching, capture buffer ownership, audio-loop savings and canvas selection fixes, plus remaining camera-startup and browser-capture limits. [Camera-control validation](docs/mixer-camera-validation.md) covers 0.3.0. [0.2.0 installed-app validation](docs/mixer-0.2-validation.md) and [initial 0.1.0 validation](docs/mixer-validation.md) retain earlier workflow results and remaining gaps. Inherited gates remain under `native-qt/tests`, `qa`, and `e2e`. A successful build or gate is not end-to-end testing.

[Original Game Capture documentation](docs/game-capture-base.md) is historical reference; its release numbers and screenshots describe the base application.

The original GPL license and attribution are retained; see [LICENSE](LICENSE). Packages include FFmpeg/WebView2 notices and the RNNoise, OBS virtual-camera and libdshowcapture licenses. Vendored source, local changes and the pinned upstream commit are recorded in `native-qt/vendor/PROVENANCE.txt`.
