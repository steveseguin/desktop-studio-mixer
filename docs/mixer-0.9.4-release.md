# Desktop Studio 0.9.4 — Windows

This patch corrects accumulating timing differences between the compositor and the independent recording/publishing clocks. Render deadlines now retain nanosecond precision and use the same frame interval as the output pacers. The previous mismatch accumulated approximately 28.5 ms over eight hours at 30 fps and 114 ms at 60 fps.

The [clock investigation and candidate checks](recording-clock-followup-2026-10-18.md) document the change and its limits. Short runs confirmed clean video/audio files at 30 and 60 fps. They do not establish that every long-session recording repeat is fixed.

Known remaining issues: some browser-overlay workflows deliver fewer distinct pictures than their encoded frame rate; WebView memory/handle growth requires investigation; one API screenshot omitted an overlay label. Long-duration confirmation of the clock fix remains outstanding.

Builds run locally with an explicit worker limit and a separate package directory. No GitHub Actions build is required. Release qualification is performed under shared host load because another Codex session may be installing or compiling; these runs are not controlled performance comparisons.
