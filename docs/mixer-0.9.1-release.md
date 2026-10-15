# Desktop Studio 0.9.1 Windows release

Windows x64 installer and portable ZIP for Desktop Studio, published in the
existing private `steveseguin/video-mixer` repository.

- Scenes, single-canvas and studio workspaces, landscape/portrait formats,
  guest invitations, browser overlays with transparent backgrounds and audio,
  audio mixing/monitoring, recording and streaming.
- Local CLI, authenticated HTTP API and 40-tool MCP interface, including
  headless operation and application screenshots.
- Conservative 4,000 kbps streaming defaults, opt-in microphones, encoder pacing
  improvements, AMD encoder compatibility corrections and resilient process
  audio format negotiation.
- Isolated browser/media/output workers, recoverable recordings, bounded
  diagnostics and improved WHIP NACK/RTX repair budgeting.

## Exact Windows package checks

The signed packaged app SHA-256 is
`8a61931746fbf2229f173d68617d19cae2b33874e2a14bdfea4ef0d63e3a0244`.
It was built from source commit `1f19c3d` with version 0.9.1.

The installer was run into a new private QA directory. Installed and portable
ZIP executable hashes matched. CLI/MCP commands, 12 distinct live portrait
screenshots, recording playback and uninstall passed; maximum recorded frame
interval during screenshots was 34 ms. No owned child processes remained.

The packaged app also passed scene switching, headless screenshots, concurrent
SRT publishing/recording, RTMP reception/recording, browser transparency and
default audio capture, browser crash/audio recovery, API access-control cases,
MCP controls, five themes, keyboard scene selection and Spanish core menus.
The user's original show was unchanged. Evidence is retained under
`native-qt/e2e/reports/release-0.9.1/installed`, `automation`, and
`automation-core`. The `automation` run failed on the WHIP receiver path below;
its failure was retained, and `automation-core` completed the other workflows.

## Known limitations

- WHIP to MediaMTX followed by FFmpeg RTSP reception can produce duplicate
  timestamps near startup. This reproduced in the release package. The separate
  local recording is not dependent on that receiver/remux path.
- Chromium WHIP playback can pause for 300-366 ms with 1% packet loss plus
  jitter/reordering. Disabling audio RED or increasing playback buffering did
  not eliminate the measured pauses. Neither workaround is advertised as a fix.
- Physical AMD hardware/driver behavior, overnight endurance and clean-machine
  validation remain outstanding. The available test host has Intel/NVIDIA GPUs.

The preceding [fault and performance results](encoding-defaults-validation-2026-10-14.md)
retain successful measurements and failed receiver cases separately.

The setup and application carry a Social Stream Ninja signature. This host's
Windows verifier reports an untrusted certificate root; signing is not a claim
of a publicly trusted certificate or SmartScreen reputation. Qt/VC runtime and
the LGPL FFmpeg bundle are included; Microsoft WebView2 Evergreen is installed
and updated separately. SHA256SUMS.txt accompanies the release assets.
