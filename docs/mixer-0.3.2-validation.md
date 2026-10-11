# Audio meters and scroll panels — 0.3.2

Every Studio audio track and the master bus now have a full-width visual level
meter with a -60 to 0 dBFS scale. Green, amber and red segments show increasing
level, a white marker holds peaks for one second, and CLIP remains visible for
two seconds after a track reaches full scale. These display post-volume/mute
peak levels; the master reading follows the existing output limiter. Audio
routing and the limiter are unchanged.

Scrollable audio tracks, camera controls and output settings use a contrasting
panel background and frame styling. The track list reserves enough height to
show one complete channel, including its meter and controls. Scene/source lists
retain their distinct backgrounds. Labels use the surrounding panel background.

Packaged-app workflows on 2026-10-11, using the C920 and a private local browser:

- A quiet tone registered visibly at -34 dBFS. Louder tones registered at -8 and
  -2 dBFS with the expected amber and red segments.
- Peak hold remained visible after the signal fell. Raising track gain into
  overload showed CLIP. Muting returned both track and master readings to silence.
- Browser and physical microphone tracks both had working meters. Scrolling to
  the microphone retained a clearly distinguishable viewport boundary.
- The camera controls and output settings scroll surfaces were checked in the
  running app and visually inspected. Camera settings were not changed by this
  appearance workflow.
- The final portable also passed browser capture, Preview/Program Take and Fade,
  recording, mute and scene-switch workflows. The decoded 15.36-second recording
  contained the expected browser and Break images; audio RMS changed from
  0.1200 to 0 after mute.

Validated portable executable SHA256:
`ea100fc63e04649ad813a460cf18112d1f8bf73991bd8241985c21513c89989a`

A 30-second idle sample with the final meter layout used 0.36% of one logical
core on the same 20-core machine. This is a short regression check; the full
six-workload comparison remains the 0.3.1 performance review.

The workflow is `native-qt/e2e/mixer-meters-e2e.py`. Local screenshots and reports
are ignored under `native-qt/e2e/reports/mixer-meters/`, including camera imagery.
Build/syntax/archive checks are gates; they are separate from the application
workflows above. Installer packaging uses the same executable; installation was
not separately requalified for this appearance update.

The [performance review](mixer-performance-review.md) remains applicable: camera
capture-mode changes can pause Program updates for about one second, and
transparent browser snapshots remain a performance limit.
