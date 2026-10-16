# Desktop Studio: Radeon RX 9060 XT codecs

Tested with AMD driver **32.0.22042.14002** on Windows 11. Intel Graphics and
an NVIDIA TITAN RTX are also installed. The signed Desktop Studio **0.9.2**
executable SHA-256 is
`09b06369594272e4be71a1ed11cc05fffdb78f4dd91dbadb55cde1132bfb201c`.

## Mixed show, streaming and independent recording

The actual Studio interface selected each codec. A moving media source and
stereo audio filled the 720p30 Program canvas. Publishing used 4 Mbps; independent
Program recording ran at the same time. A VDO.Ninja viewer in Edge checked
initial playback, viewer reload and publisher transport restart.

| Selection | Actual streaming encoder | Received video |
| --- | --- | --- |
| H.264 + AMD | `h264_amf` | H.264, 29.98–30.06 fps |
| AV1 + AMD | `av1_amf` | AV1, 29.97–30.06 fps |
| VP9 + Auto | `libvpx-vp9`, software | VP9, 29.96–30.05 fps |

All nine measurement windows had zero reported drops/freezes and audible track
energy. Codec and encoder identity were checked rather than inferred from the
selected option. The three independent recordings decoded fully, contained
changing pictures and stereo audio, and finalized normally. Their H.264 recording
encoder remained independent of the selected streaming codec. The original
user show and UI preferences were restored.

## HEVC is receiver-dependent

Studio started the actual AMD `hevc_amf` encoder and kept recording cleanly.
However, the tested native OBS Ninja receiver explicitly supports only H.264
and VP9: HEVC received audio but a black picture. The test correctly failed
when the recorded picture did not change. Use **H.264 for this OBS receiver**.
The separate browser test in Game Capture rejected HEVC and recovered using
H.264. Neither result counts as successful HEVC application playback.

The same bundled FFmpeg/AMD driver successfully encoded and decoded 300 frames
of H.264, HEVC and AV1 in a supplemental 720p30 check. Each had increasing
timestamps and a maximum 34 ms frame interval. That establishes the driver path,
not an end-to-end HEVC receiver. See the [shared codec results](https://github.com/steveseguin/game-capture/blob/main/docs/radeon-codecs-2026-10-16.md).

VP9 encoding uses software in these apps; explicit AMD + VP9 is unsupported.
RTMP, SRT, WHIP, RTSP, UDP and RIST output currently use H.264. AV1 and HEVC
remain experimental VDO.Ninja codec choices, not new codecs for those outputs.

## Scope and evidence

These are short local tests. AMD-only systems, other driver versions, HDR/10-bit,
game/GPU saturation and overnight operation remain separate qualification work.
[Auto selection and failure recovery](radeon-rx9060xt-validation-2026-10-16.md)
documents the implementation and previous fault/performance measurements.

`native-qt/e2e/mixer-amd-codecs-e2e.py` selects codecs through the interface and
uses the shared browser/native receiver observer. Default cases are H.264,
AV1 and VP9. Explicit HEVC requires a receiver that supports it.

Ignored evidence is in `native-qt/e2e/reports/release-0.9.2/amd-codecs-r5`.
The complete four-codec exploration remains failed because of HEVC; the three
passing cases are retained individually. Early runner failures involving UI
selectors and the GUI start confirmation remain separately recorded. Temporary
stream identifiers, local control credentials, raw logs and recordings are not
published with this summary.
