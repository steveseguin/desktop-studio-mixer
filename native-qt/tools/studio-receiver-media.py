"""Decode a received recording and verify steady A/V around deliberate disruptions."""
import argparse
import json
import subprocess
import sys
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--ffmpeg', required=True)
p.add_argument('--capture', required=True)
p.add_argument('--out', required=True)
a = p.parse_args()
d = json.loads(Path(a.capture).read_text(encoding='utf-8-sig'))
out = Path(a.out)
out.mkdir(parents=True, exist_ok=False)
flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0


def run(options):
    return subprocess.run([a.ffmpeg, '-hide_banner', '-nostdin', '-n', *options],
                          capture_output=True, creationflags=flags, timeout=180)


checks = []
decode = run(['-v', 'error', '-xerror', '-i', d['file'], '-map', '0',
              '-fps_mode', 'passthrough', '-enc_time_base:v', 'demux', '-f', 'null', '-'])
(out / 'decode.log').write_bytes(decode.stderr)
checks.append({'name': 'Complete received picture/audio decode', 'ok': decode.returncode == 0 and not decode.stderr.strip()})
events = d['events']
clips = [('before-action', 2, 15)]
if 'resumeEpoch' in events:
    clips.append(('after-resume', events['resumeEpoch'] - d['startedEpoch'] + 2, 8))
    audio = run(['-v', 'error', '-i', d['file'], '-map', '0:a:0', '-ac', '1', '-ar', '48000', '-f', 'f32le', '-'])
    wave = np.frombuffer(audio.stdout, np.float32)
    begin = events['pauseEpoch'] - d['startedEpoch'] + .6
    end = events['resumeEpoch'] - d['startedEpoch'] - .3
    silent = wave[int(begin*48000):int(end*48000)]
    peak = float(np.max(np.abs(silent))) if len(silent) else None
    checks.append({'name': 'Paused browser audio is silent in the recording', 'peak': peak,
                   'ok': audio.returncode == 0 and not audio.stderr.strip() and len(silent) >= 24000 and peak < .001})
if 'reconnectedEpoch' in events:
    clips.append(('after-reconnect', events['reconnectedEpoch'] - d['startedEpoch'] + 3, 8))
samples = []
for label, start, duration in clips:
    clip = out / (label + '.mkv')
    extract = run(['-v', 'error', '-ss', str(start), '-i', d['file'], '-t', str(duration),
                   '-map', '0:v:0', '-map', '0:a:0', '-c:v', 'ffv1', '-c:a', 'pcm_s16le', str(clip)])
    (out / (label + '-extract.log')).write_bytes(extract.stderr)
    check = {'name': label + ' picture/audio align within 100 ms', 'ok': False}
    if extract.returncode == 0:
        probe = subprocess.run([sys.executable, str(Path(__file__).with_name('studio-journey-media.py')),
                                '--ffmpeg', a.ffmpeg, str(clip)], capture_output=True, creationflags=flags, timeout=180)
        (out / (label + '-probe.log')).write_bytes(probe.stdout + probe.stderr)
        path = Path(str(clip) + '.analysis.json')
        if probe.returncode == 0 and path.exists():
            result = json.loads(path.read_text())
            samples.append(result)
            check['ok'] = (result['videoFlashes'] >= 3 and result['audioPulses'] >= 3
                           and result['maxAbsSyncOffsetMs'] is not None and result['maxAbsSyncOffsetMs'] <= 100)
    checks.append(check)
result = {'file': d['file'], 'checks': checks, 'avSamples': samples}
(out / 'results.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
sys.exit(1 if any(not c['ok'] for c in checks) else 0)
