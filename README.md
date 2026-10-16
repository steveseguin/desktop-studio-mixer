# Desktop Studio

A Windows studio for live streams, podcasts, interviews and recordings. Mix cameras, games, guests and browser overlays in one workspace, powered by VDO.Ninja.

**[Download for Windows](https://github.com/steveseguin/desktop-studio-mixer/releases/latest)** · [Website](https://steveseguin.github.io/desktop-studio-mixer/) · [User guide](docs/mixer-guide.md)

[![Desktop Studio with a welcome scene in Preview, a game and overlay in Program, scene controls and the audio mixer.](media-assets/screenshots/02-preview-and-program.png)](media-assets/screenshots/02-preview-and-program.png)

*Actual app screenshot. [More screenshots and a short walkthrough](media-assets/README.md).*

## Make your show

- **Scenes and sources:** windows/games, webcams, video, images, titles and transparent browser overlays with audio.
- **Your workspace:** edit directly on one canvas, or stage scenes in Preview before taking them live. Landscape and portrait formats.
- **Audio and guests:** track meters, mute/gain, headphone monitoring, VDO.Ninja guest invitations with mix-minus, wireless cameras and social chat overlays.
- **Stream and record:** independent local recording alongside VDO.Ninja, RTMP/RTMPS, SRT or WHIP output. Advanced destinations include RTSP, UDP and RIST.
- **Local control:** CLI, HTTP API and MCP, headless operation and built-in screenshots. Keyboard access, languages and themes.

## Get started

1. Install the Windows x64 build, or extract the portable ZIP and open `desktop-studio.exe`.
2. Choose **Add source**, arrange your scene, and check the audio meters.
3. Use **Record...** for a local recording, or choose **Output settings** and **GO LIVE**.

Browser sources require [Microsoft WebView2 Evergreen](https://developer.microsoft.com/microsoft-edge/webview2/). Its browser engine updates separately. Start with the default 4 Mbps stream and leave upload headroom for audio and recovery traffic.

## Guides

| Need | Read |
| --- | --- |
| Scenes, camera controls, guests, audio and recording recovery | [User guide](docs/mixer-guide.md) |
| Destinations, codecs, bitrate and loss recovery | [Streaming guide](docs/mixer-streaming.md) |
| AI assistants, CLI, API and headless control | [Automation](docs/mixer-automation.md) |
| Browser isolation, requirements, building and test results | [Technical details](docs/mixer-technical.md) |
| Screenshots, video and machine-readable asset descriptions | [Media library](media-assets/README.md) |

Based on [Game Capture](https://github.com/steveseguin/game-capture). [GPL license and attribution](LICENSE).
