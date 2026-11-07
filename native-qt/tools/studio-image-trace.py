"""Compare synthetic picture fingerprints at capture, IPC and recording stages."""
import argparse
import bisect
import collections
import csv
import json
from pathlib import Path

import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix', required=True, help='VIDEO_MIXER_IMAGE_TRACE path used for one owned app')
p.add_argument('--out', required=True)
a = p.parse_args()
prefix = Path(a.prefix)
stages = collections.defaultdict(list)
files = sorted(prefix.parent.glob(prefix.name + '-*.csv'))
for f in files:
    with f.open() as stream:
        for row in csv.DictReader(stream):
            stages[row['stage']].append((int(row['steadyUs']), row['imageHash']))
for rows in stages.values():
    rows.sort()
required = ['wgc', 'worker', 'proxy', 'record-write']
if any(len(stages[s]) < 2 for s in required):
    raise SystemExit('Incomplete trace: need capture, worker, proxy and recording rows')
begin = max(stages[s][0][0] for s in required) + 2_000_000
end = min(stages[s][-1][0] for s in required) - 1_000_000
if end - begin < 5_000_000:
    raise SystemExit('Trace intersection must cover at least five seconds')
summary = {}
for stage in required:
    rows = [r for r in stages[stage] if begin <= r[0] <= end]
    gaps = [(y[0] - x[0]) / 1000 for x, y in zip(rows, rows[1:])]
    summary[stage] = {'frames': len(rows),
                      'sameAdjacentFingerprints': sum(x[1] == y[1] for x, y in zip(rows, rows[1:])),
                      'gapMs': dict(zip(['p50', 'p95', 'max'], np.percentile(gaps, [50, 95, 100]).tolist()))}
captured = stages['wgc']
times = [x[0] for x in captured]
lag = []
unmatched = 0
for t, fingerprint in stages['record-write']:
    if not begin <= t <= end:
        continue
    last = bisect.bisect_right(times, t)
    first = bisect.bisect_left(times, t - 1_000_000)
    matches = [(t - captured[i][0]) / 1000 for i in range(first, last)
               if captured[i][1] == fingerprint]
    if matches:
        lag.append(min(matches))
    else:
        unmatched += 1
result = {'files': [str(f) for f in files], 'seconds': (end - begin) / 1e6,
          'stages': summary, 'unmatchedRecordingFrames': unmatched,
          'captureToRecordingMs': dict(zip(['p50', 'p95', 'max'], np.percentile(lag, [50, 95, 100]).tolist())) if lag else {},
          'meaning': 'Sampled fingerprints for an owned moving reference. Not a pixel-completeness or network-latency measurement.'}
out = Path(a.out)
if out.exists():
    raise SystemExit('Preserve existing evidence; use a new output file')
out.write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
