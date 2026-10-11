# Video Mixer 0.3.1 performance review

Reviewed and exercised on 2026-10-11. Scope: the Studio compositor, capture handoff,
browser capture, audio mixing, camera controls, and their output integration.
The inherited transport/codec stack was not exhaustively audited.

## Findings and changes

| Priority | Finding | Result |
| --- | --- | --- |
| P2 | Studio composited and repainted both 1280x720 canvases every 33 ms, even for unchanged title cards or identical Preview/Program scenes. | Fixed: three bounded composition caches, keyed by input image identity, geometry, opacity, order and background. Identical scenes share the resulting image. Canvases repaint only when their image or selection changes. Hidden Preview skips composition. Output still receives regularly timestamped frames, including static scenes. |
| P2 | Every camera/window/browser capture buffer was copied again into a QImage. | Fixed: the immutable QImage owns the moved capture buffer until its last reference is released. Qt detaches the image if a consumer writes to it. |
| P2 | Audio-only scene layers intercepted clicks and drags on visible video. A press on empty canvas could also move the previously selected layer using an old drag origin. | Fixed and exercised in the packaged app: hit selection skips audio-only/hidden/zero-opacity layers, and movement requires a successful initial hit. |
| P3 | The audio worker processed 960 silent samples per video-only source every 10 ms, and performed delay-deque work even for zero delay. | Fixed: an atomic audio-active flag skips these sources, zero-delay channels bypass the delay deque, and Listen is sampled once per chunk. Actual audio sources still drain while muted/off air. |

Code: [compositor and audio mixer](../native-qt/src/mixer/mixer_widget.cpp),
[capture buffer ownership](../native-qt/include/versus/mixer/frame_image.h),
[browser source](../native-qt/src/mixer/browser_source.cpp).

## Measurements

Release portable executables, same Windows 11 machine, Intel Core Ultra 7 265K
(20 logical processors), Logitech HD Pro Webcam C920 at 1280x720/30.
Each scenario had a five-second warm-up followed by approximately 30 seconds of
process/thread CPU sampling. The Studio window remained visible. Browser fixtures
were local: an animated canvas with a 731 Hz tone and a transparent chat overlay.
Recording added the C920 microphone and wrote to a local MKV.

CPU below uses **one logical core = 100%**. Divide by 20 for this machine's
Task Manager-style percentage. Tree totals include owned WebView2 and output
processes; the Python driver is excluded.

| Workload | App CPU before | App CPU after | Tree CPU before | Tree CPU after |
| --- | ---: | ---: | ---: | ---: |
| Static title | 16.53% | 1.09% | 16.53% | 1.09% |
| C920 camera | 25.20% | 10.99% | 25.20% | 10.99% |
| Camera + animated browser | 39.69% | 21.20% | 46.39% | 35.64% |
| Camera + browser + transparent overlay | 47.02% | 27.69% | 74.48% | 61.48% |
| Mixed recording, including microphone | 85.53% | 65.29% | 114.93% | 103.25% |
| Break scene with sources still running off air | 46.60% | 19.42% | 74.46% | 46.29% |

These are single-run before/after workload measurements, not isolated attribution
to each optimization or a statistical benchmark. Browser subprocess load varies.
The largest measured improvement is removal of repeated static/duplicate drawing.

The camera's ending one-second health sample was 29.3 fps before and 29.4 fps after.
The opaque browser was 20.5 versus 21.5 fps; the transparent overlay was 11.8 versus
12.7 fps in the camera/browser/overlay scenario. These are received source frames,
not a claim that every 30 fps output frame contains a new browser image.

Sum of process working sets for the mixed recording was 1114.8 MiB before and
1114.0 MiB after. This sum counts shared pages in each process and is not unique
physical memory consumption. The short samples do not establish long-run leak
freedom. Native UI message round-trip p95 during recording was 43.2 versus 43.5 ms;
no meaningful improvement in that measure was demonstrated.

Windows Performance Recorder refused CPU stack collection under the current
system performance policy (`0xc5585011`). No settings were changed and no WPR
session remains active. This report uses process/thread counters, code inspection,
output decoding, and separate application frame timestamps; it does not claim
sampled function-stack or GPU utilization measurements.

Baseline executable SHA256:
`e6dfe1b502c25340bd2473bc30c37018d3ca43a75f45a9e303b1f989272e85be`

Profiled 0.3.1 executable SHA256:
`2f8e4bb9977fa5cdb68d4979e676591804165135c796fa5f6c41e42af1732727`

## End-to-end evidence

- Both mixed recordings decoded to H.264 1280x720/30 with AAC 48 kHz stereo.
  The optimized recording contained 1162 decoded frames and 38.72 seconds of audio;
  its measured 731 Hz tone amplitude was 0.1132 and its final frame was the Break
  scene. More than 99% of adjacent frames changed in the sampled portion.
- Packaged-app scene workflows passed: geometry edits affect Preview until Take,
  layer reorder invalidates the cache, source replacement updates its image, and
  Fade reaches the correct cached destination scene.
- Clicking/dragging visible video beneath a microphone layer selects and moves the
  video. Dragging empty canvas leaves the previously selected layer in place.
- The browser/record/mute/scene-switch workflow passed. Decoded audio RMS went
  from 0.1198 to 0 after mute, with the correct browser and Break video frames.
- C920 controls passed again in the optimized portable: brightness and zoom
  changed the actual picture; exposure/focus/white-balance manual and Auto modes
  worked; 1080p30, 720p30 and 480p15 capture modes worked during recording.
  Reconnect and full application restart restored the saved controls and 480p15
  mode. The output decoded as H.264 720p30. Original driver controls were restored.

Raw reports and physical camera media stay in the ignored local directory
`native-qt/e2e/reports/mixer-performance/`. They are not included in the repository
or release archives. Build and diff checks are gates, separate from the app
workflows above.

## Remaining review items

1. **P2: camera lifecycle operations still block the GUI thread.**
   `MixerWidget::Impl::startSource` calls `CameraCapture::startCapture`
   synchronously. That function waits up to ten seconds for initialization, and
   shutdown joins the device thread. A slow/blocked driver can therefore delay
   Studio composition and repeat an old output frame. Moving source lifecycle
   operations off the GUI thread requires a separate ownership/cancellation
   change; this performance patch does not solve it. A separate frame-timestamp
   run during the three C920 mode changes recorded gaps of **878, 951 and 994 ms**
   between Program submissions, versus a 33.3 ms median and 35.5 ms p95 across the
   run. Avoid changing camera capture mode during a live production until this
   lifecycle issue is addressed.
2. **P2: transparent browser capture remains costly and below video cadence.**
   `BrowserSource::capture` uses WebView2 PNG snapshots, followed by decoding on
   the GUI thread. Off-air browser sources keep running to support immediate
   scene takes and Listen. GPU composition/capture and an explicit suspend policy
   for unused sources are the next performance work; silently stopping off-air
   sources would change existing monitoring behavior.
3. **P3: driver-initiated camera format changes leave the settings snapshot stale.**
   The `MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED` handler updates frame dimensions
   and stride, but not `settings_.activeFormat`. The displayed active mode can be
   wrong if a driver renegotiates without a user-requested restart. Identified by
   inspection; this hardware did not reproduce an unsolicited renegotiation.

The per-browser audio-session mute timer also repeats process/session enumeration
for each source. Its cost was not isolated here, so it is a follow-up profiling
candidate rather than a demonstrated bottleneck.

## Reproduce

Use a disposable standard Opening/Main/Break show, connect the C920, launch the
portable application, then run from `native-qt` with the desktop requirements and
`mixer-requirements.txt` installed:

```powershell
python -B e2e/mixer-performance-e2e.py --pid <app-pid> --report e2e/reports/mixer-performance/run
python -B e2e/mixer-compositor-e2e.py --pid <app-pid> --report e2e/reports/mixer-performance/compositor
```

The performance workflow records the camera/microphone locally. Back up personal
shows before using this QA workflow. No public streaming destination is used.
