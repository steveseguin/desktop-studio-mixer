"""Decode a complete saved recording; check motion and sample A/V at both ends."""
import argparse, json, re, subprocess, sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--ffmpeg', required=True)
parser.add_argument('--file', required=True)
parser.add_argument('--out', required=True)
parser.add_argument('--expected-seconds', required=True, type=float)
parser.add_argument('--expected-fps', type=float, default=30)
args = parser.parse_args()
out = Path(args.out)
out.mkdir(parents=True, exist_ok=False)
flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0

def ffmpeg(options):
    return subprocess.run([args.ffmpeg, '-hide_banner', '-nostdin', '-n', *options],
                          capture_output=True, creationflags=flags, timeout=1800)

# Decode every video frame and every audio sample. Hash each scaled frame so
# repeated pictures cannot hide behind otherwise regular mux timestamps.
hashes_file = out / 'frames.md5'
result = ffmpeg(['-v', 'error', '-xerror', '-progress', 'pipe:1', '-i', args.file,
                 '-map', '0:v:0', '-vf', 'scale=96:54', '-fps_mode', 'passthrough',
                 '-enc_time_base:v', 'demux', '-an', '-f', 'framemd5', str(hashes_file),
                 '-map', '0:a', '-vn', '-f', 'null', '-'])
log = result.stderr.decode(errors='replace')
(out / 'decode.log').write_text(log)
progress = result.stdout.decode(errors='replace')
(out / 'decode-progress.txt').write_text(progress)
times = re.findall(r'^out_time_us=(\d+)', progress, re.M)
duration = int(times[-1]) / 1e6 if times else 0
hashes = [line.split(',')[-1].strip() for line in hashes_file.read_text().splitlines()
          if line and not line.startswith('#')] if hashes_file.exists() else []
longest = streak = repeated = 0
previous = None
for value in hashes:
    streak = streak + 1 if value == previous else 1
    repeated += value == previous
    longest = max(longest, streak)
    previous = value
checks = [
    {'name': 'All video and audio decode without errors', 'ok': result.returncode == 0 and not log.strip()},
    {'name': 'Saved duration matches the observed recording window within three seconds',
     'ok': abs(duration - args.expected_seconds) < 3},
    {'name': 'The moving reference has no unchanged interval longer than three seconds',
     'ok': len(set(hashes)) > 10 and longest / args.expected_fps <= 3},
    {'name': 'Decoded frames cover the saved duration at the expected cadence',
     'ok': abs(len(hashes) / args.expected_fps - duration) < .2},
]
summary = {'file': args.file, 'durationSeconds': duration, 'expectedSeconds': args.expected_seconds,
           'framesSampled': len(hashes), 'uniqueFrames': len(set(hashes)),
           'longestUnchangedFrames': longest, 'longestUnchangedSeconds': longest / args.expected_fps,
           'identicalAdjacentFrames': repeated, 'identicalAdjacentPercent': 100 * repeated / len(hashes) if hashes else None,
           'decodeExit': result.returncode, 'checks': checks}
# Stream-copy packet timestamps keep container precision and expose gaps across
# the entire file, independently of the decoded motion hashes.
packet_file = out / 'video-packets.crc'
packet_result = ffmpeg(['-v', 'error', '-copyts', '-i', args.file, '-map', '0:v:0',
                        '-c:v', 'copy', '-f', 'framecrc', str(packet_file)])
(out / 'packet-inspection.log').write_bytes(packet_result.stderr)
packet_text = packet_file.read_text() if packet_file.exists() else ''
time_base = re.search(r'#tb\s+0:\s*(\d+)/(\d+)', packet_text)
presentation_times = []
if time_base:
    scale = int(time_base.group(1)) / int(time_base.group(2))
    for line in packet_text.splitlines():
        if line and not line.startswith('#'):
            fields = line.split(',')
            try:
                presentation_times.append(int(fields[2]) * scale)
            except (ValueError, IndexError):
                checks.append({'name': 'Every video packet has a usable timestamp', 'ok': False})
presentation_times.sort()  # Presentation order also works for reordered codecs.
gaps = [b - a for a, b in zip(presentation_times, presentation_times[1:])]
span = presentation_times[-1] - presentation_times[0] if len(presentation_times) > 1 else 0
effective_fps = (len(presentation_times) - 1) / span if span else 0
summary.update(videoPackets=len(presentation_times), effectiveVideoFps=effective_fps,
               maxPresentationGapMs=max(gaps) * 1000 if gaps else None,
               duplicatePresentationTimes=sum(gap <= 0 for gap in gaps))
checks.extend([
    {'name': 'The complete video packet timeline has no duplicates or gaps over 100 ms',
     'ok': packet_result.returncode == 0 and not packet_result.stderr and bool(gaps)
           and min(gaps) > 0 and max(gaps) <= .1001},
    {'name': 'Video packet cadence matches the expected frame rate',
     'ok': abs(effective_fps - args.expected_fps) < .05},
])
if result.returncode == 0 and duration > 120:
    clips = []
    for label, start in [('first-minute', 0), ('last-minute', duration - 60)]:
        clip = out / (label + '.mkv')
        cut = ffmpeg(['-v', 'error', '-ss', str(start), '-i', args.file, '-t', '60',
                      '-map', '0:v:0', '-map', '0:a:0', '-c', 'copy', '-avoid_negative_ts', 'make_zero', str(clip)])
        if cut.returncode:
            checks.append({'name': label + ' A/V sample extracts successfully', 'ok': False})
        else:
            clips.append(str(clip))
    if clips:
        analysis = subprocess.run([sys.executable, str(Path(__file__).with_name('studio-journey-media.py')),
                                   '--ffmpeg', args.ffmpeg, *clips], capture_output=True,
                                  creationflags=flags, timeout=300)
        (out / 'av-analysis.log').write_text(analysis.stdout.decode(errors='replace') + analysis.stderr.decode(errors='replace'))
        checks.append({'name': 'Both end samples complete pulse/frame analysis', 'ok': len(clips) == 2 and analysis.returncode == 0})
        summary['avSamples'] = [json.loads(Path(c + '.analysis.json').read_text()) for c in clips if Path(c + '.analysis.json').exists()]
        checks.append({'name': 'Both end samples produce analysis results', 'ok': len(summary['avSamples']) == 2})
        for sample in summary['avSamples']:
            checks.append({'name': sample['file'] + ' has readable pulse audio/video within 100 ms',
                           'ok': sample['fullDecodeExit'] == 0 and not sample['decodeLog'].strip()
                           and sample['videoFlashes'] > 2 and sample['audioPulses'] > 2
                           and sample['maxAbsSyncOffsetMs'] is not None
                           and sample['maxAbsSyncOffsetMs'] <= 100})
(out / 'results.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
sys.exit(1 if any(not c['ok'] for c in checks) else 0)
