# File playback and held-comment performance

Work performed on 2026-11-07 UTC in `C:\Users\Steve\code\video-mixer-chat-integration`, branch `feature/studio-chat-integration`. Main's existing application edits are preserved.

## Change and artifact

Local media now carries presentation timestamps across its worker boundary. The compositor selects due pictures from a bounded queue with 50 ms of scheduling headroom, and primes file audio against the same clock. Live camera/microphone/browser capture retains its existing latency. Pause, seek and reconnect clear queued pictures and audio. High-rate media is reduced to one queued picture per output interval, with at most six pictures retained per file source.

A held plain-text chat card now retains its rendered image until a new comment, style or gift-update token arrives. Previously it encoded and decoded a full PNG fifteen times per second. The card waits for fonts before the initial capture. The overlay acknowledgement also stops repeatedly parsing the author after acknowledgement. The hidden host no longer maintains unused DOM/debug copies of each incoming payload; its diagnostic feed remains bounded to 100 normalized comments.

Final portable: `native-qt/dist/desktop-studio-polish3-nov7/desktop-studio.exe`, SHA-256 `a5e45745c0a73e83835d9a87faa33c6570eb0540808d6a8b9cf1739dace7ac4f`. The local Social Stream Ninja signature is timestamped; Windows trust remains `UnknownError`. Runtime and SSN 3.50.17 are unchanged. Runtime hard links must not be modified for corruption checks.

## Packaged workflows

Evidence root `P` is `native-qt/build-integration/polish-nov7/`. Pointer, keyboard and Windows UI Automation drive the packaged application. Local control reads verify state; saved media and visual inspection verify output. Compilation and syntax/whitespace checks are separate gates.

| Workflow | Checks | Evidence relative to P |
| --- | ---: | --- |
| Fifteen-minute recording, anonymous live chat, real featured comment, VDO publication and normal stop | 7 | `final-live/cadence/` |
| Complete saved recording, packet cadence and first/last-minute A/V | 10 | `final-live/cadence/media/` |
| General navigation, four window sizes, malformed inputs, private selection, scenes, keyboard access and reopening | 77 | `regression3/workflows-20261107-144548-354/` |
| Support events, three styles, literal metadata, saved selection and gift streak updates | 77 | `regression3/support-20261107-144652-548/` |
| Separate VDO.Ninja receiver renders picture/audio and records ninety seconds | 5 | `receiver/receive/` |
| Pause, paused seek, resume, seek near the end, loop, reconnect, scene changes and finalization | 13 | `transport/transport/` |
| Saved transport recording: full decode, pause/slate silence, four A/V samples | 8 | `transport/transport/media2/` |
| 120 fps input recorded at 30 fps through three loops | 3 | `high-rate30/cadence/` |
| Video settings select 60 fps; 120 fps input records at 60 fps through three loops | 4 | `high-rate60/cadence/` |
| BRIO camera/settings/reconnect, physical microphone, mute/unmute, recording and playback | 13 | `hardware/hardware/` |
| Saved hardware recording: full decode, audio/mute/unmute and extracted pictures | 6 | `hardware/hardware/media/` |
| Recording writer killed, visible error/recovery, preserved original and next recording | 10 | `writer/writer-failure/` |

Support events are synthetic fixtures through the real SSN engine, native panel and Program renderer. They verify presentation and explicit featuring, not platform gift extraction or purchases. Anonymous public chat was read; no messages were posted. VDO.Ninja publication used a random unlisted ID and synthetic picture/audio only.

The receiver file fully decodes, has no video timestamp gap above 34 ms, and its measured flash/pulse offset stays within 69 ms. Its browser/network path still has 39 near-static transitions out of 2,692 (1.45%), each a two-frame interval of 66.7 ms. This change does not eliminate received-browser cadence issues. The transport recording is silent during pause and Break; steady samples before/after resume, reconnect and returning to the media scene stay within 50 ms. The 120-to-30 fps recording fully decodes, stays within 62 ms, and has zero near-static transitions at the stated threshold.

The 120-to-60 fps recording also fully decodes, has no timestamp gap above 17 ms, stays within 23 ms and has zero near-static transitions. These high-rate runs each last approximately 61 seconds; they do not qualify every codec, variable-frame-rate file or prolonged 4K workload.

The real BRIO recording contains the available room picture and room sound. Saved audio maxima were -49.0 dB before mute, -91.0 dB while muted and -53.2 dB after unmute. Camera picture was visually inspected. No physical device content was published. Writer-failure recovery preserves the interrupted original's hash and produces a separate MP4; the recovered file and following recording were decoded independently.

Visually reviewed the 1024x768 workspace, gift streak selection/private preview, paused-seek controls and received Program picture. The toolbar wraps, platform/value cues remain readable, paused seeking shows the requested position, and the real featured comment reaches the receiver. The compact layout still uses scrolling in the source/audio sections at small sizes.

## Comparison and limits

Baseline is the prior portable `3627893f...`. An isolated three-minute file recording found one near-static transition on both the baseline and initial cadence candidate, so that simple scenario did not demonstrate an improvement. The busier baseline adds anonymous Twitch/YouTube chat, a held comment, unlisted VDO output and two other media sources. Its four-minute recording contains 65 near-static transitions out of 7,219 (0.90%), with recorder counters also reporting 65 repeats. CPU averaged 5.07% after startup on this 20-logical-processor PC.

Motion comparison uses mean absolute luma change at 96x54 with threshold 0.1. Lossy encoding makes these near-repeat candidates, not a proof that every unique source frame was delivered. Regular mux timestamps alone are insufficient. Background activity and real chat traffic vary; this is a practical comparison, not a controlled causal benchmark.

The final busy recording is 900.992 seconds long: all 27,030 pictures and the full audio decode without errors. It has **zero near-static transitions** at threshold 0.1, zero duplicate presentation timestamps, a maximum packet gap of 34 ms and measured first/last-minute A/V offsets within 46/43 ms. Recorder counters show zero repeats. The dropped-submission counter moves from zero to eleven during the first second while the encoder starts, then remains unchanged for the entire run; these counters are not an end-to-end source-frame delivery percentage. The real featured comment remains on screen, both YouTube and Twitch report reading comments, and publication stops normally. The observed Show-to-composed time was 229 ms, versus 240 ms in the baseline run; these are individual observations, not latency percentiles.

After excluding the first ten active samples, CPU averages **3.93%** across the final run, or **3.90%** over the same first 231 samples used for the baseline's **5.07%**. These measurements include the sender and its descendants, normalized across 20 logical processors. Other isolated QA workflows ran concurrently during the new run. Overall private memory averages **1,405 MiB** (peak **1,636 MiB**), versus the baseline mean **1,371 MiB**. File buffering trades some memory and 50 ms of local-file presentation latency for timing headroom.

Chat private memory, excluding the first five two-second samples, averages **921 MiB** versus **947 MiB** in the shorter baseline. The new run's first/last two-minute means are **976/915 MiB**, with **913 MiB** at the end. Its first two-minute mean is actually above the baseline's **958 MiB**. Startup garbage collection, different durations and varying real chat prevent a leak-fix claim. The resource comparison is saved in `P/comparison.json`; sample histories and full media results remain alongside each recording.

The owner's profile was reopened on the final package, preserving all seven scenes, eight sources and four channels, and leaving production idle (four checks). Restoration evidence is under `native-qt/build-integration/ui-exploration/20261106-085441/playback-restored-20261107-150024/`. The saved Twitch session still exposes **Log Out**; returning restores ready chat without changing channels or starting output (four more checks, `saved-twitch-20261107-150025/` beside the restoration evidence). No credentials or browser storage were read. Studio is left open and idle on this profile.

## Retained failures

The first optimized-host package (`polish1`) failed the actual practice-chat journey: removing unused markup left the script before an HTML body existed. The explicit body fixes startup; the final package repeated all general/support checks. That failed artifact is not qualified. `polish2` was an intermediate package before high-rate queue bounding; it is not the final artifact.

The first busy baseline harness reused `$chat`, colliding with PowerShell's case-insensitive `$Chat` switch. It stopped its owned recording/output safely; `baseline-live2` is the successful rerun. The first transport media checker requested unavailable `libx264`; the corrected checker uses bundled FFV1 and retains extraction/probe logs. Its failed `media/` and successful `media2/` results remain separate. Build wrapper/execution-policy failures were corrected and are not application results.

No new two-hour soak or installer validation is claimed for this executable. The earlier [reliability report](studio-reliability-2026-11-07.md) identifies its different artifacts. Real broadcast rollover, complete network loss, physical hotplug, forced disk-full conditions, real gift extraction, Meshcast and the remaining platforms are still unqualified. Chat memory remains substantial.
