"""Inspect every saved picture for near-static intervals in a moving reference.

Exact hashes can miss repeated source pictures after lossy re-encoding. This
companion to studio-soak-media.py measures mean absolute luma change instead.
Use only with a continuously moving reference, not a stationary camera scene.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ffmpeg', required=True)
parser.add_argument('--file', required=True)
parser.add_argument('--out', required=True)
parser.add_argument('--fps', type=float, default=30)
parser.add_argument('--threshold', type=float, default=0.25,
                    help='Mean absolute change on the 0–255 luma scale')
parser.add_argument('--crop', nargs=4, type=int, metavar=('X', 'Y', 'WIDTH', 'HEIGHT'),
                    help='Score an uncovered moving reference region before scaling')
args = parser.parse_args()
if args.crop and (min(args.crop[:2]) < 0 or min(args.crop[2:]) <= 0):
    parser.error('Crop coordinates must be nonnegative and dimensions positive')
out = Path(args.out)
out.mkdir(parents=True, exist_ok=False)
flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
width, height = 96, 54
previous = None
changes = []
frames = 0
filters = f'scale={width}:{height}'
if args.crop:
    x, y, region_width, region_height = args.crop
    filters = f'crop={region_width}:{region_height}:{x}:{y},' + filters
with (out / 'decode.log').open('wb') as log:
    process = subprocess.Popen([
        args.ffmpeg, '-hide_banner', '-nostdin', '-v', 'error', '-xerror',
        '-i', args.file, '-map', '0:v:0', '-vf', filters,
        # Keep millisecond input precision; rounding onto a 1/fps mux grid can
        # manufacture duplicate output DTS even when input PTS are increasing.
        '-fps_mode', 'passthrough', '-enc_time_base:v', 'demux', '-pix_fmt', 'gray', '-an', '-f', 'rawvideo', '-'
    ], stdout=subprocess.PIPE, stderr=log, creationflags=flags)
    try:
        while raw := process.stdout.read(width * height):
            if len(raw) != width * height:
                raise RuntimeError('Incomplete decoded frame')
            picture = np.frombuffer(raw, dtype=np.uint8).astype(np.int16)
            if previous is not None:
                changes.append(float(np.mean(np.abs(picture - previous))))
            previous = picture
            frames += 1
        code = process.wait(timeout=30)
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.kill()
            process.wait()


def intervals(threshold):
    streak = longest = near_static = 0
    episodes = []
    for index, change in enumerate(changes):
        if change <= threshold:
            streak += 1
            near_static += 1
            longest = max(longest, streak)
        else:
            if streak >= 3:
                episodes.append({'startSeconds': (index - streak) / args.fps,
                                 'seconds': (streak + 1) / args.fps})
            streak = 0
    if streak >= 3:
        episodes.append({'startSeconds': (len(changes) - streak) / args.fps,
                         'seconds': (streak + 1) / args.fps})
    return {'threshold': threshold, 'nearStaticTransitions': near_static,
            'longestSeconds': (longest + 1) / args.fps, 'episodes': episodes}


motion = intervals(args.threshold)
checks = [
    {'name': 'Every picture decodes without errors',
     'ok': code == 0 and frames > 1 and not (out / 'decode.log').read_bytes()},
    {'name': 'Moving reference has no near-static interval longer than three seconds',
     'ok': bool(changes) and motion['longestSeconds'] <= 3},
]
result = {'file': args.file, 'frames': frames, 'cropXYWH': args.crop, 'checks': checks, 'motion': motion,
          'meanAbsoluteLumaChangePercentiles':
              dict(zip(['0', '1', '5', '50', '95', '100'],
                       np.percentile(changes, [0, 1, 5, 50, 95, 100]).tolist())) if changes else {},
          'thresholdSensitivity': [intervals(t) for t in [0.1, 0.25, 0.5, 1.0]]}
(out / 'results.json').write_text(json.dumps(result, indent=2))
print(json.dumps({'file': args.file, 'frames': frames, 'cropXYWH': args.crop, 'checks': checks,
                  'motion': {k: v for k, v in motion.items() if k != 'episodes'}}, indent=2))
sys.exit(1 if any(not check['ok'] for check in checks) else 0)
