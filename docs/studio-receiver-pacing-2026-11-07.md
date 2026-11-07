# Received-browser picture pacing

Work on 2026-11-07 UTC in `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`. The main checkout's existing application edits are preserved.

## Finding and implementation

Opaque browser pictures previously replaced a single latest-image slot. Windows/WebRTC arrivals can straddle a Studio render deadline: one picture is repeated and the next is skipped even when both fresh pictures reached Studio. An opt-in image trace separates Windows capture, worker delivery, parent-process receipt and actual recorder input. It samples fingerprints, saves no pixels/audio/URLs, writes a separate bounded file per process and does no image sampling when disabled.

In one simultaneous comparison against the same synthetic VDO.Ninja sender, the old receiver recorded **684 near-repeat candidates out of 3,620 transitions (18.90%)**; the buffered candidate recorded **6 out of 3,619 (0.17%)**. The aligned native trace found 3,530 fresh Windows images and 679 repeated recorder-input fingerprints on the old receiver. The candidate received 3,525 fresh images and repeated six recorder inputs. This identifies an internal timing issue without relying only on regular mux timestamps. Other baseline windows had seven and 21 candidates in two minutes; the rejected 60 fps capture experiment had 56. The worst comparison is not a universal baseline rate.

The implementation retains at most three opaque browser pictures. Sustained near-30-fps arrivals are presented in sequence with about 50 ms of headroom. A 60 fps canvas holds each 30 fps picture for two output slots. Slower or irregular pages retain arrival-based timing instead of being forced through a 30 fps sequence. The cadence observation keeps only 31 timestamps and uses hysteresis. Source failure/reconnect clears the picture queue and observation state. Browser audio receives a persistent 50 ms delay in addition to the existing user adjustment; this survives audio gaps during navigation or pause. Transparent sources retain their existing path.

This trades a small amount of latency and up to three retained pictures per opaque source for steadier presentation. It cannot create pictures that Windows or the remote stream did not deliver. The native trace is a sampled-fingerprint diagnostic, not proof of pixel completeness or a measurement of network latency.

## Artifacts and evidence

Evidence root `R` is `native-qt/build-integration/receiver-pacing/`. Runtime and SSN files are unchanged; packages share hard-linked runtime files, which must not be modified for corruption checks. Executables have the local timestamped Social Stream Ninja signature; Windows trust remains `UnknownError`.

| Package under `native-qt/dist/` | SHA-256 | Role |
| --- | --- | --- |
| `desktop-studio-polish3-nov7` | `a5e45745c0a73e83835d9a87faa33c6570eb0540808d6a8b9cf1739dace7ac4f` | Previous qualified baseline |
| `desktop-studio-receiver60-nov7` | `7379745366726f64eacf6dbb1ac16a46c421157226394b3a7f0395daf30854b4` | Rejected 60 fps capture experiment |
| `desktop-studio-receiver-trace-nov7` | `22773f51a56ef9f73e5764563241f4d4ae92d0d56de03d3400f583a07648adf4` | Baseline behavior with opt-in tracing |
| `desktop-studio-receiver-buffer-nov7` | `e4a4b04f86fae09fb79931eef03ead34ea342b6878d38a45e0c5e33298581160` | Initial buffer candidate; not final |
| `desktop-studio-receiver-final-nov7` | `7b9d8f7e6d22975c2b3a671e1ff1b2f755900b6abb245fb648d0c9437f4f9cba` | Final qualified portable |

Pointer input, keyboard input and Windows UI Automation drive the packaged applications. Saved media, screenshots and local-control reads verify the behavior. Compilation, syntax, whitespace and secret scanning are separate gates.

## Experiments retained

`baseline/`, `capture60/`, `trace30/`, `buffer30/`, `paired-baseline/` and `paired-buffer/` each contain a two-minute received recording, metrics and workflow results. Raising capture to 60 fps did not improve the result, so that change was reverted. The native capture remains capped at 30 fps.

`buffer30/` initially looked worse, but its trace shows only about 26 fresh incoming pictures per second, with the recorder retaining almost all of them. That run is not evidence that a buffer restores missing upstream frames. The later simultaneous comparison distinguishes this from the internal repeat/skip problem.

The initial buffer used one-time audio priming and forced slower clips through its 30 fps queue. Its 24 fps fixture (`slow24-before/`) had holds up to 133 ms. The final implementation uses persistent audio delay and arrival timing for slower sources. The final 24 fps steady sample has holds no longer than 66.7 ms; repeating some frames is expected when converting 24 to 30 fps. Actual Pause/Resume controls in the browser were exercised during recording.

The simultaneous comparison's whole-process-tree CPU means were **2.22% baseline / 2.48% candidate** on this 20-logical-processor PC, with private-memory means **634 / 642 MiB**. These include the receiver and its child processes. Real-time/browser conditions vary; the runs are practical comparisons, not controlled performance guarantees. Resource summaries are in `R/comparison-resources.json`.

The first final-sender motion check reported rawvideo muxer DTS warnings. Its independent full-file/timeline check passed: the motion helper was rounding millisecond input timestamps onto an output frame-rate grid. `studio-soak-motion.py` now preserves demuxer timestamp precision, as the existing full-file analyzer already does. The retained failed `final-sender/cadence/motion/` is separate from the successful `motion-demux/`. Both paired comparisons and both final sender/receiver files were re-analyzed with the correction; picture counts and near-repeat counts are unchanged.

## Final qualification

The final executable's five-minute receiver recording is **301.435 seconds / 9,043 frames**, with **two near-repeat candidates out of 9,042 transitions (0.022%)**, a longest near-static interval of 100 ms, no duplicate presentation timestamps and no video timestamp gap above 34 ms. Every picture and audio sample decodes. First/last-minute flash/pulse offsets stay within **32/34 ms**. Near-repeat analysis uses mean absolute luma difference at 96x54 and threshold 0.1; codec noise and the reference's movement limit what this heuristic can prove. The native tracing is disabled in this final qualification run.

The receiver averaged **2.31% CPU** across its process tree after the first ten recording samples, with **644 MiB** mean private memory and **653 MiB** peak. Other isolated QA work ran concurrently. The paired experiment is more useful for comparing CPU cost; the longer final run is a separate workload with anonymous live chat and a real featured comment on the sender.

| Final packaged workflow | Checks | Evidence under R |
| --- | ---: | --- |
| Ten-minute recording, anonymous chat, real featured comment, VDO publication and normal stop | 7 | `final-sender/cadence/` |
| Complete sender file, timestamps, duration and first/last-minute A/V | 10 | `final-sender/cadence/media/` |
| Five-minute separate receiver: real picture/audio, recording and finalization | 5 | `final-receiver/receive/` |
| Whole receiver file, timestamps, duration and first/last-minute A/V | 10 | `final-receiver/receive/media/` |
| Actual 24 fps browser playback, Pause/Resume and saved recording | 9 | `slow24-final/receive/` |
| Saved pause silence and A/V before/after resume | 4 | `slow24-final/receive/behavior-media/` |
| 60 fps canvas, browser-worker failure, automatic recovery and visible Reconnect | 10 | `recovery60/receive/` |
| Complete recovery recording and steady A/V before/after reconnect | 3 | `recovery60/receive/behavior-media/` |
| General UI, four window sizes, malformed input, privacy, keyboard access and restart | 77 | `regression-final/workflows-20261107-161359-747/` |
| Synthetic support events, private previews, three styles and gift streaks | 77 | `regression-final/support-20261107-161504-505/` |

Automatic browser-worker recovery took **5.19 seconds** from verified worker termination to fresh picture and pulse audio. Recording and source identity survived. The complete file has 60 fps timestamps with gaps no larger than 17 ms; the browser still supplies at most 30 fresh pictures per second. Steady A/V samples before/after reconnect stay within **33/13 ms**. The deliberate outage has missing picture/audio; a whole-file nearest-pulse comparison across that gap is not a meaningful steady-sync check. This is browser-worker failure, not whole-app crash or a full Internet outage.

The saved pause interval is silent, the paused Program picture stays unchanged, and picture/audio resume via the actual browser page buttons. Before/after-resume offsets stay within **53/57 ms**. The 24 fps reference's steady segment has no hold above 66.7 ms, versus 133 ms with the first candidate. Four derived FFV1 analysis clips were removed to reclaim 84.8 MiB of disk space; original recordings, extraction/probe logs and results remain, and `studio-receiver-media.py` regenerates the clips.

Visually reviewed the final receiver workspace and featured comment, the recording/error state after worker failure, and the compact 1024x768 chat layout. The recording control and recovery notice remain visible; the compact toolbar wraps. Gift checks use fixtures through the actual native/SSN rendering path, not real gift purchases or proof of every platform's extraction.

The sender completed **600.849 seconds / 18,025 frames** with anonymous chat, a held real comment and two additional media sources. The entire saved file decodes with **zero near-repeat candidates**, no duplicate presentation timestamps, no timestamp gap above 34 ms, and first/last-minute A/V offsets within **53/47 ms**. It averaged **4.26% CPU** across its process tree after startup, with **1,441 MiB** mean private memory. Streaming and recording stopped normally. Publication contained only the synthetic reference and public comments, not camera/microphone content; nothing was posted to any chat.

The owner's seven scenes, eight sources and four channels were restored on the qualified package, with production idle (four checks). The real saved Twitch account still exposes **Log Out**, and returning restores ready chat without changing channels or starting an output (four checks). No credentials or browser storage were read. Evidence is under `native-qt/build-integration/ui-exploration/20261106-085441/receiver-restored-20261107-161858/` and `saved-twitch-20261107-161900/`. Studio is left open and idle. The owned loopback fixture server and isolated QA apps are closed.

## Reproduction

Use `studio-cadence-workflow.ps1 -Live -Chat -BackgroundClips 2` to start an isolated packaged sender with the existing moving/pulse reference. Feed its `cadence/destination.json` to `studio-receiver-cadence-workflow.ps1 -Seconds 300`. The receiver tool also supports `-OutputFps 60 -ExerciseRecovery`. Use fresh output folders and avoid overlapping GUI interactions between workflows.

For slow-source playback, convert the reference to a 20-second 24 fps H.264 MP4 with bundled FFmpeg (`-t 20 -vf fps=24 -c:v libopenh264 -b:v 2000k -c:a copy`). `studio-browser-playback-fixture.py --file CLIP --destination NEW_JSON` serves only that clip and its controls on loopback; pass the destination to the receiver workflow with `-ExercisePlayback`. The recorded run used the same fixture body in an ignored local launcher; the reusable launcher adds explicit path arguments and range validation.

For synthetic-image diagnostics only, set `VIDEO_MIXER_IMAGE_TRACE` to a fresh path prefix before launching the owned receiver. After it closes normally, run `studio-image-trace.py --prefix PREFIX --out NEW_JSON`. Analyze completed recordings with `studio-soak-media.py`, `studio-soak-motion.py --threshold 0.1`, and `studio-receiver-media.py` for deliberate disruptions. These tools inspect saved application output; they are distinct from build/syntax gates.

No two-hour soak, new installer qualification, Meshcast validation or universal browser/network smoothness is claimed. Chat memory remains unresolved; this pass targets received-browser presentation. Previous local-file and chat work is documented in [the playback report](studio-playback-polish-2026-11-07.md).
