# Desktop Studio 0.9.4 â€” Windows

This patch corrects accumulating timing differences between the compositor and the independent recording/publishing clocks. Render deadlines now retain nanosecond precision and use the same frame interval as the output pacers. The previous mismatch accumulated approximately 28.5 ms over eight hours at 30 fps and 114 ms at 60 fps.

The [clock investigation and candidate checks](recording-clock-followup-2026-10-18.md) document the change and its limits. Short runs confirmed clean video/audio files at 30 and 60 fps. They do not establish that every long-session recording repeat is fixed.

Known remaining issues: some browser-overlay workflows deliver fewer distinct pictures than their encoded frame rate; WebView memory/handle growth requires investigation; one API screenshot omitted an overlay label. Long-duration confirmation of the clock fix remains outstanding.

Builds run locally with an explicit worker limit and a separate package directory. No GitHub Actions build is required. Release qualification is performed under shared host load because another Codex session may be installing or compiling; these runs are not controlled performance comparisons.

## Signed package qualification

Built from `aaf2139f610ef1038aa586a56432e1e32cf718c9`; subsequent qualification documentation does not change the packaged application.

| Artifact | SHA-256 |
|---|---|
| Application | `c365b5ef64901b553ca01fc3c428bdab42a85c7ae50a0816b731f51590dc9a4d` |
| Setup installer | `6bc518cabedac619bfb25ea693fd4481980f0ef85f6437e507c6a32fe7f8efc6` |
| Portable ZIP | `f4bafeb8d07476dc90e7ffcf0e69e8046bef7d0309736163f8deacf2d2724b8c` |

All 3,775 portable ZIP files match the frozen staging package; archive CRC checks passed. Stable download aliases are byte-identical to the versioned files. The complete FFmpeg bundle matches 0.9.3, so its source/license information is unchanged. Application and installer are timestamped using the same Social Stream Ninja certificate as 0.9.3. This host reports signature trust status `UnknownError`; this is not a claim of trusted-root or SmartScreen reputation. These are packaging gates, separate from actual application testing.

The signed portable application ran for 60.85 seconds with an opaque browser video source, captured browser audio, a transparent clock overlay, GPU composition, 720p30/3 Mbps private UDP publishing and independent recording. Both complete files decoded cleanly, with increasing timestamps, approximately 30 fps and a maximum 34 ms frame gap. Audio and flash/beep sync were confirmed in all six sampled windows. Recording queue drops remained 11 ? 11 with zero repeated frames; the initial counter includes encoder-startup replacements. The program screenshot was visually inspected. This does not establish perfect browser source-picture cadence.

A real 0.9.3 ? 0.9.4 per-user upgrade in an owned test directory preserved the saved show, source identity and portrait canvas. The installed executable matched the signed portable hash and reported `video-mixer-native-qt/0.9.4`. Fifteen successive screenshots changed, and a new recording decoded cleanly. Uninstall preserved user files and the test restored user preferences. An earlier attempt stopped at a driver assertion that expected a bare version string; its evidence is retained and the corrected retry passed without an application change.

These checks do not qualify a clean machine, GUI preference migration, native portrait source content or multi-hour reliability. Another Codex session was left running; process-tree CPU/GPU/memory and system-load observations are retained as shared-load evidence, not a controlled speed comparison.

[Full measurements and raw evidence (private research repository)](https://github.com/steveseguin/video-encoder-testing/tree/main/knowledge/2026-10-18-studio-0.9.4-release).
