# Video Mixer 0.1.0 - initial packaged workflow validation

Date: 2026-10-11. Windows x64, installed WebView2 Evergreen Runtime, Release portable package produced by `native-qt/build-mixer.ps1 -Package`.

Executable SHA-256: `447e6b089dc22950e60205a1fe5afb40b2a99605b0be8e99279e5e04968110e2`.

## Exercised in the actual portable app

- Switch Opening/Main/Break in Preview; verify Program remains unchanged until Cut or Fade.
- Add a loopback browser fixture to WebView2, capture its rendered pixels, and take it to Program.
- Record that source with its 440 Hz audio, mute its channel, switch to a title scene, and stop recording.
- Attempt to record to an existing MKV; verify the app rejects the path and the existing file's SHA-256 is unchanged.
- Decode the resulting 15.36-second MKV: H.264, 1280x720, 30 fps; AAC, 48 kHz stereo. At two seconds the expected green browser pixels were present and audio RMS was 0.11999. After mute and the scene switch, the title background was present and audio RMS was zero.
- Capture a separate animated native window, stage it, and take it to Program.
- Attempt to remove an on-Program source; verify the source remains. Take another scene, remove the source, and verify capture stops.
- Create guest and wireless-camera invitations through the dialogs; verify HTTPS VDO.Ninja push URLs, random stream/password fields, and the phone rear-camera preference. No links were sent to anyone.
- Restart the app and verify scenes, sources, and audio settings persist. Remove the temporary sources and verify the clean starter show persists on another restart.
- Quit the app after using browser sources and verify its process exits. The first implementation blocked Quit because offscreen browser windows rejected close events; the packaged build above fixes that.

The executable was exercised through Windows UI Automation and native capture windows. Build and diff checks were additional gates, not substitutes for these workflows. Local screenshots, recordings, and machine-readable results are under `native-qt/e2e/reports/mixer/` and excluded from Git.

## Reproduction helpers

Use the dependencies in `native-qt/e2e/desktop-ui-requirements.txt`. Launch the portable app with its starter scenes, then run from `native-qt`:

```powershell
python e2e/mixer-desktop-e2e.py --pid <app-process-id>
python e2e/mixer-media-check.py <recording-from-workflow.json> --ffmpeg dist/video-mixer-portable/ffmpeg/bin/ffmpeg.exe --report e2e/reports/mixer/media.json
python e2e/mixer-sources-e2e.py --pid <app-process-id>
```

These helpers operate the desktop and change the show's temporary QA sources. The source workflow removes those temporary sources and quits the app. Run against a disposable starter show. They do not open physical cameras/microphones or publish to an external streaming destination.

## Not yet verified

- Physical webcams, microphones, Spout senders, real guest/phone senders, and two-machine A/V synchronization.
- Authenticated Social Stream platform connections, live messages, and chat-dock moderation.
- Mixer output to VDO.Ninja, WHIP, SRT, or RTMP services; those inherited transport paths were retained, but this session verified local recording only.
- Windows 10, an absent/older WebView2 Runtime, high source counts, long-running sessions, and the inherited full release qualification suite.

This is a working initial development build, not a production-qualified release. The README describes current resolution/cadence, audio monitoring, guest talkback, and output limitations.
