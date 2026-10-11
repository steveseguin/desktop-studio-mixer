# Windows 0.2.57 release closeout follow-up

Date: September 7, 2026, with an installed-app follow-up after the user completed administrator installation. Status: published and installed GUI/OBS end-to-end checks passed; upgrade and uninstall lifecycle remain unverified.

## Follow-up after administrator installation

The user completed installation with administrator privileges. The installed `C:\Program Files\Game Capture\game-capture.exe` matches the published 0.2.57 EXE SHA-256 below. Windows registers version 0.2.57 and its uninstaller. Desktop and Start menu shortcuts both target the installed executable with its installation directory as their working directory. The enabled inbound allow rule `Game Capture WebRTC UDP` targets that exact executable, UDP, all profiles.

All 94 installed counterparts from the ZIP match byte-for-byte. Three ZIP files are absent: `qt.conf`, `release-artifact-manifest.json`, and `vc_redist.x64.exe`. The release installer source omits these files from its copy list; the installed application successfully loaded Qt and bundled FFmpeg on this machine. This does not establish behavior on a clean Windows machine lacking existing runtime dependencies.

The **installed GUI** was launched and used to select the moving Spout2 fixture and Go Live with VP9 software, 1280x720 at 30 FPS, alpha enabled, no audio. A real OBS 32.2.2 session loaded the verified VDO.Ninja 1.1.68 DLL. Both initial playback and Stop/Go Live recovery passed ten-distinct-image transparency/motion analysis. OBS recording passed at 30.00 decoded FPS and 29.8755 distinct moving frames/sec, with zero render or output skips.

The original application settings were restored with matching registry export hashes, and the test-owned processes were closed. Game Capture remains installed. The administrator installer interaction itself was performed by the user, not observed by this automation; upgrade from 0.2.56, upgrade settings retention, and uninstall/reinstall were not exercised.

Installed-app evidence is under `native-qt/qa/reports/installed-closeout-0.2.57/`: registration, firewall filters, shortcut targets, payload comparison, live diagnostics, pixel sequences, recording results, and cleanup confirmation. The following sections retain the earlier ZIP validation and canceled elevation attempt as historical evidence.

## Artifacts and actual runtime

The latest published release was 0.2.57 when this work began. The published Windows ZIP digest was verified against the GitHub release asset. The GUI was launched from `native-qt/dist/game-capture-0.2.57-win64/game-capture.exe`, not a development build or an installed executable.

| Artifact | SHA-256 |
| --- | --- |
| Game Capture 0.2.57 EXE | `ffe317a2a0a1234d6af7c920b64509c90300c631898f10da29964aeb88575166` |
| Published 0.2.56 installer | `835a7675e5ab2cdd0a33fe300270b3967f23fb4731c13e158260072292e6a545` |
| Published 0.2.57 installer | `7c58b936c5a264b359c065c62e5feef6f2645d4075bac6a517c50ab33ac110a2` |
| Running OBS 32.2.2 portable EXE | `e79066670cd8e8b95662f4cd417c4fa6f5b0c77e856a7b41c7de0e00059ba6f2` |
| Loaded OBS VDO.Ninja 1.1.68 DLL | `09975b30d4d4e917dd911dc8d971e19dbe08684a795dcb2668845da4f983f5cb` |

The OBS process was actually launched and its loaded module path/hash verified. A moving Spout2 alpha fixture was selected through the released Game Capture GUI. Go Live, Stop, and Go Live again were invoked through GUI controls. OBS rendered the native receiver over a colored background and recorded its output.

## Results

- GUI launch and local diagnostics: passed.
- GUI Spout2 selection and publishing at VP9 software, 1280x720, 30 FPS, alpha enabled, no audio: passed; active encoder/codec verified from diagnostics.
- OBS transparency and motion: passed, with ten distinct decoded composite images at each of three checkpoints: initial VP9 publishing, Stop/Go Live recovery, and application quit/relaunch.
- OBS recording: passed. Decoded output was 30.00 FPS; independently measured moving-edge and changed-frame cadence were both 29.8755 FPS. Zero render skips, zero output skips, and no consecutive held-frame interval were reported by the recording analyzer.
- Stop: diagnostics confirmed both capturing and live were false.
- GUI settings persistence across quit/relaunch: passed for Spout2 mode, stream ID, VP9, software encoder, 1280x720, 30 FPS, no audio, and alpha enabled. Selecting the fixture and going live after relaunch restored valid moving alpha in OBS.
- Cleanup: test-owned GUI, OBS, and sender processes closed. OBS harness restored its configuration. Original `HKCU\Software\VDO.Ninja` settings were restored, and before/after registry exports matched exactly.

## Failures reviewed and limitations

The first OBS sample was blank because the GUI still used its saved AV1 selection, which the native receiver does not support. UI Automation selection/default-action calls highlighted combo-box entries without applying their values. Targeted mouse messages to the actual popup applied the settings; both accessible values and live diagnostics then confirmed VP9. The subsequent pixel and recording checks passed. This was a configuration/automation failure, not evidence of a new VP9 compositor defect. The failed sample is retained with the successful evidence.

The 0.2.56 installer was launched with elevation to begin a 0.2.56-to-0.2.57 upgrade check. Windows returned “The operation was canceled by the user” at UAC; the installer never ran. The session is not elevated, and the installer requires administrator privileges. No elevation bypass or second elevation attempt was made.

Consequently, clean installation, installed executable launch, 0.2.56-to-0.2.57 upgrade, installer settings retention, shortcuts, firewall rules, and uninstall have **not** been verified by this follow-up. The release cannot be described as fully closed until these checks run with approved administrator elevation. GUI persistence above is an application restart check, not an installer upgrade check.

Other limitations from `release-0.2.57-windows-validation.md`, including hardware-specific coverage gaps, still apply. This follow-up did not repeat the earlier long-running regression matrix or modify production code. Concurrent working-tree changes were preserved.

## Local evidence

Evidence root: `native-qt/qa/reports/installer-closeout-0.2.57/` (ignored local artifacts).

- `obs-ready.json`: running OBS and loaded plugin identities.
- `gui-vp9-confirmed-diagnostics.json`: active VP9/alpha configuration.
- `gui-vp9-confirmed-result.json`, `gui-vp9-restarted-result.json`, `gui-vp9-relaunch-result.json`: successful motion/alpha pixel sequences.
- `gui-vp9-recording-result.json`: recording cadence, file path, digest, and OBS performance counters.
- `gui-stopped-diagnostics.json`, `gui-persisted-settings.json`: Stop and restart behavior.
- `obs/`: screenshots, recording, and accumulated runtime results.
- `cleanup.json`: settings restoration and process cleanup.

Registry backups and local-control discovery files contain private settings/tokens and must remain local.
