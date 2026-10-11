# Video Mixer

A native Windows video mixer based on [Game Capture](https://github.com/steveseguin/game-capture), with a separate executable, settings, installer identity, browser profiles, logs, and update feed.

Development version: **0.3.2**. Fork baseline: `a76d44feaf01ad228aa99ed35531454be6fe1b4b` (Game Capture 0.2.62). The original project is independent and unchanged.

## Use the studio

1. Open `video-mixer.exe`. Select a scene and choose **Add source**.
2. Add windows/games, webcams, Spout2 feeds, microphones, desktop audio, images, titles, colors, or browser URLs. Check a source to include it in Preview.
3. Drag sources in Preview or use X/Y/W/H, opacity, layer order, and the Full, Side by side, and Picture in picture presets.
4. **Take / Cut** or **Fade** copies the staged scene to Program. Editing Preview does not change the current Program layout.
5. Adjust source gain/mute in **Audio mixer**. Each track and the master have a visual dBFS level meter with colored segments, peak hold and a clip indicator. **Scene** makes audio follow Program; microphones default to always on. A master fader and peak limiter control the final mix.
6. Choose **Output settings**, leave **Mixer program** selected, and choose a destination. **Record to file (MKV)** writes H.264/AAC to a new file. Stop output to finalize it. Existing recordings are never overwritten.
7. Studio's **Record...** chooses a new recording file and starts recording directly. Its start/stop button controls the selected output and displays connection status.

Select a source and use **Edit source** to rename it, change its URL/file/device, or change browser capture mode. **Reconnect** restarts a source; windows prompt you to select a current window. These source changes apply to every scene, including Program. The source status shows errors and received frame rate.

**Audio delay** in Edit source adjusts Program audio by 0-500 ms for synchronization. Browser feeds start at 60 ms; cameras, networks and page rendering can need different values. Headphone monitoring stays immediate.

Select a webcam source and choose **Camera settings...** for a live preview, capture resolution/frame rate, and its driver-supported controls. These can include zoom, exposure, focus, brightness, contrast, white balance, saturation, sharpness, gain, backlight compensation, pan, and tilt. Sliders and numeric inputs use the device's range and step size; **Auto** appears enabled where the driver supports switching modes. **Reset** uses that control's driver default, with automatic mode where supported.

Camera changes apply immediately, including to Program. **Apply capture mode** briefly restarts that source and shows the actual negotiated mode; the compositor remains 1280x720/30. Explicitly changed controls and the requested mode are saved with the source and reapplied on reconnect/show reload. Selecting a different physical camera clears those overrides. Opening the settings panel does not change the device. Exposure and low light can reduce delivered frame rate, and other applications may share the camera's hardware settings.

**Headphone monitor** has its own device and volume controls and starts off each launch. A channel's **Listen** sends it to headphones, including while off air; **Mute** silences both monitoring and Program. Microphones start with Listen off. Desktop audio excludes Video Mixer and its browser processes, preventing its monitor output from feeding back into the mix. Use headphones for guests and microphones.

**Program window** provides clean output (F11 for fullscreen). Shows autosave under `%LOCALAPPDATA%/VideoMixer/studio.mixer.json`; Save show as/Open show transfer layouts and source settings. Show files include private source URLs and invitation links.

## Guests, phones, and chat

- **Invite guest** creates a private VDO.Ninja room invitation and a receiving source. Guests choose and start their camera/microphone in their browser. Prepare their source in Preview and Take when ready. The room is retained in the saved show.
- **Wireless camera** creates a similar invitation with the rear-camera preference for a phone.
- **Social chat** accepts a Social Stream Ninja session ID or overlay URL. A session ID adds its featured-message overlay. Select it and choose **Open / Invite** to open its chat dock and feature messages. Connect social platforms in Social Stream Ninja first.
- Browser sources also accept existing VDO.Ninja viewing URLs. **Open / Invite** provides browser interaction.

**Guest talkback** opens the room's VDO.Ninja director controls. Enable its microphone, then unmute to address guests, or use **Solo Talk** for one guest. The panel starts muted, with incoming playback off; use Studio's headphone monitor to hear guests once. Room guests hear each other without receiving their own audio. Talkback is separate from Program; add your microphone as a Studio source when it should be on air. Existing standalone invitations from 0.1.0 must be recreated to join the room.

If the talkback microphone is also a live Studio source, mute that Studio channel before speaking privately to guests.

## Engine and current limits

The original controls remain in **Capture & output**: standalone window/camera/Spout capture, hardware encoders, codecs, transparency workflows, audio encoding settings, and VDO.Ninja, WHIP, SRT, and RTMP/RTMPS outputs. Studio supplies frames and audio to that same output engine. Use the Studio faders when Mixer program is selected.

- Studio renders **1280x720 at 30 fps** using Qt. Output resolution presets can scale this image.
- Opaque browser sources use **Windows Graphics Capture targeting 30 fps**, avoiding PNG encoding/decoding. Actual cadence depends on hardware and page activity. **Preserve browser transparency** uses snapshots at up to **15 fps**; Social chat defaults to that mode. The compositor still reads back to CPU memory.
- One output destination at a time, including recording. Simultaneous recording/streaming, virtual-camera/Spout output, video-file playback, and cropping are not implemented. Guests receive room talkback/other guests, rather than a return of the full Program mix.
- Reconnect window sources after reopening a show: window handles are transient. Missing devices/images/runtime report errors. Microphone and headphone device selections can be changed in Studio.
- Browser audio is routed from Web Audio/media streams to the native mixer. Browser speaker sessions are silenced; only enabled native monitoring plays locally. Ordinary Web Audio and WebRTC receivers are supported. Protected media, cross-origin media without CORS permission, and unusual page audio graphs may not expose audio; check the channel meter before going live. Chat overlays are visual sources.
- Application/desktop loopback audio needs Windows build 20348 or newer. Camera/mic capture and the browser audio bridge do not use that process-loopback API. Avoid capturing the same game's audio both individually and via Desktop audio.

## Browser runtime

Uses **Microsoft Edge WebView2 Evergreen**. No Chromium engine is bundled. Microsoft's runtime updates separately; users install it if missing. The SDK/loader is a build dependency, not the browser engine. [Microsoft distribution guidance](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution).

Profiles live under `%LOCALAPPDATA%/VideoMixer/browser/`. Receiving/overlay pages cannot access local cameras or microphones. The explicitly opened talkback panel may ask for microphone access on `https://vdo.ninja`. Browser downloads and non-HTTP(S) top-level navigations are blocked. Guests grant camera permissions in their own browser. Press Escape to park an interactive browser source.

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

Portable executable: `native-qt/dist/video-mixer-portable/video-mixer.exe`.

Archives and setup: `native-qt/dist/video-mixer-0.3.2-windows-x64.zip` and `video-mixer-0.3.2-setup.exe`. Setup installs under `%LOCALAPPDATA%/Programs/Video Mixer`, includes Qt/VC runtime DLLs, creates Start menu shortcuts, and preserves shows/recordings on uninstall. It does not bundle WebView2 or modify firewall rules. These development packages are unsigned. `installer-mixer.nsi` is the current installer; `installer.nsi` remains the inherited machine-wide installer.

The pinned SDK is fetched from NuGet; other native dependencies use CMake FetchContent. Downloaded dependencies, build outputs, local shows, and QA artifacts are excluded from Git.

## Validation and provenance

[Audio meters and scroll-panel validation](docs/mixer-0.3.2-validation.md) covers 0.3.2's visible track/master meters and distinct scroll surfaces. [Performance review and measurements](docs/mixer-performance-review.md) covers 0.3.1's scene caching, capture buffer ownership, audio-loop savings and canvas selection fixes, plus remaining camera-startup and browser-capture limits. [Camera-control validation](docs/mixer-camera-validation.md) covers 0.3.0. [0.2.0 installed-app validation](docs/mixer-0.2-validation.md) and [initial 0.1.0 validation](docs/mixer-validation.md) retain earlier workflow results and remaining gaps. Inherited gates remain under `native-qt/tests`, `qa`, and `e2e`. A successful build or gate is not end-to-end testing.

[Original Game Capture documentation](docs/game-capture-base.md) is historical reference; its release numbers and screenshots describe the base application.

The original GPL license and attribution are retained; see [LICENSE](LICENSE). Packages also include FFmpeg notices and the WebView2 SDK license.
