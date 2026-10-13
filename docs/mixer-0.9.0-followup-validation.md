# Desktop Studio 0.9.0: additional browser audio and recovery testing

This follow-up ran the packaged Windows app and verified actual recorded media,
private receiver playback, and visible browser controls. Reports and recordings
remain private under `native-qt/e2e/reports/studio-090-additional/`.
The later recording-recovery pass below uses `studio-090-recovery-depth/`.

## Browser audio defect found and fixed

On the original 0.9.0 executable
(`ab185f5d76a551fd991b0c36ad5436b94a7a80842190724efcad41aa99e0e5db`),
replacing the audio track inside an existing `MediaStream` could leave the
removed track audible. Changing an element from a live stream back to a file
could also leave the old live stream connected and omit the file's audio.
Recorded 440/880 Hz fixtures reproduced both failures; this was an actual audio
routing failure, not just an incorrect meter.

The audio bridge now compares track membership, rebuilds the live-stream tap
when it changes, disconnects obsolete taps/listeners, and restores automatically
captured file audio after `srcObject` is cleared. It preserves audio graphs
created by the page. Empty streams stay silent and resume when a track is added.

The first audio-corrected packaged executable has SHA-256
`38495cb5f658eab8497070aa6b037963731815a21c47ebd90749c4ddba8e9ce3`.
The final packaged executable for that browser pass also corrects the recovery notice described below:
`4fb5c3d2b1caa5783c1b6e1f3f753a32e1bb1438e6f4db5cbb10f704b5fa0598`.
The subsequent recording-recovery build is identified below; 0.9.0 remains unreleased.

## Recovery notice found during visual inspection

The browser picture recovered after its host was killed, but the bottom status
bar still said "Reconnecting automatically". A replacement browser proxy need
not emit a recovery signal because it begins without an error. The app now
retires that source's previous warning when usable video returns, preserving
any newer unrelated status message. Obsolete capture callbacks are ignored.
The visible workflow now checks this notice, and the final screenshot shows
"Browser video resumed" while the recovered picture and audio meters are active.

## Completed workflows

| Workflow | Actual result |
|---|---|
| Browser audio switching | Twenty separately recorded phases passed full decoding and tone/silence checks: file playback, pause, mute, file replacement, file-to-stream, stream replacement, track replacement/removal/addition, stream-to-file, page-owned audio graphs, targeted disconnect/reconnect, context close, cross-origin iframe playback/removal, navigation and playback afterward. The native browser worker stayed the same; default transparency survived navigation. |
| Guest regression | Four, six and eight private VDO.Ninja guests received video and mix-minus. At eight guests, own-tone measurements were approximately -91 to -104 dB and another guest's tone approximately -39 dB. Recording decoded through Private break, backstage/readmit and a peer leaving. |
| Visible browser security | Camera, microphone, screen, clipboard and notification requests were denied; host objects, file/custom navigation, popups/downloads and malformed privileged messages were blocked/ignored. The final build's eight-second message flood had a maximum observed UI response of 51.89 ms and 0.12 MiB parent RSS growth. Killing the browser host recovered while recording continued; its recording fully decoded and the recovery notice cleared. This is bounded fixture coverage, not proof against all browser vulnerabilities. |
| Visible chat interaction | Open / Invite opened the chat dock on its first click; closing and reopening it three times passed. |
| Suspended browser audio | An AudioContext stayed silent for 15 seconds and then resumed with the same browser worker. Recording stayed active and fully decoded with the expected tone before/after silence. |
| Abrupt app termination | Killed only the owned app while Program, a named audio track and a separate source video were recording. All captured descendants exited. Its Unicode-named autosave/media reopened, a stale owner lock was recovered, and outputs stayed off after restart. Both interrupted MKVs exported through the app to MP4 and fully decoded with the expected tone. A new recording after restart also decoded. |
| Damaged project and backup | A malformed active show was preserved byte-for-byte in a recovery file. The app created a usable default show and subsequently opened the rolling backup as its active project. |
| Low disk and actual disk-full | Used a disposable 256 MiB WSL tmpfs, without filling the Windows drive. Program recording stopped at the 128 MB guard while RTMP reception continued. A separate file publisher reached real ENOSPC and reported an error while independent Program recording survived. Surviving recordings/receiver output decoded; the temporary mount was removed. |
| Installed application | Installed into a private QA directory, exercised packaged CLI/MCP control, captured twelve distinct portrait live screenshots and recorded moving video. The recording fully decoded; its maximum frame interval during screenshots was 47 ms. All owned descendants exited, installed/ZIP/portable executables matched, and uninstall removed the app, automation, shortcuts and installation registration. |

Disk and abrupt-crash workflows used the original executable. Guest, chat and
suspended-audio regressions used the first audio-corrected executable; browser
audio switching and visible security/recovery were repeated on the final
executable. The native recording/project implementation did not change in that
browser pass; the subsequent recording changes are described below. Neither
pass constitutes another hour-long soak.

The installer workflow above used the browser-recovery executable. Its documentation was
packaged with the same tested runtime, verified against SHA-256 manifests. No
GitHub release was published. The saved user show and original Game Capture
checkout were unchanged, registry preferences were restored byte-for-byte, and
no test app processes remained.

## Limits and reproduction

This fix does not establish support for every protected or CORS-restricted
browser audio graph. It does not resolve the transparent snapshot frame-rate
limit or the WebView2 memory growth measured in the
[earlier rehearsal](mixer-0.9.0-validation.md). The eight-guest color differences
also remain; the panel check verifies picture identity/placement, not accurate
color reproduction. Clean-machine/Windows 10 and external hardware/account
checks remain as previously listed.

New workflows: `mixer-browser-audio-switching-e2e.py` and
`mixer-crash-project-recovery-e2e.py`. Existing workflows used here include
`mixer-disk-failure-e2e.py`, `mixer-panel-080-e2e.py`, and the visible
`mixer-ui-workflows-e2e.py` cases `browser-security` and `browser-interaction`.
Installer coverage uses `mixer-installed-e2e.py`.
Build, JavaScript/Python syntax, package hashes and whitespace checks are gates,
separate from this actual app testing.

## Recording recovery and combined-load follow-up

The original browser-recovery build (`4fb5c3d2...`) recovered a one-minute
recording after abrupt termination, but its last video timestamp was 58.215 s.
Four separate source recordings ended at 58.225 s. All files decoded and all
four selected audio tones survived MP4 export, but roughly 1.8 seconds remained
inside the muxers when the processes were killed.

Recording now uses a 250 ms MKV cluster limit alongside packet flushing. It
does not change encoding quality, bitrate or frame rate. On the corrected build
(`d5fc7ac0c90edcbaa34b428bf0a4195967c92d50b0b78bcaa6a4b98e7b2ebbbc`),
the same one-minute scenario preserved video through 59.943 s in Program and
59.899-59.920 s in the four source recordings. All five files and all selected
audio tracks decoded after export. These measured endings are not a universal
maximum-loss guarantee; this does not force Windows or drive caches to disk.

| Abrupt-exit scenario | Observed result after shorter clusters |
|---|---|
| About 0.1 s after Recording appeared | No file contained a video frame. Starting the encoder/writer does not mean a frame has reached the recording. |
| About 0.7 s | All five MKVs decoded, through approximately 0.679-0.702 s. |
| About 2.3 s | All five MKVs decoded, through approximately 2.218-2.241 s. |
| About 5.5 s | All five MKVs decoded, through approximately 5.220-5.433 s. |
| About 60 s | All five MKVs decoded and exported; named audio track identities and expected tones survived. Every captured descendant exited after its parent was killed, and outputs remained off after reopening the show. |

The final executable adds playback guidance and has SHA-256
`63c42bd1d6585b69825eab07e39d82c4853f3281bc659391f8d432ba9a41f7eb`.
Using its actual interface, the interrupted MKV played picture and audio but
reported no duration. The Playback dialog now explains **Recordings > Export
recording as MP4...** and disables seeking until a duration is available.
The visible export workflow preserved the original file, decoded all exported
tracks, played the MP4 with audio, sought to its middle and near its end with
the keyboard, and resumed playback.

The export fault workflow suspended and killed an export worker after it wrote
about 6 MiB of partial MP4. Independent Program recording kept advancing and
fully decoded afterward; the app rejected overwriting the partial MP4, and
retrying with a new filename succeeded. Malformed input reported an error.
Killing the whole app during another partial export also terminated its worker,
preserved the original MKVs, and allowed a successful export after reopening.
The maximum sampled API response during the suspended export was 31.42 ms.

The 1080x1920 portrait/60 fps combined workflow sustained Program plus 16 named
audio tracks, four separate source videos, browser audio and hardware RTMP to
a private receiver. All five recordings and both receiver captures decoded;
all 16 source tones were present. It produced 4,768 Program video packets,
including setup/receiver capture time, with a maximum timestamp interval of
33 ms. The recording drop counter stayed at its startup value of 28. The
approximately 63-second sampled interval used 4.07 logical cores across the
entire tree; API p95/max were 30.46/32.18 ms. This measures output cadence,
not 60 distinct frames from source media recorded at 30 fps.

The 720p30 maximum-track workload ran for 600 seconds after warm-up, with the
same five recording files, 17 Program-file audio streams and private hardware
RTMP. All five recordings and both receiver captures decoded. All 16 selected
source tones survived, including browser audio; an interior sample of Private
break was silent across all 21 audio streams (17 in Program plus four source
files). There were 18,559 Program video packets including setup/receiver time,
with a maximum timestamp interval of 50 ms, no worker restarts and no increase
from the startup recording-drop count of 13.

Total CPU averaged 2.43 logical cores during the ten-minute sampled interval.
The earlier 63-second baseline was 2.35 cores. Matching short windows initially
showed 2.34 versus 2.65 cores; a later three-minute corrected-build interval was
2.35 cores. These are sequential observations, not an isolated benchmark proving
zero overhead. A ten-second process sample attributed about 0.68 cores to
Program's 17-track AAC writer and 0.30 cores to the main app. Total private memory
grew 97.67 MiB and handles grew by 241; main private memory grew 12.89 MiB.
Memory fluctuated after warm-up, but this does not establish leak-free operation.
The first offline privacy check failed because the harness supplied `.5` instead
of `0.5` to FFmpeg; correcting that argument and analyzing the retained media
completed verification. Live API latency samples were not persisted before that
harness error and are not claimed for the ten-minute run.

A further finalization case suspended the Program writer, requested Stop,
confirmed finalization was still busy, and terminated the app. All five MKVs
decoded afterward, and no captured descendants survived. This checks a blocked
writer during shutdown, not physical drive removal.

Large-file coverage generated a 4,985,796,308-byte MKV by repeating recovered
media, then used the app to export a 4,992,205,131-byte MP4 in 13.34 seconds.
Both containers played in the app, sought to about 89:57 (beyond the 4 GiB file
boundary), and resumed with picture/audio. Beginning, middle and end samples
decoded all tracks. Source hashes stayed unchanged; the two large fixtures were
removed after verification, retaining hashes and near-end screenshots. This was
large-file export/playback coverage, not a 90-minute live recording rehearsal or
a complete decode of both large files.

The final executable passed a fresh installation, packaged CLI/MCP control,
twelve distinct portrait screenshots during recording, full recording decode
and uninstall. The maximum frame interval during screenshots was 52 ms.
Installed, portable and ZIP executable hashes matched; all owned descendants
exited. Final documentation is repackaged with the same tested runtime and
checked against its hash manifest. User preferences were restored byte-for-byte;
the saved user show and original Game Capture checkout were unchanged.

Remaining checks include actual power loss/OS failure, storage disconnection,
longer maximum-load rehearsals, remote phones/guests and physical lip-sync,
authenticated provider/chat accounts, other GPUs and audio devices, external
virtual-camera consumers, lock/sleep/RDP transitions, and the deferred clean
Windows 10/machine installation. Software process termination is not equivalent
to losing power or disconnecting a disk.

Reproduction: `mixer-crash-timing-e2e.py`, `mixer-export-failure-e2e.py`,
`mixer-combined-recording-e2e.py`, `mixer-large-file-recovery-e2e.py`, and the visible runner's
`recovery-playback-ui` workflow. `STUDIO_RECOVERY_INPUT` selects an interrupted
MKV; `STUDIO_COMBINED_SECONDS` and `STUDIO_COMBINED_FORMAT` control the bounded
combined workload. `STUDIO_CRASH_AGES` selects crash timings and
`STUDIO_CRASH_DURING_STOP=1` exercises interrupted finalization.

## Fault quality and diagnostic retention

The next pass uses private evidence under
`native-qt/e2e/reports/studio-090-fault-quality/`. It adds two packaged-app
workflows, `mixer-diagnostics-fault-e2e.py` and `mixer-fault-quality-e2e.py`.

Review found that restarting truncated the main diagnostic log, and repeated
concurrent instances accumulated UUID-named logs. Logs now rotate on startup
and at 5 MiB, keeping three archives per independently locked instance slot.
Slots are reused after their owner exits. Native crash reports also preserve
the last log beside the JSON/minidump. The exception handler no longer enters
the logger, whose mutex could belong to the faulting thread. Worker starts and
failures now identify the component and PID; failures are logged once per
worker without source URLs or command payloads.

The diagnostics workflow records moving video and a known audio tone, then
injects an actual unhandled access violation into its owned packaged process.
It verifies the exception code, JSON, minidump, crash-log snapshot, complete
decode of the interrupted MKV, descendant cleanup, and a fresh recording after
restart. Five further launches rotate near-limit closed logs while checking
that the crash snapshot remains unchanged. A synthetic publishing credential
must be absent from logs and the issue report. Blocking the log directory with
a regular file must still allow launching, recording and decoding media.
These checks do not establish that crash dumps contain no private process data.

The quality workflow records Program plus separate media/browser audio tracks
while publishing hardware-encoded RTMP through a private loopback proxy. Its
moving source contains binary frame numbers and audio pulses whose frequencies
identify their matching source second. Analysis uses decoded video/audio PTS
and source identities, measuring repeated and skipped source pictures as well
as container timing; it cannot mistake a neighboring one-second pulse for the
correct sync match. It exercises 24 busy JavaScript workers, browser process
termination, browser suspension, 35 seconds at approximately 256 kbit/s,
bandwidth restoration without a fixture socket reset, publisher termination,
and simultaneous browser termination/publisher suspension. Each fault must
leave the independent media worker and recording alive. Received clips are
decoded and checked against a current Program screenshot for stale backlog.

The initial quality run on executable
`519dc0a43ae4b33dc9ca1774041803124d594265fafb47cbd3d99b6c9ee2018f`
recorded 6,699 video frames at 30.001 fps. Local maximum A/V offset was 26 ms;
the maximum timestamp gap was 58 ms and the longest repeated picture was
92 ms. Four receiver clips decoded with maximum measured offsets of 19.5 to
36.2 ms. Recording drops stayed at the startup count of 13, browser audio was
present before/after recovery, and the unaffected media worker never restarted.
API p95/maximum was 27.6/38.6 ms. Total private memory peaked at 940.9 MiB
during deliberate browser overload and fell after the browser was replaced.
This is a short containment measurement, not a long-term leak clearance.

Process CPU samples in that run attributed about 0.12 logical cores to the
main app at baseline and 0.21 during the busy-browser interval. The browser
runtime used approximately four logical cores during intentional overload,
consistent with its existing per-source budget. Both hardware encoders together
used approximately 0.29/0.32 cores. These are observations of this workload,
not an optimization benchmark or a promise for other machines. Newly started
processes contribute only after their first sample.

The harness initially supplied an unsupported RTSP `rw_timeout` input option;
using the RTSP-specific `timeout` option allowed the complete run. The first
diagnostics attempt expected HTTP 200 for issue-report creation; the API
correctly returned 201. Both fixture mistakes were corrected before the
successful runs above.

The final executable adds the worker failure records:
`caf831dd7a2bb24b17f7d44610cadabcc02ea4b5f233b2a2ae1a22af7d30a4b3`.
Its repeated quality run recorded 6,711 frames at 29.999 fps. Program and the
separate clock audio track independently measured 15 ms p95 and 17 ms maximum
absolute A/V offset, with no unmatched pulses. The largest timestamp gap was
52 ms, and the longest repeated picture was 78 ms. There were 17 repeated and
14 skipped source pictures in the measured interval, so this is not a claim
of zero picture loss. Recording queue drops remained at the startup count of
nine throughout all faults. Four received clips decoded with maximum A/V
offsets of 15.2-27 ms. Their last pictures were 200-233 ms behind the current
Program screenshot; the restored stream was not playing a growing old backlog.

API p95/maximum was 26.9/31.9 ms. The main process averaged 0.21 logical cores
both at baseline and during browser CPU pressure; the deliberately busy browser
runtime averaged 3.99 cores. Total private memory was 634.6 MiB initially,
peaked at 940.3 MiB, and ended at 647.2 MiB. At the half-second sampling cadence,
replacement browser processes first appeared after 2.14 seconds for termination
and 12.45 seconds for suspension. A publisher replacement appeared after
1.09 seconds for termination; during the simultaneous fault, browser/publisher
replacements appeared after 2.20/5.45 seconds. These are process-replacement
observations, not first-decoded-frame recovery latencies. The failure log
contained exactly the five injected worker failures, with component, PID and
reason; it did not repeat warnings on every media/status poll.

The final executable also passed the access-violation/recording/rotation/log
failure workflow again. Concurrent visible instances retained separate logs,
reused released default and secondary slots, and archived the secondary
instance's preceding session. That older fixture first tried opening the same
show in both instances; the app correctly refused the second open. Giving each
instance its own show completed the intended log-isolation workflow. Opening
the default show in that old fixture had also serialized newer default fields;
cleanup detected the changed hash and restored the exact original bytes from
the app's verified backup. The fixture now explicitly selects separate shows
and checks the user's default-show hash. User preferences were restored
byte-for-byte, and the original Game Capture checkout was unchanged. A fresh
installation passed CLI/MCP control, portrait screenshots during recording,
media decode and uninstall. Portable, ZIP and installed executable hashes
matched, and owned children exited. Build, native crash-reporter and whitespace
checks are gates; the packaged workflows above are the actual testing.

The next production checks should prioritize long repeated-failure rehearsals
with memory/handle/GPU-memory trends; SRT/WHIP/guest packet loss, reordering and
variable delay; long-run physical lip-sync across independent device clocks;
slow/disconnected/network storage; and real GPU/device/reset, sleep and RDP
transitions. Maximum retained crash-artifact storage, worker minidumps and
broader secret-redaction cases also remain follow-up work. The clean-machine
check remains deferred. No software fault injection proves power-loss safety.

## UDP loss, reordering and outage pass

Private evidence is under `native-qt/e2e/reports/studio-090-udp-faults/`.
`mixer-udp-fault-e2e.py` runs the packaged executable above, records Program
independently, and publishes through a bidirectional loopback UDP relay to
MediaMTX 1.21.2. It uses the same frame-number/audio-identity clock as the
previous quality workflow. SRT and WHIP request Auto hardware encoding at
3,500 kbit/s; both report H264 Encoder MFT (Hardware). SRT requests 300 ms
latency. WHIP offer/answer ICE candidates are rewritten only by the private
fixture so media cannot bypass impairment. No firewall settings or machine
network configuration change.

Each protocol gets a clean baseline, 1% independent packet loss plus uniformly
distributed 0-25 ms delay, 10% loss plus 0-100 ms delay, a 20-second complete
bidirectional outage, and restored connectivity. Per-packet delay deliberately
reorders bursts. The fixture retains received MKVs before/during moderate
impairment and after restoration, plus Program screenshots, relay counters,
API latency, recording drops and process memory. Severe impairment/outage
exercise containment and recovery; they do not have receiver quality clips.
The source and recorder workers must retain their PIDs, recording must stay
active, and all received clips must decode and meet the existing timing limits.
Decode failures stay failures even when transport and the app remain alive.

The first full run found a reproducible WHIP quality gap. Independent recording
decoded 6,481 frames at 29.999 fps with maximum measured A/V offset 24.3 ms,
maximum timestamp gap 51 ms and longest repeated picture 77 ms. All three SRT
clips decoded cleanly, including the moderate impairment clip. WHIP's baseline
decoded cleanly, but its moderate impairment clip contained H264 macroblock
errors. The app, source worker and recorder survived the entire sequence.

A separate WHIP run isolated 1% loss from 0-25 ms jitter. Loss alone reproduced
the H264 corruption; jitter/reordering alone decoded cleanly at 29.999 fps,
maximum measured A/V offset 48 ms. Restored reception also decoded cleanly.
The relay observed repeated RTP sequence numbers during both impaired phases,
and SDP negotiated video NACK/PLI: retransmission was active. The independent
recording decoded 2,392 frames at 29.998 fps with maximum measured A/V offset
29 ms. Recording queue drops remained at the startup count of ten; API
p95/maximum was 26.9/31.9 ms. Process-tree private memory ranged from 382.0 to
395.3 MiB. These short observations do not establish long-term leak freedom.

Receiver behavior is relevant to diagnosis: MediaMTX uses the default
[64-packet reorder buffer](https://github.com/bluenviron/gortsplib/blob/v5.6.7/pkg/rtpreceiver/receiver.go),
while its Pion dependency's default
[NACK interval is 100 ms](https://github.com/pion/interceptor/blob/v0.1.49/pkg/nack/generator_interceptor.go).
Delayed retransmissions can therefore outlive that buffer at this packet rate.
This is a supported hypothesis, not proof that the receiver is the sole cause.
Cross-checking another WHIP receiver and evaluating pacing/recovery behavior
remain necessary before choosing a production change. No production bitrate,
quality, buffering or security settings were weakened to make this pass green.

The full repeat retained 146 observations of dropped RTP packets being
retransmitted during WHIP's moderate phase; 66 were at least 64 sequence numbers
behind the latest packet when they returned to the relay. That is evidence of
late recovery, not proof those packets were accepted by the receiver. WHIP's
moderate clip again had H264 errors. Its restored clip decoded cleanly with
maximum measured A/V offset 46 ms, and its final picture was 233 ms behind
Program. This repeat's WHIP baseline also failed the strict decode gate on
duplicate video timestamps around two seconds; the two preceding baselines
passed. The intermittent timestamp issue remains open separately from loss.

SRT's repeat baseline and moderate clips decoded cleanly, at approximately
30 fps and maximum measured A/V offsets 39.2 and 32 ms. Its restored clip
decoded cleanly with maximum measured A/V offset 34 ms, but its final picture
was 3.20 seconds behind Program, exceeding the 3-second freshness limit.
The first full run's corresponding age was 2.77 seconds, compared with
baseline/moderate ages of 0.57/0.73 seconds. This is a recovery-latency finding;
clean decode and an active publishing indicator do not clear it.

Independent recording in the repeat decoded 6,495 frames at 30.000 fps,
maximum measured A/V offset 20 ms, maximum timestamp gap 51 ms, and longest
repeated picture 93 ms. There were 429 repeated and 425 skipped source
pictures, so nominal frame rate does not imply perfect source cadence.
Recording queue drops stayed at the startup count of nine. API p95/maximum
was 26.9/31.6 ms. Process-tree private memory ranged from 381.4 to 414.3 MiB;
handles started/ended at 3,435/3,104. Average sampled CPU was 0.22 logical cores
for the main app, 0.08 for media, 0.35 for both encoders, 0.04 for output workers
and 0.04 for FFmpeg. This workload includes simultaneous recording/publishing,
and newly started processes enter CPU accounting after their first sample.
It is not a before/after optimization benchmark.

At the half-second sampling cadence, SRT/WHIP first reported live again
1.11/4.25 seconds after connectivity returned in the repeat. Those are status
observations, not first-decoded-frame times. The source/recorder workers did
not restart. The freshness assertion originally interrupted offline analysis
at the SRT clip; the remaining retained clips were then analyzed separately
in `final/udp-fault-analysis.json`. The fixture now retains freshness failures
per clip and continues the remaining analyses without making the run pass.

The final SRT-only diagnostic adds another 30-second settling interval and
received clip, followed by stopping/restarting publishing on the clean path.
Baseline/moderate final-picture ages were 700/633 ms. After automatic outage
recovery, age was 3.60 seconds; after settling it remained 3.47 seconds. A
clean publishing restart restored 633 ms. All five clips decoded cleanly at
approximately 30 fps, with maximum measured A/V offsets 28/33/37/25/43 ms.
Thus extra settling time did not clear the delay; a fresh publishing session
did. Receiver versus publisher buffering still needs separate attribution.
Independent recording decoded 5,250 frames at 30.002 fps, maximum measured
A/V offset 15 ms and maximum timestamp gap 51 ms. Recording queue drops
remained at the startup count of twelve. This diagnostic also exits with a
failure result for the two stale recovered clips.

This pass changes the reusable QA fixture and validation notes only. The
remaining software priorities are persistent SRT recovery delay, WHIP video
quality under modest loss, and the intermittent WHIP startup timestamp
duplicate. A second receiver implementation should be part of those fixes'
validation. Guest WebRTC impairment and long device-clock drift remain
untested by this fixture. No GitHub release was published for this pass.

All owned app/receiver processes exited. User preferences were restored
byte-for-byte, the default show's original hash matched, and the original
Game Capture checkout was unchanged. Syntax/whitespace checks are gates;
the recorded packaged-app workflows above are the testing, with the stated
quality failures still open.

## Publishing recovery implementation and receiver cross-checks

This follow-up adds native WHIP video RTX, bounded retransmission history,
packet pacing, negotiated playback-delay hints and audio RED, plus a bounded
fresh-session retry for stale SRT startup. The controls and protocol limits are
described in [Publishing recovery](../README.md#publishing-recovery), with
automation fields in [the API documentation](mixer-automation.md).
This is an unreleased 0.9.0 development build; it does not close every receiver
compatibility finding above.

WHIP offers NACK/PLI and RTX and uses what the SDP answer accepts. RTX packets
have an independent SSRC and sequence space, retain the original media clock,
and carry the original sequence number after the complete RTP header, including
extensions. RTCP sender-report octet counts exclude extensions and padding.
An actual-browser trial exposed an extension-offset error in the first RTX
implementation; the corrected layout has a regression gate using the real H264
packetizer. There is no change to ordinary VDO.Ninja transport negotiation.

History defaults to 1500 ms, capped at 4096 packets / 8 MiB. A feedback batch
can request at most 512 distinct packets. Repeat repairs for one packet are
suppressed for 50 ms, without delaying its first repair. Repairs have a per-second
payload budget of half the configured video bitrate, with a 64 KiB floor and
2 MiB ceiling. These bounds limit excessive feedback; a cache-miss count does
not include requests suppressed by the repeat/rate limits. PLI-triggered
keyframes are limited to one per 250 ms. Primary video is paced at 1.25 times
the configured bitrate; an 8 MiB / 750 ms queue bound fails the destination
instead of retaining unbounded old media. Repairs, audio and RTCP bypass that
queue. The retransmission counter measures repairs handed to the transport,
not confirmed receiver delivery.
This is pacing and recovery, not adaptive congestion control.

Playback smoothing defaults to a best-effort 500 ms minimum, requested only
when the answer accepts the playout-delay extension. The sender does not force
an uncooperative receiver to buffer. Earlier 150 ms browser trials decoded
frames but displayed substantial late-frame drops; 300 ms improved playback
but did not pass every repeat. A 500 ms hint delivered approximately 30 fps
through both RED-on/off loss/jitter cases in the follow-up described below.
RED offers one previous Opus packet, bounded by the 1200-byte combined payload
limit. At configured audio bitrates above 448 kbps, the publisher selects 5 ms
Opus packets so both copies fit; ordinary settings retain 10 ms packets.
Neither video parity FEC nor Opus in-band FEC has been added. A receiver's
`useinbandfec=1` answer is not evidence that this sender generates it.

Private reports, clips, raw H264, SDP answers and screenshots are retained under
`native-qt/e2e/reports/studio-090-recovery-fixes/`. All network impairment is
confined to private QA relays; no firewall or machine networking is modified.

### SRT outage recovery

`srt-refresh-2` uses a packaged executable with SHA-256
`80b47fb2f7b88dd5295daae68099f4ffeb84e0a832f8b7a9ce4d4cb320f47019`.
The first attempt, checking media age alone, did not clear the persistent delay.
The implemented check also measures elapsed FFmpeg startup time. If the first
output progress arrives too late, it refreshes the session once using current
media. Consecutive refreshes are prohibited, including with intentionally high
SRT latency settings. Normal reconnect backoff remains in place for failures.

With the same loss/jitter/outage workflow, final-picture age was 600 ms at
baseline, 633 ms under moderate impairment, 600 ms after automatic outage
recovery, and 633 ms after another 30 seconds of settling. A manual clean
restart also measured 633 ms. All five receiver clips decoded cleanly at about
30 fps, maximum video timestamp interval 34 ms, maximum measured A/V offset
21 ms. This resolves the retained 3.5-second delay in this reproduction.

Independent recording decoded 5,219 frames at 30.000 fps, maximum measured
A/V offset 21 ms. Recording drops remained 9 to 9. API p95/maximum was
23.7/29.4 ms; process-tree private memory ranged from 380.1 to 414.8 MiB.
Average sampled CPU was 0.188 logical cores for the main app, 0.108 for media,
0.326 for both encoders, 0.034 for output workers and 0.047 for FFmpeg.
This is simultaneous recording/publishing, not a controlled optimization
benchmark or a long-term memory-leak assessment.

### WHIP receiver limits and timestamp attribution

The unmodified MediaMTX 1.21.2 receiver still produced H264 corruption under
1% loss with the new sender. A private diagnostic build changing only the
WebRTC inbound RTP reorder capacity from 64 to 512 packets passed separate
loss-only and jitter-only reception at approximately 30 fps with clean full
decode (`receiver-buffer512`). It did not negotiate RED or the playback-delay
hint. This supports a receiver-buffer limitation: increasing the sender cache
cannot recover a packet after the receiver has already discarded its frame.
The modified receiver is diagnostic only, is not shipped, and is not a claim
that the unmodified server passes. The default-size source is linked in the
previous section; `writeQueueSize` is not this inbound RTP reorder capacity.

The intermittent startup timestamp finding remains open in the MediaMTX-to-
FFmpeg RTSP recording path. Twelve clean starts in `startup-aligned` yielded
nine failed clips; six more with MediaMTX `useAbsoluteTimestamp: true` yielded
four failed clips (`startup-absolute`). Therefore that setting is not a verified
workaround. The retained sender-side primary RTP frame markers increase
monotonically, while the recorded RTSP timeline can move backward when initial
RTCP timing arrives. The aiortc receiver retained increasing video timestamps.
A later Chromium trial also observed one 5 ms backward adjustment in HTML video
`mediaTime` after network restoration, while the source frame number advanced;
this is a separate receiver-timeline finding from the RTSP startup failure.
The browser fixture now retains RTP timestamps as well as `mediaTime` to help
distinguish sender timestamps from receiver mapping. These observations are
not proof that every interoperability case is solved. Do not hide failed clips
by relaxing timestamp/decode checks.

The independent aiortc receiver retains the assembled H264 bitstream before
decoding, so a decoder's concealment cannot make corrupted input look like a
clean decode. In `reference-final`, 1% loss alone remained at about 30 fps and
the entire raw stream decoded cleanly, but combined loss/jitter fell to 27.13 fps
with a 267 ms maximum gap; that run failed. After increasing repeat-request
suppression to 50 ms, `reference-repeat-bound` passed: baseline/loss-only/
combined/restored reception measured 30.07/29.97/28.57/30.07 fps, maximum gap
133 ms, no backward timestamps and clean full H264 decode. Recording decoded
2,282 frames at 30.001 fps, maximum measured A/V offset 10.4 ms, drops 12 to 12.
This receiver did not select RED or the playback-delay hint. The comparison is
not a deterministic benchmark: packet timing and loss selection vary with
actual encoder output and scheduling.

Actual Chromium playback is checked separately with frame callbacks, audio
levels, negotiated codec stats and retained screenshots. At 300 ms smoothing,
`browser-final` passed with RED on/off at 28.20/29.01 displayed fps during
combined impairment. A later `browser-repeat-bound` run failed at 27.53/27.19 fps
and 300/267 ms gaps. These failures remain recorded. Increasing the playback
hint to 500 ms in `browser-500ms` produced the following observed rates:

| Audio mode | Clean | 1% loss + 0-25 ms jitter | Restored | Largest impaired gap |
|---|---:|---:|---:|---:|
| Negotiated RED | 30.03 fps | 29.96 fps | 29.91 fps | 34 ms |
| Ordinary Opus | 29.91 fps | 30.00 fps | 30.02 fps | 34 ms |

Both modes retained audible audio and increasing video timestamps. This is why
the default favors 500 ms for publishing; operators can choose a smaller hint
in Advanced. It does not change guest-call buffering, force a server's timing,
or guarantee smooth playback on every link. Browser displayed-frame counts are
separate from decoder counts; neither proves perfect source cadence.

`browser-high-audio` also exercised 510 kbps Opus with negotiated RED and
approximately 200 audio packets/second, confirming the 5 ms high-bitrate path.
Audio remained audible through loss/jitter. Its independent recording decoded
1,343 frames at 29.999 fps, maximum measured A/V offset 4 ms and drops 9 to 9.
This audio check measures successful browser reception, not perceptual audio
quality or a physical speaker-to-picture lip-sync measurement.

Further repeats did not consistently reproduce the clean 500 ms browser result.
`browser-shipped` retained an 800 ms impaired gap and the HTML `mediaTime`
adjustment noted above. A trial that paced repairs separately also failed the
browser and aiortc quality limits (`browser-paced-repairs` and
`reference-paced-repairs`); it was removed. An experimental queue delaying
initial audio until the first video keyframe was also removed because it did
not resolve startup timestamp conversion. The final code retains immediate
bounded repairs and the original audio startup behavior. A single passing
receiver run must not be described as universally reliable loss recovery.

### Desktop controls and packaging

`ui-persisted` used actual GUI clicks and keyboard entry to set history to
2000 ms, playback smoothing to 250 ms and RED off; start/stop WHIP; verify that
the controls disable while live; quit; and reopen with the same values.
Screenshots were inspected. Invalid API history/delay values and a string in
place of the RED boolean returned 400 without changing the configured profile.
The earlier `ui-final` reopen assertion was a fixture error: explicit
`--studio-show` intentionally disables publisher-preference persistence.
The corrected test selects a private active project and uses normal GUI launch.

Packet parsing, negotiation, RTX wrap/layout, malformed feedback, history/rate
bounds, pacer backpressure/shutdown and shared-clock checks are unit gates.
Packaged GUI and actual received/recorded media workflows are the testing.

### Final artifact closeout

The final portable executable has SHA-256
`0ed93fe9a171fa3fec6bcd8ffc7826d7d4b155e2774f5641849194acd44d7bac`.
The preceding experiment reports retain their own executable hashes; they are
not silently relabeled as results for this artifact.

`browser-closeout` uses ordinary combined A/V playback in one HTML video
element, with the final default 500 ms hint. RED-on baseline/impaired/restored
rates were 29.91/30.04/30.01 fps, with a maximum impaired gap of 34 ms. RED-off
rates were 30.07/29.88/29.98 fps, with a maximum impaired gap of 100 ms. Both
had audible audio, no backward `mediaTime` and zero Chromium-reported dropped
video frames. The independent recording decoded 2,682 frames at 30.000 fps,
maximum measured A/V offset 15 ms. Earlier browser cases used separate audio
and video elements; their variable playback results remain relevant to such
receiver implementations and are not erased by this combined-playback pass.

`reference-closeout` independently passed strict full decoding of the received
raw H264, at 29.94 fps clean, 29.89 fps with 1% loss alone, 28.70 fps with loss
plus jitter, and 30.24 fps after restoration. The maximum impaired timestamp gap
was 133 ms; no backward timestamps were observed. Recording decoded 2,285
frames at 30.000 fps with maximum measured A/V offset 15 ms, and recording
drops stayed 9 to 9. This receiver's lack of RED/playout-delay negotiation also
checks that plain-Opus and receiver-default timing remain usable.

`udp-closeout` repeated the full SRT/WHIP impairment and outage workflow on this
same artifact, including the extra SRT settling and clean-restart observations.
SRT's moderate/restored/settled/restarted picture ages were 600/667/667/633 ms,
with clean decode for those four clips. Its baseline RTSP clip failed on
duplicate DTS near two seconds. Thus the remaining timestamp-conversion
finding is also reproducible with an SRT publisher, not just WHIP. WHIP's clean
baseline passed; its impaired MediaMTX clip still contained H264 errors and its
restored RTSP clip had duplicate DTS. The overall UDP workflow correctly exits
with failure. SRT freshness is fixed in this reproduction; MediaMTX loss and
RTSP timestamp compatibility are not declared fixed.

The independent recording decoded 8,479 frames at 30.000 fps, maximum measured
A/V offset 21.4 ms, with recording drops unchanged at 9. The source/recorder
workers were not replaced during publishing failures. API p95/maximum was
23.3/27.7 ms; process-tree private memory ranged from 380.6 to 414.3 MiB.
Sampled CPU averaged 0.202 logical cores for the main app, 0.112 for media,
0.303 for both encoders, 0.041 for output workers and 0.036 for FFmpeg.

`ui-closeout` passed normal GUI configuration, live control disabling, invalid
API input and reopen/persistence checks on the final build; screenshots were
inspected. `installed-closeout` installed into a fresh private directory, ran
CLI/MCP control and twelve portrait screenshots during recording, decoded the
recording with a maximum frame gap of 35 ms, and uninstalled. Portable, ZIP and
installed executable hashes matched. All owned descendants exited.

Original preferences were restored byte-for-byte and the original user-show
hash matched. The Game Capture checkout was unchanged. Build, syntax,
whitespace and transport regression gates passed. No GitHub release was
published; the updated installer and portable archive are local development
artifacts. Remaining work is receiver interoperability, broader real-network
conditions and long-running A/V drift; these short fault runs do not establish
universal loss-free playback or long-term leak freedom.
