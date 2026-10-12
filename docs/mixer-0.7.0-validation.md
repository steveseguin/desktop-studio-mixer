# Desktop Studio 0.7.0 validation

Validated on Windows on October 12, 2026, using the packaged native executable,
packaged CLI/MCP scripts and actual media outputs. No public streams were created.
The original Game Capture checkout and the user's existing show were preserved.

## Packaged workflows

`native-qt/e2e/mixer-automation-e2e.py` drives real CLI, authenticated HTTP, MCP and
visible Qt workflows. Private results/media are under the ignored
`native-qt/e2e/reports/studio-automation/release` directory.

- Started the mixer with no visible main window; rendered known-color scenes,
  staged a separate Preview, faded to Program and checked exact screenshot pixels.
  Program, Preview and app-window PNGs came from the app's API. Window screenshots
  did not make the main window visible and included current canvas contents.
- Added a real H.264/AAC media file, edited layer geometry/lock and audio gain,
  mute/effects, paused/restarted playback, and round-tripped a show file.
- Published RTMP, SRT and WHIP to private local receivers while independently
  recording Program. Stopping publishing left recording running.
- Fully decoded all six receiver/independent files without decoder errors. The
  expected 731 Hz audio tone was present. Minimum half-second RMS, excluding the
  boundary half-seconds, remained above 0.1214 in these files: no silent loop gaps.
- Ran the packaged MCP stdio server through a real SDK client. Discovered 31 typed
  tools, added/selected/deleted a scene, received a PNG image content block, read
  `studio://state`, and rejected invalid tool arguments. Client disconnect left
  the app running as documented.
- Loaded a local video/audio webpage in a windowless browser source. Audio was
  enabled and the body background was transparent by default. Killed the owned
  browser worker during recording; it restarted and audio/video returned. The
  recording stayed valid. Its intentional source outage contained silence; this
  is recovery, not uninterrupted output from a crashed source.
- Switched all five palettes, selected the podcast layout, relaunched in Spanish,
  inspected Windows UI Automation names and per-track labels, used Tab/arrows to
  change scenes, and selected a workspace layout using menu keyboard mnemonics.
  Default styles and focus rules were retained; no new white focus outlines.

`mixer-automation-edge-e2e.py` additionally verified Unicode show paths and names,
rejection of a second instance opening the same show, exact Program pixel
preservation across themes, rejection of invalid output configuration without
changing the current protocol, and French menus/keyboard layout selection.
Its private evidence is under `studio-automation/edges3`.

The installer was also exercised in a disposable installation directory. The
installed CLI launched a windowless show, the installed MCP server passed its
real SDK workflow, and an independent recording decoded without errors. Quitting
left no owned worker processes. Uninstall removed the automation dependencies,
shortcuts and installer registration. The installed, portable and ZIP executables
matched SHA-256 `b6eb08a8c66e56e7f7a58886e561350ef308aba18f828e40f2681c236d4db02e`.
Private evidence is under `studio-automation/installer/installed-results.json`.

## Security and failure checks

- Unauthenticated screenshot requests returned 401. Requests carrying a browser
  Origin, even with the correct token, returned 403.
- Unknown fields, wrong JSON types, missing IDs, invalid screenshot sizes and
  `file:` browser sources were rejected. Existing screenshot/recording files
  were not overwritten. Output and canvas changes were rejected while busy.
- The local API remains opt-in, bound to 127.0.0.1, bearer-authenticated, with no
  browser CORS access. MCP uses stdio and adds no listening network port.
- CLI validates discovery endpoint/PID identity and never returns the token.
  State omits source URLs and output credentials. Authenticated local clients are
  intentionally trusted to write explicit paths and operate sources/outputs.
- Worker isolation and browser policies from 0.6.2 remain in place. This run is
  not a fresh audit of the WebView2 runtime or a guarantee against OS/driver faults.

## Issues found and corrected

- The inherited headless path bypassed the mixer. `--studio-headless` now runs
  the same workspace hidden, with no tray and no automatic publishing timeout.
- Hidden canvases skipped painting, leaving app-window screenshots blank. They
  now populate only when a window screenshot is requested.
- Output startup acceptance preceded actual publishing. State now distinguishes
  active sessions, connecting/live/reconnecting/error, and packet counters.
- Repeated media restart/loop sequences could make Media Foundation return
  `E_FAIL`, stopping picture and sound. Keeping decoder streams selected, draining
  the remaining stream explicitly, and flushing queued samples before seeking
  fixed the reproduced failure. A separate 20-second replay continued across five
  loop boundaries; the longer combined publishing/recording run also stayed audible.
  Decoder errors and end-of-file state are now exposed through the mixer API.
- Windows narrow command-line parsing corrupted non-ASCII show paths. The new
  show/discovery arguments now use the native Unicode Windows command line.
- Inherited UI gates constructed a mixer with a null core. That unsupported
  construction is now guarded, allowing the capture-only UI gates to run again.
- Added non-conflicting localized keyboard mnemonics for Appearance and layouts.
- The first installed workflow found that the installer omitted the new CLI/MCP
  directory. Packaging and uninstall rules now include it; the installed workflow
  above passed after the fix.
- Local API output startup bypasses the interactive firewall notice so a visible
  app cannot block an automation request on that modal dialog. Normal UI startup
  retains its notice; no firewall rules are changed by automation.

## Performance sample

During windowless browser video/audio plus recording, after the injected browser
crash recovered, a 6.60-second sample measured the native app and its descendants:

| Metric | Observation |
|---|---:|
| CPU, percent of one logical core | 76.74% |
| Sum of process private memory | 494.44–513.06 MiB |
| Local state-request p95 latency | 24.44 ms |

This short sample is a responsiveness check, not a long memory-leak soak or a
controlled performance comparison with 0.6.2. Normal automation is event-driven;
there is no additional background screenshot or API polling loop. Transparent
browser capture still has the documented snapshot cost and 15 fps maximum.

## Gates and limits

Release builds, Node syntax checks, whitespace checks and the inherited
`MainWindowTest`/`LocalControlServerTest` gates passed. These gates are separate
from the packaged workflows above.

Windowless mode still requires Windows graphics/device support in a user session;
it is not a tested Windows service or logged-out-desktop mode. Spanish and French
cover core controls; technical messages and some dialogs remain English. UIA and
keyboard behavior were exercised, but a full NVDA/JAWS/Narrator user audit remains
outside this validation. The optional automation tools require Node.js 22+.
