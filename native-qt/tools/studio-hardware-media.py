"""Inspect a real camera/microphone workflow's saved recording and mute intervals."""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--ffmpeg', required=True)
parser.add_argument('--capture', required=True, help='capture.json from studio-camera-mic-workflow.ps1')
parser.add_argument('--out', required=True)
args = parser.parse_args()
root = Path(args.out)
root.mkdir(parents=True, exist_ok=False)
data = json.loads(Path(args.capture).read_text(encoding='utf-8-sig'))
flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0

def run(options):
    return subprocess.run([args.ffmpeg, '-hide_banner', '-nostdin', *options],
                          capture_output=True, creationflags=flags, timeout=120)

result = run(['-v', 'error', '-xerror', '-i', data['file'], '-map', '0',
              '-fps_mode', 'passthrough', '-enc_time_base:v', 'demux', '-f', 'null', '-'])
(root / 'decode.log').write_bytes(result.stderr)
checks = [{'name': 'Saved camera and microphone recording decodes completely',
           'ok': result.returncode == 0 and not result.stderr}]
levels = {}
for name, start in [('audible', 2), ('muted', data['mutedAt'] + 1), ('restored', data['unmutedAt'] + 1)]:
    result = run(['-ss', str(start), '-i', data['file'], '-t', '3', '-vn',
                  '-af', 'volumedetect', '-f', 'null', '-'])
    log = result.stderr.decode(errors='replace')
    (root / (name + '-volume.log')).write_text(log)
    match = re.search(r'max_volume: (-?inf|[-\d.]+) dB', log)
    level = float(match.group(1)) if match else None
    levels[name] = level
    # The microphone needs audible room sound for the two unmuted samples.
    checks.append({'name': name + ' microphone sound is correct in the saved recording',
                   'ok': result.returncode == 0 and level is not None and
                   (level < -65 if name == 'muted' else level > -65)})
for label, start in [('first-frame', 2), ('last-frame', data['unmutedAt'] + 10)]:
    result = run(['-v', 'error', '-n', '-ss', str(start), '-i', data['file'],
                  '-frames:v', '1', str(root / (label + '.png'))])
    checks.append({'name': label + ' extracts from the actual camera recording', 'ok': result.returncode == 0})
summary = {'file': data['file'], 'checks': checks, 'maxVolumeDb': levels}
(root / 'results.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
sys.exit(1 if any(not check['ok'] for check in checks) else 0)
