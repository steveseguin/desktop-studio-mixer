"""Private aiortc WHIP receiver for cross-checking MediaMTX fault findings.

Install aiortc/aiohttp into .cache/mixer-receiver. Retains the actual assembled
H264 bitstream before decoding, decoded frame identities, audio and feedback.
This is a QA endpoint, bound to loopback; it is not shipped as an app service.
"""
import argparse, asyncio, json, logging, sys, time, uuid, threading
from pathlib import Path
sys.path.insert(0, str(Path('.cache/mixer-receiver').resolve()))
import aioice.ice
import av
import numpy as np
from aiohttp import web
from aiortc import RTCPeerConnection, RTCConfiguration, RTCBundlePolicy, RTCSessionDescription
from aiortc.mediastreams import MediaStreamError
import aiortc.codecs
from aiortc.jitterbuffer import JitterBuffer
from aiortc.rtcrtpreceiver import RTCRtpReceiver

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True, type=int)
parser.add_argument('--report', required=True)
args = parser.parse_args(); report = Path(args.report).resolve(); report.mkdir(parents=True, exist_ok=True)
aioice.ice.get_host_addresses = lambda use_ipv4, use_ipv6: ['127.0.0.1']
sessions = {}; current = None
logging.basicConfig(level=logging.WARNING)
OriginalDecoder = aiortc.codecs.H264Decoder
original_add = JitterBuffer.add
original_nack = RTCRtpReceiver._send_rtcp_nack

def observe_add(self, packet):
    origin = self._origin
    result = original_add(self, packet)
    if self._is_video and result[0] and current and len(current['jitterResets']) < 10000:
        current['jitterResets'].append({'wall': time.monotonic(), 'origin': origin,
            'sequence': packet.sequence_number, 'timestamp': packet.timestamp,
            'newOrigin': self._origin, 'capacity': self.capacity})
    return result

async def observe_nack(self, media_ssrc, lost):
    if current and len(current['nacks']) < 20000:
        current['nacks'].append({'wall': time.monotonic(), 'ssrc': media_ssrc, 'lost': list(lost)})
    await original_nack(self, media_ssrc, lost)

JitterBuffer.add = observe_add
RTCRtpReceiver._send_rtcp_nack = observe_nack

class ObservedDecoder(OriginalDecoder):
    def __init__(self):
        super().__init__(); self.session = current
    def decode(self, frame):
        s = self.session
        with s['lock']:
            if not s['closed']:
                s['raw'].write(frame.data)
                s['encoded'].append({'pts': frame.timestamp, 'bytes': len(frame.data), 'wall': time.monotonic()})
        return super().decode(frame)

aiortc.codecs.H264Decoder = ObservedDecoder

async def consume(track, s):
    resampler = av.AudioResampler(format='flt', layout='mono', rate=48000)
    try:
        while True:
            frame = await track.recv(); wall = time.monotonic()
            if track.kind == 'video':
                gray = frame.to_ndarray(format='gray'); scale = frame.width/640
                ident = sum(int(gray[int(8*scale):int(16*scale),int((n*20+6)*scale):int((n*20+14)*scale)].mean()>128)*(1<<n) for n in range(16))
                s['frames'].append({'pts': float(frame.pts*frame.time_base), 'wall': wall, 'id': ident})
            else:
                for audio in resampler.resample(frame):
                    values = audio.to_ndarray().ravel().astype(np.float32)
                    s['pcm'].write(values.tobytes())
                    s['audio'].append({'pts': float(audio.pts*audio.time_base), 'wall': wall, 'samples': len(values), 'rms': float(np.sqrt(np.mean(values*values)))})
            if len(s['frames']) > 30000 or len(s['audio']) > 60000: raise RuntimeError('QA capture bound reached')
    except (MediaStreamError, asyncio.CancelledError): pass

def snapshot(s):
    return {'id': s['id'], 'closed': s['closed'], 'frames': list(s['frames']), 'audio': list(s['audio']), 'encoded': list(s['encoded']),
            'jitterResets': list(s['jitterResets']), 'nacks': list(s['nacks'])}

async def close(s):
    if s['closed']: return
    await s['pc'].close()
    for task in s['tasks']: task.cancel()
    await asyncio.gather(*s['tasks'], return_exceptions=True)
    with s['lock']:
        s['closed'] = True; s['raw'].close(); s['pcm'].close()
        (report/(s['id']+'.json')).write_text(json.dumps(snapshot(s)), encoding='utf-8')

async def offer(request):
    global current
    if current and not current['closed']: await close(current)
    ident = str(uuid.uuid4()); pc = RTCPeerConnection(RTCConfiguration(iceServers=[], bundlePolicy=RTCBundlePolicy.MAX_BUNDLE))
    s = {'id': ident, 'pc': pc, 'closed': False, 'frames': [], 'audio': [], 'encoded': [], 'tasks': [], 'jitterResets': [], 'nacks': [],
         'lock': threading.Lock(), 'raw': (report/(ident+'.h264')).open('wb'), 'pcm': (report/(ident+'.f32')).open('wb')}
    sessions[ident] = s; current = s
    @pc.on('track')
    def track_arrived(track): s['tasks'].append(asyncio.create_task(consume(track, s)))
    try:
        await pc.setRemoteDescription(RTCSessionDescription(sdp=await request.text(), type='offer'))
        await pc.setLocalDescription(await pc.createAnswer())
        return web.Response(status=201, text=pc.localDescription.sdp, content_type='application/sdp', headers={'Location': '/session/'+ident})
    except Exception:
        await close(s); raise

async def delete(request):
    if request.match_info['id'] in sessions: await close(sessions[request.match_info['id']])
    return web.Response(status=200)

async def status(request):
    if not current: return web.json_response({})
    return web.json_response(snapshot(current))

async def cleanup(app):
    for s in sessions.values(): await close(s)

app = web.Application(client_max_size=1024*1024)
app.router.add_post('/whip/whip', offer)
app.router.add_options('/whip/whip', lambda request: web.Response(status=204))
app.router.add_delete('/session/{id}', delete)
app.router.add_get('/status', status)
app.on_cleanup.append(cleanup)
web.run_app(app, host='127.0.0.1', port=args.port, print=None)
