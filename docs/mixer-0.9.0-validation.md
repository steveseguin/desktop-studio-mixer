# Desktop Studio 0.9.0: projects, recording and isolation

Subsequent [browser-audio and recovery testing](mixer-0.9.0-followup-validation.md)
found and fixed stale audio when pages replace live tracks or switch back to file
playback. That report identifies the newer packaged executable. Results and
hashes below describe the original development pass.

This development pass addresses remaining production software in the Windows
application. Testing means packaged app workflows and decoded media. Build,
syntax, schema and whitespace checks are separate gates. Private recordings,
screenshots, invitations and logs remain in ignored
`native-qt/e2e/reports/studio-090/` directories.

## User-facing changes

- Up to 16 named source audio tracks alongside Program in its MKV, and four
  separate source video MKVs. Selection is available under **Recordings >
  Recording tracks** and through `record.start`. Files retain their track
  identity when sources are removed. Separate tracks stay available off-screen,
  but obey privacy and mute controls; the README specifies their routing.
- A seek slider, including silent video cueing while paused, keyboard control,
  and a last-frame fallback when a requested cue exceeds the last video sample.
- Active **Open** and **Save As**, a rolling previous-valid backup, asynchronous
  collection of local assets into a portable show, and exact-filename relinking.
  Project locks follow Open/Save As; snapshot export also respects another
  Studio instance's active lock. Collection never overwrites an existing folder.
- SRT caller and listener selection, including encrypted output, late receivers,
  reconnects and cancellation. Rendezvous is explicitly unavailable in this
  integration; FFmpeg's current SRT path does not provide the separate local and
  remote addressing needed here. See the [FFmpeg backend source](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/libsrt.c).
- A combined browser CPU/memory budget. Per-source CPU allowances rebalance with
  headroom so hostile sources cannot spend the parent's complete scheduling
  interval and halt a healthy sibling. Windows nested CPU rates are relative to
  their parent allowance, as described in [Microsoft's CPU-rate documentation](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_cpu_rate_control_information).
- Independent Spout and virtual-camera producer workers, asynchronous isolated
  camera registration, bounded pending frames, crash/hang recovery and a restart
  limit. Static pictures reuse their copied pixels while retaining fresh output
  timestamps. `local.set` controls these outputs in a windowless Windows session.

The API schema also adds `show.saveAs`, `show.collect`, `show.relink`,
`show.open.activate`, `media.set.seekMs`, recording selections and resource/state
details. The MCP server discovers the commands from the same schema.

## Actual workflow evidence

Tests use the existing Windows workstation, Intel Core Ultra 7 265K (20 logical
processors), available NVIDIA hardware encoding, WebView2 Evergreen and the
Logitech C920 where specified. Other QA/build activity overlaps some observations;
CPU percentages below use **100% = one logical processor** and are not isolated
comparative benchmarks.

| Workflow | Observed result |
|---|---|
| Maximum combined recording capacity | Program plus 16 source audio tracks and four separate source videos recorded together for 30 seconds on candidate J. Every file/stream fully decoded and each separate audio track contained its assigned tone. |
| Source recording isolation | Four source videos continued after the Program FFmpeg writer was killed. Conversely, killing one source writer left Program running. Surviving files decoded cleanly. |
| Recording privacy/removal | Mute, On air and Private break produced silence where expected; off-screen sources retained their separate pictures/audio; removal left the selected source file silent and black. |
| Visible project/recording controls | Actual mouse/keyboard actions sought a paused clip, saved an active show, generated a backup, collected assets, selected recording tracks and exported a multi-audio MP4. |
| Portable projects | Collected assets matched the originals and played after the original was moved. Missing-asset collection left no final folder. Relinking restored playback. |
| Project concurrency | Two running Studio processes rejected writes/activation into each other's active shows. Successful Save As transferred the lock; invalid Open retained it; quitting released it. |
| Collection shutdown | Quit during a 2 GiB copy completed in 0.219 seconds, leaving neither a partial destination nor its temporary folder. |
| SRT | Encrypted caller and listener captures decoded. A receiver joining 18 seconds late and leaving/rejoining succeeded. Unconnected caller cancellation took 0.953 seconds while recording continued. |
| Browser overload | One busy page used 3.69 cores within its four-core cap. Three hostile pages together used 5.58 cores under the shared eight-core cap while a healthy video source kept its worker. Maximum nearly identical output-frame run was seven, versus 20–22 before the fairness fix. |
| Browser failures/security | Hardware H.264 decoding reported `D3D11VideoDecoder`; renderer hangs, GPU-process failure, restart exhaustion/manual recovery and approximately 4 GiB page memory exhaustion preserved the healthy source and recording. Recorded browser audio remained present. The existing browser security GUI workflow passed. |
| Guest panels | Four, six and eight real private WebRTC guests received video and other callers' audio while their own tone was excluded. The eight-picture grid and recording through Private break, Backstage/readmit and a departing peer passed. |
| Network pressure | RTMP recovered after bandwidth starvation; the independent recording decoded with tone throughout. An untrusted RTMPS fixture was rejected without changing Windows certificate trust. |
| Output interfaces | The RTMP/WHIP GUI workflows produced receiver captures with decoded video and audio. |
| Local-output recovery | Spout and camera producer termination recovered in about 2.1 seconds; whole-worker suspension recovered in 3.2 seconds. Suspending only the native camera delivery thread recovered in 12.42 seconds while the worker's control loop was still responsive. The receiving Edge camera stream remained open. A fourth failure paused the camera, and toggling it restored output while Spout and recording survived. |
| Headless local output | Portrait 720x1280 Spout reception and a 121-frame virtual-camera capture decoded from an app with no visible window. Conflicting registration requests were rejected before changes. |
| Packaged automation and accessibility | The final portable package exposed 40 MCP tools. Typed recording arrays started named audio/source video recordings that fully decoded; invalid types and duplicate selections were rejected. RTMP/SRT/WHIP and independent recording decoded. Spanish menus, keyboard scene selection, per-track accessible labels, themes and layout controls passed in the visible window. |
| Final SRT/security controls | Selecting listener in the visible output settings produced encrypted 720p video and audio while recording independently. WHIP authentication failure, excessive response size and cross-origin redirect cases were exercised; bearer credentials were not forwarded across origins or placed in worker command lines. |
| Installer lifecycle | Installed into a private QA directory, ran real CLI/MCP and portrait recording, captured 12 distinct live screenshots, decoded the recording, and uninstalled. Maximum recording frame interval while taking screenshots was 48 ms. Portable, ZIP and installed executable hashes matched; no owned worker descendants remained. |
| One-hour production rehearsal | Candidate C ran 1080p30 camera/microphone, private VDO.Ninja video, transparent chat/flash/tone, scene changes, independent MKV and NVENC RTMP for 3,600 seconds without injected faults. The entire 3.29 GB recording and start/end receiver captures decoded without errors. The recording drop counter remained 17 from the first sample through the last. |

The new maximum-track test initially failed: waiting 1 ms for FFmpeg progress on
every packet could not sustain 1,700 PCM packets/second. The corrected worker
pumps progress without that wait and still sleeps when its bounded input queue is
empty. Queue limits were retained. The original browser aggregate cap also failed
the video-cadence check twice; per-source quota balancing fixed the observed stalls.
End-of-clip paused seeking and rapid keyboard seeking each exposed and received
their own fixes. These failed recordings/reports remain available locally.
The expanded MCP workflow also caught the schema converter treating the new
recording arrays as strings. Array conversion was corrected and the real
packaged MCP recording workflow then passed.

Timing cases delivered approximately 30/60 output frames per second across the
landscape and portrait presets. Local-media p95 absolute flash/tone offsets were
at most approximately 21 ms in this run. The transparent overlay's corresponding
measurement reached 109 ms; it remains a 15 fps snapshot source. This is not a
claim of universal physical camera/microphone or distant-guest lip sync.

The one-hour rehearsal produced 108,415 decoded frames at 29.976 fps, including
receiver startup/finalization time. There were 84 frame intervals above 50 ms;
the maximum was 164 ms. The stable recording drop counter does not count every
scheduling gap. Camera reception stayed near 29.95 fps. The two transparent
snapshot browser sources had median reception rates of 11.95 fps (VDO.Ninja)
and 6.47 fps (overlay); neither stopped advancing in any of the 357 sampled
intervals. A 30 fps Program therefore does not imply 30 distinct browser pictures
per second. These measurements retain the current snapshot-capture limitation.

The overlay's median video-minus-audio offset was -46.7 ms near the beginning
and -15.0 ms near the end, a 31.7 ms change over the hour. This met the rehearsal's
100 ms drift limit and does not replace physical camera/remote-guest sync checks.
During the last 20 minutes, median total process-tree CPU was 1.51 logical cores
(p95 1.76), with the main app using 0.285 cores and 75.63 MiB. Median total private
memory was 1,456 MiB and grew approximately 1.32 MiB/minute; WebView2 accounted
for about 1.25 MiB/minute. The main app's memory was flat. Browser memory growth
remains unresolved; this run does not establish leak-free operation.

In sequential 12-second static-picture samples with both local outputs enabled,
the app plus its two output workers fell from 20.44% to 10.14% of one logical
processor after copied-pixel reuse. The consumer decoded 241 frames and followed
a red-to-green change. Private memory increased from 89.63 to 97.93 MiB because
workers retain the last picture. Moving-video samples did not show a speedup
(31.77% versus 40.10%, including the media worker); scheduler placement and other
work were uncontrolled. This is a measured static-output improvement, not an
across-the-board performance claim.

## Build scope and closeout

Candidate J SHA-256:
`ab185f5d76a551fd991b0c36ad5436b94a7a80842190724efcad41aa99e0e5db`.
The final portable executable matches J, including local-output pixel reuse and
native-thread delivery monitoring. Its MCP JavaScript additionally contains the
array-conversion fix tested in `automation-final`. Earlier targeted results span
candidates A–I; broad timing, RTMP/WHIP, browser security and the network-pressure
run used G before the local-output additions. Guest panels used D; browser
overload and the long rehearsal used C. The maximum-track fix first passed in D;
four-source recording and project-lock protection passed in E. GUI project
controls passed in C, G after the keyboard-cue fix, and J. The final executable
also passed combined maximum recording capacity and native-thread hang recovery.

The final portable, installer lifecycle, one-hour rehearsal and media analysis
passed the workflows above. The long rehearsal intentionally used candidate C;
it is not an hour-long qualification of the later recording/project/local-output
changes. Those have their separately identified workflows. Final documentation
is repackaged with the same tested runtime files, verified by SHA-256 manifests.
The original Game Capture checkout and saved user show were unchanged. Saved
registry preferences were restored byte-for-byte and owned app processes exited.

## Remaining engineering

- Multiple Program buses, simultaneous independent landscape/portrait output and
  direct multiple publishing destinations; arbitrary physical multichannel input
  selection. Current channel mapping starts from stereo input.
- SRT rendezvous support with suitable local/remote address configuration.
- Playlists, frame-accurate cueing, gapless AAC looping and a complete source-clock
  scheduler/video delay for sources whose audio arrives late.
- Faster transparent browser capture, shared guest-return video encoding and
  fewer CPU frame readbacks. Each guest
  return currently has its own encoder; ISO files also add encoder/disk load.
- Broader color/HDR qualification. The eight-guest grid still showed codec/color
  channel differences; its placement check does not establish faithful color.
- Audio fallback for protected, cross-origin-without-CORS and unusual browser
  media graphs; longer memory profiling across runtime versions and page types.
- Broader localization and accessibility coverage. Main-process Qt image loading,
  composition and inherited non-Studio native paths remain outside the new worker
  boundaries. Process isolation cannot contain OS/kernel/GPU-driver failures.

External checks remain: real phone rotation/backgrounding/cellular handoff;
distant guests and physical clap/lip-sync; authenticated provider/chat accounts;
other GPUs, multichannel interfaces and Bluetooth devices; removable/network
drive disconnection; Zoom/Teams/Discord consumers; coordinated lock/sleep/RDP
transitions; and the clean-machine/Windows 10 installation deferred by the user.

## Reproduction

Run with the existing desktop QA Python from `native-qt`, selecting a packaged
directory and a new private report folder. Relevant scripts include
`mixer-project-media-e2e.py`, `mixer-project-locks-e2e.py`,
`mixer-collection-shutdown-e2e.py`, `mixer-separate-recordings-e2e.py`,
`mixer-recording-capacity-e2e.py`, `mixer-recording-isolation-e2e.py`,
`mixer-srt-modes-e2e.py`, `mixer-local-api-e2e.py` and
`mixer-local-performance-e2e.py`.

Use `mixer-ui-workflows-e2e.py` for `project-recording-ui`, `local-isolation`,
`srt-listener-ui`, `timing`, `protocols-050` and `browser-security`. Do not run visible workflows
concurrently; `mixer-automation-e2e.py` also opens a GUI during its last phase.
Local-output tests must run one at a time because their named receivers are
shared within the Windows session. The GUI runner leaves failed owned windows
open for inspection; close those explicitly before starting another workflow.
