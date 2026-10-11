# Desktop Studio 0.4.3: source continuity and usable smaller windows

Validated on Windows x64, 2026-10-11. Application workflows use the packaged
Release executable; browser fixtures and the SRT receiver use private loopback
addresses. Screenshots, recordings and device identifiers stay in ignored local
reports under `native-qt/e2e/reports/studio-improvements-043/`.

## Fixes and reproduced failures

- Saving an unchanged browser source, renaming it, or changing audio delay
  previously restarted capture. In 0.4.2, the fixture loaded four times instead
  of once and its recording had a longest quiet interval of 310 ms. These edits
  now preserve the session. Device, URL, content and transparency changes still
  restart capture, as does explicit Reconnect, including window sources.
- An unavailable saved camera silently selected the connected C920 when Edit
  source opened. Missing cameras, microphones and other device selections now
  retain their saved identity and display an unavailable entry. A user must
  select a replacement before reconnecting an unavailable window.
- Saving a show discarded the alpha component of source colors and scene
  backgrounds. Both now serialize as ARGB colors, compatible with existing show
  loading. Opaque colors remain opaque.
- At 1366x768, the old workspace compressed buttons and overlapped track/master
  controls. The workspace now scrolls with a distinct background; audio controls
  retain minimum width and enough height for their labels. Smaller windows may
  require scrolling between the canvas and lower controls.

## Packaged application results

Executable SHA-256:
`d1cc9187add6cfdeb0f19ac2777ddd08e8f28a4cc88075fefb46c311826d62cb`.

- **Live source edits:** unchanged save, rename and audio-delay changes leave the
  browser document-load count at one. The decoded recording is audible in
  99.1% of sampled 10 ms blocks, excluding its first/last second. Its longest
  quiet interval is 80 ms, the deliberately requested new audio delay. Changing
  the URL produces the replacement picture; explicit Reconnect reloads it.
- **Show files:** real Open show/Save show dialogs preserve unavailable camera
  and microphone identities through renaming, preserve translucent colors, and
  reopen the saved composition with matching sampled pixels. An invalid source
  reference is rejected without replacing the current show.
- **Laptop layout:** both single canvas and Studio mode at 1366x768 retain full
  58-pixel master meters, at least 170 pixels for the track scroll area, readable
  buttons, and nonoverlapping audio controls. Scrolling reaches top output
  controls and bottom layer controls. Screenshots verify a distinct scroll
  surface.

- **Direct editing and recording:** transparency changes, dragging/resizing,
  visibility, live source removal, clean Program output, Studio isolation and
  both layout preferences surviving process restarts passed. The recording
  decoded to 797 frames with the expected visible and audible/quiet phases.
- **Production:** C920 camera/microphone, native window capture and explicit
  window reconnect, transparent chat, browser reconnect/capture-mode changes,
  headphone monitoring/mute and off-air listening, and desktop audio excluding
  the monitor all passed. A private SRT receiver captured H.264/AAC with the
  expected 731 Hz tone. This workflow passed on both baseline and final builds.
- **Audio meters and scroll surfaces:** quiet (-34 dBFS), amber (-8 dBFS), red
  (-2 dBFS), peak hold, clipping and track/master mute passed. The audio tracks,
  camera controls and output-settings scroll surfaces remain distinct.
- **Physical camera controls:** C920 brightness, zoom, exposure, focus and white
  balance, including supported Auto modes and reset, passed. Actual capture
  modes changed to 1920x1080/30, 1280x720/30 and 640x480/15. Requested mode and
  controls survived reconnect and a real application restart. The recording
  decoded as H.264 1280x720/30. Original driver-control values were restored.
- **Browser frame delivery:** a follow-up sequence measured 20.5, 21.5, 21.5,
  20.6 and 21.5 fps (median 21.5), consistent with the earlier browser capture
  measurements. The production check now samples five consecutive intervals
  rather than deciding from a single one-second startup reading.

No new steady-state CPU reduction is claimed. This change removes unnecessary
capture restarts; the 0.4.1 media-path optimizations remain in place. Camera
capture-mode changes still briefly restart that camera.

## Reproduction

Start the portable app with a disposable starter show, then run the scripts
from `native-qt`, supplying `--pid <app-pid> --report <private-directory>`:

- `e2e/mixer-edit-e2e.py`
- `e2e/mixer-show-e2e.py` (replaces the current show with private fixtures)
- `e2e/mixer-layout-e2e.py`

The shared desktop helper scrolls controls into view before pointer and image
checks. Layout assertions opt out while checking viewport clipping explicitly.
Build, syntax and archive checks are gates, separate from these application
workflows.

The original show was restored byte-for-byte and the original application
settings were restored after the workflows. The original Game Capture
repository is unchanged. Setup installation itself was not repeated for 0.4.3;
the rename/upgrade installer behavior was exercised in 0.4.2.
