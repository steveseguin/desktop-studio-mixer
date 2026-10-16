# Desktop Studio 0.9.2 Windows release

Auto encoding now tries AMD AMF before software when Windows has no usable
hardware Media Foundation encoder but an AMD adapter is present. Working
hardware encoders retain their priority; explicit Software stays software.
If AMF cannot start, Auto continues to software. Explicit AMD remains available
in Advanced settings. The 4 Mbps streaming default is unchanged.

The README and [website](https://steveseguin.github.io/desktop-studio-mixer/)
now lead with an actual app screenshot and a short getting-started section.
Detailed workflows, streaming settings and development information are in
linked guides.

## Frozen Windows package

Built from application source commit `3ca0f8f` with version 0.9.2. Later test
scripts and documentation do not change the frozen application.
Signed executable SHA-256:
`09b06369594272e4be71a1ed11cc05fffdb78f4dd91dbadb55cde1132bfb201c`.
The setup installer and portable ZIP contain the same executable.

## Actual application workflows

- RX 9060 XT: H.264 and AV1 AMF streaming, VP9 software streaming, stereo audio,
  viewer reloads and transport restarts while recording Program independently.
  All three received the selected codec at approximately 30 fps, with no drops
  or freezes in the measured playback windows. All three recordings decoded.
- Auto selected real AMD encoding when only the owned process's hardware MFT
  discovery was hidden. Denying AMF process creation made Auto recover to
  software with playable video/audio. Installed devices were not changed.
- The signed installer was used in an isolated directory. CLI/MCP controls,
  12 changing portrait screenshots, recording playback, child cleanup and
  uninstall passed. The recording's maximum frame interval was 34 ms.
- Headless scene switching, Program/Preview/window screenshots, independent
  SRT publishing and recording, API access checks, 40 MCP tools, browser
  transparency/default audio/crash recovery, themes, Spanish core menus and
  keyboard scene selection passed. User show/settings were restored.
- The AMD fault rehearsal killed the actual AMF process, overloaded/crashed/hung
  a browser, starved bandwidth, killed publishing, then failed the browser and
  publisher together. The healthy media worker never restarted. Recording
  continued throughout; publishing and browser audio recovered independently.

The fault recording contained **7,613 frames at 30 fps**, decoded fully and
kept a maximum 34 ms frame interval and measured A/V offset of 26 ms. Nine
source pictures repeated/skipped under the workload; recording queue drops
rose from 10 to 15. It was not a zero-drop test. All five receiver clips decoded
at 30 fps; their maximum measured offsets ranged from 27 to 73.5 ms. This does
not establish a universal sub-50 ms sync guarantee. API requests peaked at
28.6 ms, and private memory peaked around 875 MiB during this short run.

Build, signing, syntax and artifact identity checks are gates, separate from
these application workflows. Evidence remains under
`native-qt/e2e/reports/release-0.9.2/` (`amd-codecs-r5`, `auto-amf`,
`auto-amf-unavailable`, `installed`, `automation-core`, `amd-faults`).

## Receiver and hardware limits

See [codec results](mixer-radeon-codecs-2026-10-16.md). AMD HEVC produced clean
output in the supplemental driver check, but the tested native OBS Ninja
receiver supports only H.264/VP9 and showed a black picture for HEVC. That
application receiver case remains failed. External outputs remain H.264.

Existing WHIP loss/jitter pauses and MediaMTX-to-FFmpeg startup timestamp
duplication are not fixed by this release. This host also has Intel/NVIDIA
GPUs; AMD-only machines, other driver versions, clean-machine setup, HDR,
demanding games and overnight endurance need separate coverage.

Application/setup signatures identify Social Stream Ninja. This host reports
an untrusted certificate root; signing does not establish SmartScreen
reputation. FFmpeg notices/source information and SHA256SUMS accompany the
downloads. WebView2 Evergreen is installed and updated separately.
