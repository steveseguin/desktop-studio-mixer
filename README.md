# Video Mixer

A native Windows video mixer based on [Game Capture](https://github.com/steveseguin/game-capture), with a separate executable, settings, installer identity, browser profiles, logs, and update feed.

Initial development version: **0.1.0**. Fork baseline: `a76d44feaf01ad228aa99ed35531454be6fe1b4b` (Game Capture 0.2.62). The original project is independent and unchanged.

## Use the studio

1. Open `video-mixer.exe`. Select a scene and choose **Add source**.
2. Add windows/games, webcams, Spout2 feeds, microphones, desktop audio, images, titles, colors, or browser URLs. Check a source to include it in Preview.
3. Drag sources in Preview or use X/Y/W/H, opacity, layer order, and the Full, Side by side, and Picture in picture presets.
4. **Take / Cut** or **Fade** copies the staged scene to Program. Editing Preview does not change the current Program layout.
5. Adjust source gain/mute in **Audio mixer**. **Scene** makes audio follow Program; microphones default to always on. A master fader and peak limiter control the final mix.
6. Choose **Output settings**, leave **Mixer program** selected, and choose a destination. **Record to file (MKV)** writes H.264/AAC to a new file. Stop output to finalize it. Existing recordings are never overwritten.

**Program window** provides clean output (F11 for fullscreen). Shows autosave under `%LOCALAPPDATA%/VideoMixer/studio.mixer.json`; Save show as/Open show transfer layouts and source settings. Show files include private source URLs and invitation links.

## Guests, phones, and chat

- **Invite guest** creates a private VDO.Ninja publishing link and a receiving source. Guests choose and start their camera/microphone in their browser. Prepare their source in Preview and Take when ready.
- **Wireless camera** creates a similar invitation with the rear-camera preference for a phone.
- **Social chat** accepts a Social Stream Ninja session ID or overlay URL. A session ID adds its featured-message overlay. Select it and choose **Open / Invite** to open its chat dock and feature messages. Connect social platforms in Social Stream Ninja first.
- Browser sources also accept existing VDO.Ninja viewing URLs. **Open / Invite** provides browser interaction.

Invitations are individual incoming feeds. Backstage communication and two-way guest talkback are not implemented yet.

## Engine and current limits

The original controls remain in **Capture & output**: standalone window/camera/Spout capture, hardware encoders, codecs, transparency workflows, audio encoding settings, and VDO.Ninja, WHIP, SRT, and RTMP/RTMPS outputs. Studio supplies frames and audio to that same output engine. Use the Studio faders when Mixer program is selected.

- Studio renders **1280x720 at 30 fps** using Qt. Output resolution presets can scale this image.
- Browser sources use asynchronous WebView2 snapshots at up to **15 fps**, depending on page complexity. Native capture requests 30 fps. The browser path is an initial implementation, not a GPU texture compositor.
- One output destination at a time, including recording. Simultaneous recording/streaming, virtual-camera/Spout output, video-file playback, cropping, and monitoring/mix-minus controls are not implemented yet.
- Re-add window sources after reopening a show: window handles are transient. Missing devices/images/runtime report errors.
- Browser audio uses Windows process-loopback capture and requires a Windows build supporting that API. Browser playback is also audible locally. Avoid mixing desktop audio alongside the same individual feed. Source mute affects the rendered mix, not local browser monitoring.

## Browser runtime

Uses **Microsoft Edge WebView2 Evergreen**. No Chromium engine is bundled. Microsoft's runtime updates separately; users install it if missing. The SDK/loader is a build dependency, not the browser engine. [Microsoft distribution guidance](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution).

Profiles live under `%LOCALAPPDATA%/VideoMixer/browser/`. Receiving/overlay pages cannot automatically access local cameras or microphones. Browser downloads and non-HTTP(S) top-level navigations are blocked. Guests grant camera permissions in their own browser.

## Build

Requires Windows x64, Visual Studio 2022 C++ tools/Windows SDK, CMake 3.24+, Ninja, and Qt 6 Widgets/Network/Concurrent. The script uses `VCPKG_ROOT`, or `C:/vcpkg` when unset.

```powershell
# Prepare FFmpeg for recording, RTMP/SRT and FFmpeg-dependent codecs:
powershell -ExecutionPolicy Bypass -File native-qt/tools/fetch-ffmpeg-lgpl.ps1

# Restore the WebView2 SDK, configure, build, and package Qt:
powershell -ExecutionPolicy Bypass -File native-qt/build-mixer.ps1 -Package
```

Portable executable: `native-qt/dist/video-mixer-portable/video-mixer.exe`.

The pinned SDK is fetched from NuGet; other native dependencies use CMake FetchContent. Downloaded dependencies, build outputs, local shows, and QA artifacts are excluded from Git.

## Validation and provenance

[Packaged workflow validation](docs/mixer-validation.md) records exercised workflows and remaining gaps. Inherited gates remain under `native-qt/tests`, `qa`, and `e2e`. A successful build or gate is not end-to-end testing.

[Original Game Capture documentation](docs/game-capture-base.md) is historical reference; its release numbers and screenshots describe the base application.

The original GPL license and attribution are retained; see [LICENSE](LICENSE). Packages also include FFmpeg notices and the WebView2 SDK license.
