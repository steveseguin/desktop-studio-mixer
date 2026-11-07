"""Check recorded pause, scene silence and A/V after real transport actions."""
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
data = json.loads(Path(a.capture).read_text(encoding='utf-8-sig'))
out = Path(a.out)
out.mkdir(parents=True, exist_ok=False)
events = data['events']
flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
checks = []


def run(options):
    return subprocess.run([a.ffmpeg, '-hide_banner', '-nostdin', *options],
                          capture_output=True, creationflags=flags, timeout=120)


decode = run(['-v', 'error', '-xerror', '-i', data['file'], '-map', '0',
              '-fps_mode', 'passthrough', '-enc_time_base:v', 'demux', '-f', 'null', '-'])
(out / 'decode.log').write_bytes(decode.stderr)
checks.append({'name': 'The complete transport recording decodes cleanly',
               'ok': decode.returncode == 0 and not decode.stderr.strip()})
audio = run(['-v', 'error', '-i', data['file'], '-map', '0:a:0',
             '-ac', '1', '-ar', '48000', '-f', 'f32le', '-'])
wave = np.frombuffer(audio.stdout, np.float32)
checks.append({'name': 'The complete recorded audio decodes',
               'ok': audio.returncode == 0 and not audio.stderr.strip() and len(wave) > 0})
silence = {}
for label, start, end in [('pause', events['pausedAt'], events['resumedAt']),
                          ('break scene', events['slateAt'], events['returnedAt'])]:
    # Exclude UI observation and the short scene-audio fade at either boundary.
    segment = wave[int((start + .6) * 48000):int((end - .3) * 48000)]
    peak = float(np.max(np.abs(segment))) if len(segment) else None
    silence[label] = peak
    checks.append({'name': label + ' is silent in the saved recording',
                   'ok': peak is not None and len(segment) >= 24000 and peak < .001})

samples = []
for label, start, duration in [('initial', 1, 7),
                               ('resumed', events['resumedAt'] + 2, 7),
                               ('reconnected', events['reconnectedAt'] + 2, 5),
                               ('returned', events['returnedAt'] + 1, 5)]:
    clip = out / (label + '.mkv')
    cut = run(['-v', 'error', '-ss', str(start), '-i', data['file'], '-t', str(duration),
               '-map', '0:v:0', '-map', '0:a:0', '-c:v', 'ffv1', '-c:a', 'pcm_s16le', str(clip)])
    (out / (label + '-extract.log')).write_bytes(cut.stderr)
    check = {'name': label + ' picture and audio remain aligned within 100 ms', 'ok': False}
    if cut.returncode == 0:
        probe = subprocess.run([sys.executable, str(Path(__file__).with_name('studio-journey-media.py')),
                                '--ffmpeg', a.ffmpeg, str(clip)], capture_output=True,
                               creationflags=flags, timeout=120)
        (out / (label + '-probe.log')).write_bytes(probe.stdout + probe.stderr)
        result_file = Path(str(clip) + '.analysis.json')
        if probe.returncode == 0 and result_file.exists():
            result = json.loads(result_file.read_text())
            samples.append(result)
            check['ok'] = (result['videoFlashes'] >= 3 and result['audioPulses'] >= 3
                           and result['maxAbsSyncOffsetMs'] is not None
                           and result['maxAbsSyncOffsetMs'] <= 100)
    checks.append(check)
result = {'file': data['file'], 'checks': checks, 'silencePeaks': silence, 'avSamples': samples}
(out / 'results.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
sys.exit(1 if any(not check['ok'] for check in checks) else 0)
