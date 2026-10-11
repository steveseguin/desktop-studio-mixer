# Video Mixer 0.2.0 - installed application validation

Date: 2026-10-11. Windows x64, build 26200, installed WebView2 Evergreen Runtime. Release executable packaged by `native-qt/build-mixer.ps1 -Installer`, installed to an isolated per-user QA directory, and exercised through the desktop UI.

Executable SHA-256: `45a0125a8726f27187c06aa027b6cf90d36517b97ec62f51a96494c69f759f60`.

## Actual application workflows

- Opaque WebView2 browser pixels reached Program through Windows Graphics Capture: about 21.5 received fps on this machine, with a 30 fps target. Edited the same source to use transparent snapshots and back, renamed it, and reconnected it while live. Both capture modes rendered the expected pixels.
- Recorded Windows playback loopback while toggling the native headphone monitor and source mute. The 731 Hz tone measured 0 with monitoring off, 0.03996 with monitoring on, and 0 when muted. With Desktop audio also enabled, muting the browser again produced silence; the app's output did not feed back through desktop capture. Listening to an off-air source remained audible.
- Captured an independent animated native window, a physical Logitech HD Pro Webcam C920 (about 29.3 received fps), and its microphone. Microphone monitoring started off. Composited a transparent chat fixture, switched scenes with cuts/fades, and recorded the mixed output.
- Published the actual Program to an FFmpeg SRT listener bound to loopback. The receiver wrote H.264 video and AAC audio. No Program stream was sent to a public service.
- Created private guest and wireless-camera invitations in Studio. Two synthetic Edge WebRTC publishers joined those invitations. Verified received audio/video packets and decoded nonzero audio from the actual Program recording.
- Opened Guest talkback, enabled its microphone, verified its initial muted state, unmuted it, and measured nonzero received audio energy at both guest peers. Remuted it afterward. Browser input was synthetic for this check; this does not verify a physical phone or acoustic round trip.

- Kept both synthetic WebRTC guests connected while adding the physical camera/microphone, native window, and transparent chat fixture. Recorded scene cuts/fades and streamed the same six-input session to a private loopback SRT receiver. Decoding verified moving H.264 frames and AAC audio in both outputs: 13.65-second recording, audio RMS 0.16848; 8-second SRT capture, audio RMS 0.19048; 1280x720 video and 48 kHz stereo audio.
- Used Studio's **Record...** file dialog to record a shared-clock browser beep/flash fixture. With the default 60 ms browser audio delay, nine pairs measured audio minus video offsets from -26.7 to +40.0 ms, median +6.7 ms. This measures this local browser path, not network or acoustic latency; other sources may need a different delay.
- Added a private Social Stream session through Studio, opened the real chat dock, injected a synthetic incoming message, featured it, and verified that the actual featured overlay reached native Program. Sent the dock's clear-overlay command afterward.
- Installed and upgraded the per-user package, verified the installed executable matched the packaged SHA-256, and ran the workflows above from that installation. Uninstall removed the app, registration, and Start menu shortcuts while preserving saved-show and recording hashes. The installer needs no administrator access and makes no firewall changes.
- Quit after using browser capture, the chat dock, and talkback; the process exited normally. Restored the starter show, disabled monitoring, and selected a local recording destination after QA.

## Reproduce

Install `native-qt/e2e/mixer-requirements.txt` into a separate Python environment. Run against a disposable starter show with the packaged or installed application already open. From `native-qt`:

```powershell
python -B e2e/mixer-production-e2e.py --pid <app-process-id> --report e2e/reports/mixer-phase2
python -B e2e/mixer-guests-e2e.py --pid <app-process-id> --report e2e/reports/mixer-phase2
python -B e2e/mixer-sync-e2e.py --pid <app-process-id> --report e2e/reports/mixer-phase2
python -B e2e/mixer-chat-e2e.py --pid <app-process-id> --report e2e/reports/mixer-phase2
```

The production/combined workflows expect a C920 camera/microphone; adapt their device names for another machine. They open those physical inputs, change temporary QA sources and output settings, and save private recordings locally. Guest/chat workflows require Microsoft Edge and network access to VDO.Ninja/Social Stream Ninja. They use private synthetic peers/messages, without inviting people or authenticating social accounts.

Guest/chat automation attaches to the actual embedded WebView2 pages. Launch the QA app with child-process environment variable `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=0 --use-fake-ui-for-media-stream --use-fake-device-for-media-stream`. These are QA launch flags only; the shipped app does not set them. Use normal launch settings afterward.

Screenshots, recordings, logs, and machine-readable results stay under ignored `native-qt/e2e/reports/mixer-phase2/`. They can contain camera images, microphone audio, and private invitation credentials; they are not repository assets. Build and diff checks are additional gates, not substitutes for these application workflows.

## Remaining limits

- Physical phone operation, human guests on another machine, microphone permission prompts in an ordinary talkback launch, and acoustic round-trip latency remain unverified.
- Authenticated social-platform ingestion and moderation remain unverified; the chat check uses a synthetic incoming event in the actual private Social Stream dock.
- This session verifies local SRT publishing and MKV recording. Outbound VDO.Ninja, WHIP, RTMP/RTMPS, Spout senders, and the inherited full release suite have not been requalified.
- Windows 10, missing/older WebView2 installations, high-DPI displays, high source counts, device hot-unplug, and long-running sessions remain unverified.
- The compositor is 1280x720 at 30 fps with CPU readback. Transparent browsers use snapshots capped at 15 fps. Browser audio depends on exposed Web Audio/media streams. Protected/CORS-restricted media and unusual page audio graphs may be unavailable.
- There is one output destination at a time. Guests receive room audio/talkback, not the full Program return. Simultaneous recording/streaming, cropping, video-file playback, and virtual-camera/Spout output are outside this version.

This is an unsigned development release with the exercised workflows above, not a production-qualified release. Initial 0.1.0 results, including overwrite protection and show persistence, are retained in [mixer-validation.md](mixer-validation.md).
