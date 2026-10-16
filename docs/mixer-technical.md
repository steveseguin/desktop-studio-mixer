---
layout: default
title: Runtime, development and validation
---

[Desktop Studio](../) · [User guide](mixer-guide.md) · [Streaming](mixer-streaming.md) · [Technical details](mixer-technical.md)

# Runtime, development and validation

## Project history

Fork baseline: `a76d44feaf01ad228aa99ed35531454be6fe1b4b` (Game Capture 0.2.62). The original project is independent and unchanged.

Previously named **Video Mixer**. Setup upgrades the existing installation and replaces its Start menu shortcuts. Existing shows, output profiles, camera settings, browser profiles and the layout preference carry over. The repository/build target, `%LOCALAPPDATA%/VideoMixer` data folder and `VDO.Ninja/Video Mixer` settings identity remain stable for compatibility. Older validation documents use the name of the version they measured.


## Engine and current limits

The original controls remain in **Capture & output**: standalone window/camera/Spout capture, hardware encoders, codecs, transparency workflows, audio encoding settings, and VDO.Ninja, WHIP, SRT caller/listener, RTMP/RTMPS, RTSP TCP/UDP, UDP MPEG-TS and RIST outputs. The **SRT mode** selector switches between connecting to a receiver and accepting a receiver at the configured local address. Passphrase encryption works in both modes. Listener output retries after stalled connections; it is not a permanent multi-client server. Rendezvous remains unsupported by this integration. Studio supplies frames and audio to that same output engine. Use the Studio faders when Mixer program is selected.

- The canvas supports native **720p30/60, 1080p30/60, portrait 720x1280/30/60 and 1080x1920/30/60**. Mixer streaming and independent recording follow that format. Stop outputs before changing it. Existing window/Spout sources reconnect to negotiate the new size and rate; webcams retain their separately selected capture mode. **Stacked** places interview pictures above one another; lock backgrounds/overlays before applying layout presets to preserve their placement. **CPU composition** is the default, based on lower measured cost for the common 720p mix. Optional **GPU composition** uses Direct2D/D3D11 with CPU fallback. Source/output frames still pass through CPU memory, so this is not a zero-copy GPU pipeline.
- New browser sources and Social chat default to **Transparent overlay background**, using snapshots at up to **15 fps**. Overlay mode overrides the top-level HTML/body background to transparent on each navigation; message cards, embedded pages and other element styling remain intact. Social Stream Ninja can still supply its own custom CSS. Turn overlay mode off for opaque browser video using **Windows Graphics Capture targeting 30 fps**, avoiding PNG encoding/decoding. Existing saved capture modes are preserved. Actual cadence depends on hardware and page activity. The compositor still reads back to CPU memory.
- One streaming destination, plus independent recording, Spout output and virtual-camera output. **Spout output** publishes `Desktop Studio Program`. **Virtual camera** registers `Desktop Studio Virtual Camera` for the current Windows account without administrator access. It supplies video; select an audio device in the receiving app separately. This is a 64-bit DirectShow camera, verified in FFmpeg and Edge; applications requiring other camera interfaces may not support it. **Unregister camera** removes its registration. Local outputs start off each launch and are controllable through `local.set` in the CLI/API/MCP. Their producers run in separate workers, with bounded pending frames, crash/hang recovery and a restart limit. If an output pauses after repeated failures, turn it off and on to retry. Camera registration also runs outside the main app and UI thread.
- **Video / audio file** uses Windows Media Foundation codecs, including H.264/AAC MP4 and WAV. **Playback** offers pause, restart and looping. Local-file audio uses a 10 ms startup buffer and brief loop read-ahead to tolerate decoder jitter. Unsupported codecs report an error. Audio-only files contribute audio without occupying a video layout position.
- Cameras and native sources start asynchronously. Failed native captures retry with backoff; browser navigation failures and browser process crashes recover automatically. Windows restore by a unique executable/title match, including when their window reopens. Ambiguous or renamed windows require selecting the intended window through Reconnect. Missing devices/images/runtime report errors. Physical Logitech C920 unplug/reconnect recovered camera and microphone while recording and publishing continued; other camera drivers still need hardware coverage.
- Browser audio is routed from Web Audio/media streams to the native mixer. Browser speaker sessions are silenced; only enabled native monitoring plays locally. Ordinary Web Audio and WebRTC receivers are supported. Protected media, cross-origin media without CORS permission, and unusual page audio graphs may not expose audio; check the channel meter before going live. Chat overlays are visual sources.
- Application/desktop loopback audio needs Windows build 20348 or newer. Camera/mic capture and the browser audio bridge do not use that process-loopback API. Avoid capturing the same game's audio both individually and via Desktop audio.

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

Archives and setup: `native-qt/dist/desktop-studio-0.9.1-windows-x64.zip` and `desktop-studio-0.9.1-setup.exe`. Setup installs under `%LOCALAPPDATA%/Programs/Desktop Studio`, includes Qt/VC runtime DLLs, creates Start menu shortcuts, and preserves shows/recordings on uninstall. It does not bundle WebView2 or modify firewall rules. Release notes identify package signing and validation status. `installer-mixer.nsi` is the current installer; `installer.nsi` remains the inherited machine-wide installer.

The pinned SDK is fetched from NuGet; other native dependencies use CMake FetchContent. Downloaded dependencies, build outputs, local shows, and QA artifacts are excluded from Git.

## Validation and provenance

[0.9.2 Windows release validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.9.2-release.md)
covers the signed package on RX 9060 XT, codec playback, Auto fallback,
installation, browser/audio workflows and independent recording through faults.

[Additional 0.9.0 browser-audio and recovery testing](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.9.0-followup-validation.md) documents a fixed stale-audio bug when pages replace tracks or switch between live streams and files, plus actual app-crash, disk-full, guest and browser security workflows.

[0.9.0 project, recording and resource validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.9.0-validation.md) covers active projects and asset collection, media seeking, named audio tracks and separate videos, SRT listener, aggregate browser limits, and the remaining engineering work.

[0.8.0 production controls and validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.8.0-validation.md) covers private breaks, configurable global hotkeys, audio channel routing, guest automation, browser viewports, MP4 export, guest panels, accessibility, failure recovery, and the remaining equipment/account checks.

[0.7.4 workflow and timing review](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.7.4-validation.md) maps the main show formats and P0/P1 flows, measures loop synchronization, and records portrait support and remaining production gaps.

[0.7.3 audio and isolation validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.7.3-validation.md) covers browser audio, monitoring devices and failures, off-screen guest conversation, wireless return modes and concurrent browser overload.

[0.7.1/0.7.2 validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.7.1-validation.md) covers interface workflows, production endurance, CPU readout, real NVDA workflows, hardware/compositor matrix, browser GPU/CPU failures, disk-space failures and concurrent MCP clients.

[0.7.0 automation and accessibility validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.7.0-validation.md) covers packaged CLI/MCP control, windowless screenshots and production, private RTMP/SRT/WHIP receivers, browser recovery, keyboard access, themes and language menus.

[0.6.2 browser resilience validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.6.2-validation.md) covers a live VDO.Ninja viewer, default audio, actual GPU decoding, resource exhaustion, hangs and repeated crashes during recording. [0.6.1 overlay background validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.6.1-validation.md) covers default transparency, page security policies, reloads, source modes, composed recording and the real Social Stream Ninja workflow. [0.6.0 isolation, browser security and performance validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.6.0-validation.md) records real publishing, recording, hardware, failure-injection and sustained workflows.

[0.5.0 production validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.5.0-validation.md) covers the native canvas, independent recording, media playback, editing, audio effects, guest returns, local outputs, recovery and performance measurements.

[0.4.3 source-edit and layout validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.4.3-validation.md) covers capture continuity, unavailable devices, show-file round trips and smaller windows. [0.4.1 performance validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.4.1-validation.md) covers opaque-frame shortcuts, shared browser audio-session scanning, exact-pixel gates, and current application measurements. [Single-canvas and CPU validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.4.0-validation.md) covers direct editing, layout persistence, SIMD color conversion and transparent-image caching. [Audio meters and scroll-panel validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.3.2-validation.md) covers 0.3.2's visible track/master meters and distinct scroll surfaces. [Performance review and measurements](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-performance-review.md) covers 0.3.1's scene caching, capture buffer ownership, audio-loop savings and canvas selection fixes, plus remaining camera-startup and browser-capture limits. [Camera-control validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-camera-validation.md) covers 0.3.0. [0.2.0 installed-app validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.2-validation.md) and [initial 0.1.0 validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-validation.md) retain earlier workflow results and remaining gaps. Inherited gates remain under `native-qt/tests`, `qa`, and `e2e`. A successful build or gate is not end-to-end testing.

[Original Game Capture documentation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/game-capture-base.md) is historical reference; its release numbers and screenshots describe the base application.

The original GPL license and attribution are retained; see [LICENSE](https://github.com/steveseguin/desktop-studio-mixer/blob/main/LICENSE). Packages include FFmpeg/WebView2 notices and the RNNoise, OBS virtual-camera and libdshowcapture licenses. Vendored source, local changes and the pinned upstream commit are recorded in `native-qt/vendor/PROVENANCE.txt`.

## Developer security and QA cost controls

[Install the local commit/push secret guards](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/development-security.md) in each clone.
GitHub QA workflows are manual-only on self-hosted Windows runners, with optional
one-day artifact uploads. Routine changes do not start hosted QA jobs or nightly soaks.
