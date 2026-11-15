# Six separate audio inputs (experimental)

Desktop Studio can preserve the first six inputs of a local audio interface or media file and record them as six independently editable tracks. Stereo remains the default. This feature is available in the local review package; it has not been released.

## Use it

1. Add or edit a **Microphone / audio input** or **Video / audio file** source. Choose **Six separate inputs (experimental)** under **Input channels**.
2. Supply a format with 6–8 channels. For an interface, Windows must expose those channels through its shared-mode capture endpoint. The mixer reports the channel count actually received. A source supplying fewer than six channels is silent and displays an explanation; select Stereo to use it normally. Formats above eight channels are unsupported.
3. Open **Tools → Recordings → Recording tracks...** and select the source's **Input 1** through **Input 6**. Each selection uses one of the 16 available extra audio tracks. Program is always recorded, so the maximum remains 17 audio tracks in the Program MKV. Other sources can share the remaining choices.
4. Record, stop, then use **Tools → Recordings → Export recording as MP4...** if needed. Export preserves the selected tracks and their names. The original MKV remains intact.

Input mode and recording choices are saved with the show. Stop recording before changing input mode. An older show without an input mode opens in Stereo.

## Signal behavior

Each selected input becomes its own stereo AAC track with the same mono signal in both channels. These are six separate full-band tracks, not a 5.1 surround track: input 4 is not assigned an LFE role. The source's ordinary separate-audio choice records its stereo mix instead.

All six inputs share the source gain, mute, effects switches and delay. Effects process each input independently. Individual recording tracks honor On air, Backstage, Mute mics, Private talkback and Private break, using the existing source privacy rules. They remain recorded when the source is off-screen. Removing a source leaves its already-created recording tracks silent. Source-wide controls are shared; there are no six individual mixer faders.

Program, headphones, guest returns and streaming receive a stereo mix: inputs 1/3/5 feed left, 2/4/6 feed right, with each input weighted by one third. Inputs 7–8 are omitted. The existing Stereo/Mono/Left/Right control affects this stereo mix; it does not remap the six individual recording tracks. Headphones retain the existing pre-delay monitoring behavior.

This change does not add six-channel browser capture, six-channel Program transmission, or OBS publishing. Those are separate workstreams. A real six-channel interface and the chosen editing application still require acceptance runs.

## Review package and verification

Candidate: `native-qt/dist/desktop-studio-six-input-review-20261114/desktop-studio.exe`.
Executable SHA-256: `44f999888816a0ec33478920aeb9eaeefdc46e206a9b4d79abaeaa65d12d1bf9`.

The package contains the current application dependencies, chat assets, FFmpeg and automation schema. The existing `dist/desktop-studio-portable` package is preserved. This candidate is not signed or published.

Application acceptance uses actual multichannel WAV files through the packaged Media Foundation decoder, isolated worker, mixer, recorder and MP4 exporter. File-rate cases exercise Media Foundation's rate conversion; they do not qualify a physical interface's capture format. The six-channel resampler also has a separate code gate; a passing code gate is not application testing.

The worker's PCM queue now holds 200 ms measured in frames at the actual channel count. The former stereo-sized queue truncated six-channel decoder packets, leaving periodic gaps. Ordinary 48 kHz stereo retains the same 19,200-float queue bound. The shared region has a bounded maximum for eight channels at 192 kHz, and its version marker rejects incompatible workers.

| Packaged application journey | Evidence |
| --- | --- |
| Six native file inputs at 44.1/48/96 kHz; first six of an eight-channel file; six named inputs plus a reference and Program; MKV and MP4 | [Four passing cases](../native-qt/qa/reports/six-input-final-rerun-20261114/six-input.json). All six speech-range tones remain isolated, including 2900 Hz in input 4. Unwanted source tones must remain below 1% of the intended tone. Each inspected 10 ms window must remain audible. Inputs 7/8 are excluded. |
| Mute, On air, Private break, saved input mode, insufficient-input warning/silence, return to stereo, reject mode changes while recording | Same application report. Privacy checks measure complete one-second windows; user show preserved and all owned processes exit. |
| GUI selection, saved track choices across an actual restart, recording and export | [GUI report](../native-qt/qa/reports/six-input-gui-final-rerun-20261114/six-input-ui/six-input-ui.json) and [isolation report](../native-qt/qa/reports/six-input-gui-final-rerun-20261114/isolation.json). Eight named tracks in each file; full decode, independent inputs and unchanged original MKV. |
| Existing stereo recording/export during continuous streaming: 7, 16, reordered 7 source tracks, each plus Program | [Three passing cycles](../native-qt/qa/reports/six-input-stereo-cycles-20261114/multitrack-roundtrip.json). Correct source isolation, names/order, unchanged encoded AAC through export, overwrite protection, clean receiver decode and no streaming-worker restarts. |
| Four separate source videos, off-screen retention, privacy, source removal and Program-writer failure | [Passing recording-isolation workflow](../native-qt/qa/reports/six-input-stereo-privacy-20261114/recording-isolation.json). All four source files decode and continue independently when the Program writer is deliberately terminated. |
| Existing maximum-track recording with browser audio and continuous local RTMP output | [Passing concurrent workflow](../native-qt/qa/reports/six-input-stereo-combined-20261114/combined-recording.json): 133.9-second sustained phase with 16 source audio tracks plus Program and four source videos. All five recordings and both receiver captures decode; all 21 audio streams are silent during the measured private break. No worker restarts; maximum observed Program video packet gap 34 ms. |
| Open a show saved by the previous portable package, use its saved recording choices, export and reopen after saving | [Passing legacy-show workflow](../native-qt/qa/reports/six-input-legacy-20261115/legacy-audio.json). The unmodified show has no `inputChannels` fields. All sources default to stereo; its seven selected sources plus Program retain names, isolation and identical exported AAC. The original old show and user's show remain unchanged. |

[Candidate verification index](../native-qt/qa/reports/six-input-candidate-20261114.json) ties the application results to the executable hash and confirms no candidate processes remain. The original portable executable retains SHA-256 `4873f5feeb76451ce233c0950a96a3924efbb5418a733dead2bf80b98eac6a77`. The candidate's short concurrent run is not a multi-hour endurance qualification. Its dropped-frame counter increased from 14 to 15 early in the run, then stayed at 15; main-process private memory grew about 9.9 MiB and the process tree about 83.6 MiB during the measured phase.

The GUI runner was updated for the current Tools → Recordings menu and Qt file dialog. An initial privacy analyzer used the request start time before encoder startup; the corrected run anchors after recording becomes active and observes the middle of a three-second mute interval. The silence threshold remains unchanged. Failed attempts are retained beside the final reports.

The audio conversion gate reports 26 passes and one skipped physical-input stress case. Building that gate exposed missing existing chat sources in the shared test-library target; its source list now matches the application for those components. This build-only correction does not alter the application target.

Physical six-channel capture, device unplug/replug, listening through real headphones, noise suppression with real speech, editor import/solo/edit/export, and multi-hour endurance remain open. File decoding and frequency measurements do not close those journeys.

## Remaining acceptance work

On November 14, the packaged application's read-only input enumeration found only Logitech BRIO and Realtek Stereo Mix endpoints, both exposing two channels at 48 kHz. No matching audio/video editor was found in the standard Program Files/Local Programs folders or installed-app registry. Enumeration did not start microphone capture. The intended interface model and editor remain to be supplied.

1. Connect a real interface that exposes 6–8 channels to Windows. Feed each input separately, then all six simultaneously; verify the individual recordings, stereo mix and headphone output. Repeat stop/start and disconnect/reconnect, and verify mute/private-talkback behavior with actual microphone inputs.
2. In the intended editor, import the MKV or exported MP4, identify all tracks, solo and edit one input, then export and confirm the other five remain unchanged. Include a recording with more than six independent source tracks.
3. Run a multi-hour capture/recording session using that interface, including concurrent streaming and repeated recording/export cycles, before promoting this candidate.

Six-channel Program transmission and native OBS publishing are separate implementation work. They need opt-in multichannel encoding and receiver negotiation, an explicit channel order, and a stereo fallback that retains all six inputs. The current recording candidate does not imply either publishing feature is complete.

## Main checkout verification, 2026-11-15

The primary checkout is on `main`. Its audio implementation was rebuilt and staged separately as `native-qt/dist/desktop-studio-six-input-main-review-20261115`. Executable SHA-256: `d6cdecd16cbcdabceaf49452e787ccef79da1c7eec575c1af8e5734cd1833829`. The older packages remain unchanged.

Fresh packaged runs passed all four multichannel file cases and the three stereo recording/export cycles during continuous streaming (8, 17, then 8 total audio tracks). The multichannel run includes source privacy, saved input mode, insufficient-input handling and independent MKV/MP4 tracks. Both applications runs shut down cleanly and preserved the user's show. [Checked-in result summary](mixer-six-channel-main-validation.json) identifies the executable and local evidence. This is targeted audio acceptance, not full release qualification; the hardware/editor limitations above remain open.
