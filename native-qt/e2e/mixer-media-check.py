"""Decode an actual mixer-desktop-e2e recording and verify rendered A/V."""
import argparse
import array
import json
import math
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('recording', type=Path)
parser.add_argument('--ffmpeg', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
metadata = subprocess.run([str(args.ffmpeg), '-hide_banner', '-i', str(args.recording)],
                          capture_output=True, text=True).stderr
match = re.search(r'Duration: (\d+):(\d+):([\d.]+)', metadata)
assert match, metadata
duration = int(match[1])*3600 + int(match[2])*60 + float(match[3])
assert duration > 10 and 'Video: h264' in metadata and 'Audio: aac' in metadata, metadata
assert '1280x720' in metadata and '30 fps' in metadata and '48000 Hz, stereo' in metadata
results = {'recording': str(args.recording), 'duration': duration, 'observations': []}
for timestamp in [2, duration-1]:
    pcm = subprocess.check_output([str(args.ffmpeg), '-v', 'error', '-ss', str(timestamp),
        '-i', str(args.recording), '-t', '0.5', '-vn', '-f', 'f32le', '-ac', '1', 'pipe:1'])
    samples = array.array('f'); samples.frombytes(pcm)
    assert samples
    rms = math.sqrt(sum(sample*sample for sample in samples)/len(samples))
    rgb = subprocess.check_output([str(args.ffmpeg), '-v', 'error', '-ss', str(timestamp),
        '-i', str(args.recording), '-frames:v', '1', '-vf', 'crop=32:32:640:180,scale=1:1',
        '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'])
    results['observations'].append({'time': timestamp, 'audioRms': rms, 'rgb': list(rgb)})
first, last = results['observations']
assert first['audioRms'] > .01 and last['audioRms'] < .001, results
assert first['rgb'][1] > 130 and last['rgb'][1] < 80, results
args.report.write_text(json.dumps(results, indent=2), encoding='utf-8')
print(json.dumps(results, indent=2))
