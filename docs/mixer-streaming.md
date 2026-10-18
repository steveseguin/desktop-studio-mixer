---
layout: default
title: Streaming and encoder settings
---

[Desktop Studio](../) · [User guide](mixer-guide.md) · [Streaming](mixer-streaming.md) · [Technical details](mixer-technical.md)

# Streaming and encoder settings

## Codec compatibility

H.264 is the default and the safest choice for OBS and external destinations.
On the RX 9060 XT, AMD H.264 and AV1 passed actual Studio playback with independent
recording; VP9 uses software. HEVC encoded correctly in the driver check, but
the tested native OBS Ninja receiver does not support it. See the
[codec results and receiver limits](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-radeon-codecs-2026-10-16.md).

## Streaming defaults, direct receivers and microphones

New streaming sessions in the next Windows build default to **3,000 kbps video** and **128 kbps audio**. The encoder ceiling follows the selected target. Saved bitrate choices and explicit CLI/API overrides are retained. This is a video encoding target, not a total upload cap: audio, transport headers, retransmissions and extra VDO.Ninja viewers add traffic. Independent Studio recording keeps its separate quality settings. Higher streaming bitrates remain in advanced settings. See the [complete default policy](streaming-defaults.md).

**UDP / MPEG-TS (LAN)** and **RIST** are under advanced Output settings. Both carry H.264 video and AAC audio using the bundled FFmpeg worker. They also accept `--output=udp` / `--output=rist` and `--output-url=...` in headless capture. Start a receiver first, for example:

```powershell
# Sender URL: udp://RECEIVER-IP:5000
ffplay -i "udp://0.0.0.0:5000?fifo_size=8192&overrun_nonfatal=1"
# Sender URL: rist://RECEIVER-IP:8194; use matching 200 ms buffers
ffplay -buffer_size 200 -i "rist://@0.0.0.0:8194"
```

OBS Media Source and VLC can receive the MPEG-TS UDP stream. UDP provides no loss recovery or delivery acknowledgement; a Live status means the sender is transmitting. RIST uses Main profile and a configurable recovery buffer (20–8,000 ms, 200 ms UI default). Match the receiving buffer to the sender. This RIST integration does not configure encryption; use a trusted link/VPN or choose SRT with a passphrase. See the [FFmpeg protocol documentation](https://ffmpeg.org/ffmpeg-protocols.html#rist) for receiver options. The receiving application can add startup buffering of its own.

Standalone capture starts with **No microphone**, including when switching to Camera. Choosing a microphone enables it; the adjacent **Mute / Unmute** button stays usable while live and preserves the selected gain. Muting gates microphone samples before primary/additional routing, so it covers microphone-only audio and a microphone mixed with game audio. It does not stop the device; choose **No microphone** before the next capture to avoid opening it. Microphone selection and mute state are saved. Studio mixer microphones remain explicit sources with their existing per-track Mute buttons; the standalone microphone controls are disabled for Mixer program.

**AMD AMF** explicitly uses FFmpeg's AMD encoder instead of requiring an AMD Media Foundation encoder registration. Its defaults select low-latency CBR, balanced quality and a single queued frame. H.264 disables B-frame reordering, forces true IDRs at the requested GOP interval (at most one second in low-latency mode), and preserves AMD's live headers without injecting stale startup headers. HEVC uses AMF's own access-unit delimiters, avoiding a metadata filter that can reject incomplete AMD startup extradata. These policies are shared with Game Capture's AMD fixes. Settings follow [AMD's AMF guide](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/AMF-Encoder-Settings-and-Tuning-in-FFmpeg) and the [FFmpeg AMF implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/amfenc_h264.c). Explicit AMD selection fails visibly when unavailable. Auto now tries AMD AMF before software if an AMD adapter is present but no usable hardware Media Foundation encoder is available. See the [RX 9060 XT application and fallback validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/radeon-rx9060xt-validation-2026-10-16.md) for actual hardware coverage and remaining limits.

Process audio capture prefers the default output device's sample rate when Windows does not report a process-loopback format. Missing or invalid device information uses 48 kHz. Rates such as 22.05 kHz that require fractional frames in a 10 ms capture buffer also use 48 kHz, avoiding periodic silent buffers observed with Windows process capture. If Windows rejects another rate, capture retries once at 48 kHz with a fresh audio client. Native microphone formats keep their normal conversion path. Mixed/output audio remains 48 kHz; no Windows device setting is changed.

Software H.264 on Windows uses Media Foundation, rather than x264. Its default now favors low encoding complexity and uses at most four encoder workers (at most half the logical processors, minimum one). Native H.264 explicitly requests CBR with a one-second encoder buffer; hardware-specific FFmpeg encoders keep their low-latency settings. Encoder rate control is not a network pacer or a total upload limit.

Fresh window-capture and new-show settings use **1280x720 at 30 fps** with hardware-first Auto encoding. Camera capture retains its separate **1080p30** default. Higher resolutions/frame rates and custom bitrates remain under Advanced; saved selections are preserved, including previously saved 12 Mbps values. **Balanced (3000 kbps)** is the streaming default, with **Economy (2500 kbps)** retained for smaller upload budgets. Software encoding cannot guarantee spare CPU during a demanding game. Independent local recording and guest-return streams add their own encoding work.

Packaged measurements, CPU contention results and remaining hardware limits: [encoding defaults validation](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/encoding-defaults-validation-2026-10-14.md).

## RTSP and VRChat output

Advanced output settings offer **RTSP / VRChat (TCP)** and **RTSP (UDP)**, alongside RTMP/RTMPS, SRT and WHIP. RTSP sends H.264/AAC to a publishing server; TCP also accepts `rtspt://`. Use the server's viewing link in VRChat. See the [setup and automation guide](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/rtsp-vrchat-output.md).

## Publishing recovery

WHIP uses WebRTC media negotiation; it has no universal error-correction percentage. Desktop Studio offers recovery automatically and uses the mechanisms accepted by the destination. **Output settings > Advanced** exposes the following controls; stop publishing before changing them. Output profiles remember them.

| Mechanism | Behavior and control |
|---|---|
| **NACK** | The receiver asks for missing video packets. The sender retains **1500 ms** of history by default, adjustable **250-3000 ms**, with hard limits of 4096 packets / 8 MiB. Increasing history gives late requests more time; it does not increase the receiver's own buffer. |
| **RTX** | Offered automatically. When accepted, repairs have a separate RTP sequence space and SSRC. Otherwise negotiated NACK uses original-packet retransmission. |
| **PLI** | The receiver requests a fresh keyframe when it cannot recover a picture. Requests are limited to one every 250 ms to prevent a keyframe storm. |
| **Playback smoothing** | Requests a **500 ms** minimum video playback delay, allowing time for repairs before display. Adjustable **0-1000 ms**; **0 / Receiver default** omits the hint. Only sent when the receiver accepts the playout-delay extension. This is a best-effort receiver hint, not a guarantee of end-to-end latency. |
| **Audio RED** | **On by default as an offer.** When selected by the receiver, each Opus packet includes one previous packet when both fit within 1200 bytes. This can nearly double audio payload bandwidth; it does not double video bandwidth. Disable the offer in Advanced if necessary. Unsupported destinations receive plain Opus. |
| **Video FEC / Opus in-band FEC** | Not implemented in this sender. RED provides redundant audio, but there is no video parity/FlexFEC/ULPFEC or adjustable FEC percentage. An SDP answer advertising Opus `useinbandfec=1` does not mean this encoder generates it. |

Video packets are paced to reduce bursts. Repairs bypass that queue, with a bounded repair budget and duplicate-request suppression. A stalled pacing queue reconnects only that destination. Recovery status shows negotiated NACK/RTX/PLI, RED selection, playback hint, retransmission count and cache misses. The retransmission counter counts repairs handed to the transport; it does not prove the destination recovered them. The receiving service still determines its NACK timing, reorder window, decoding and final playback buffer.

These controls do not implement automatic bitrate adaptation. Leave upload headroom for repairs; increasing redundancy on a saturated connection can make loss worse. A larger sender history cannot fix a receiver that discards repairs before they arrive. The [recovery validation notes](https://github.com/steveseguin/desktop-studio-mixer/blob/main/docs/mixer-0.9.0-followup-validation.md#publishing-recovery-implementation-and-receiver-cross-checks) distinguish browser/independent-receiver results from the remaining MediaMTX loss and RTSP startup-timestamp issues.

SRT's latency control sets its retransmission window. After a prolonged connection setup or reconnect, Studio refreshes a stale media session once to avoid carrying several seconds of old video into the recovered stream. This retry is bounded; independent recording continues through it. No transport can recover an arbitrarily long outage without interruption.

Protocol references: [WHIP](https://www.rfc-editor.org/rfc/rfc9725.html), [NACK/PLI](https://www.rfc-editor.org/rfc/rfc4585.html), [RTX](https://www.rfc-editor.org/rfc/rfc4588.html), [audio RED](https://www.rfc-editor.org/rfc/rfc2198.html), and [playout-delay guidance](https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/playout-delay/).

