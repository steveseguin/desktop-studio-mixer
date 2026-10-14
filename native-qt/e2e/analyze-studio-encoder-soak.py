"""Analyze a completed packaged Studio recording/publishing soak.
Run after the application exits; decoding must not overlap resource profiling.
"""
import argparse,json,statistics,re,subprocess
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('report',type=Path);p.add_argument('--ffmpeg',required=True);args=p.parse_args()
report=json.loads((args.report/'results.json').read_text());assert report.get('passed'),report.get('error')
rows=[]
for case in report['cases']:
    raw=[json.loads(s) for s in (args.report/(case['id']+'-resources.jsonl')).read_text().splitlines()]
    origin=case['samples'][0]['wall']-case['samples'][0]['seconds']
    def summarize(start,end):
        samples=[s for s in raw if start<=s['time']-origin<end]
        assert len(samples)>10
        return {'samples':len(samples),'meanSystemCpu':statistics.mean(s['systemCpuPercent'] for s in samples),
                'meanPublisherCpu':statistics.mean(s['treeCpuPercentOneCore']/s['logicalCpus'] for s in samples),
                'medianTreeRssMiB':statistics.median(s['treeRss']/2**20 for s in samples),
                'medianMainPrivateMiB':statistics.median(s['private']/2**20 for s in samples),
                'medianMainHandles':statistics.median(s['handles'] for s in samples),
                'childPids':sorted({c['pid'] for s in samples for c in s['children']})}
    finish=case['samples'][-1]['seconds'];load=case['load'];begin=load['startSeconds'];end=begin+load['requestedSeconds']
    row={'case':case['id'],'seconds':finish,'before':summarize(30,begin-10),
         'loaded':summarize(begin+10,end-5),'recovered':summarize(end+30,finish-5)}
    row['mainPrivateGrowthMiB']=row['recovered']['medianMainPrivateMiB']-row['before']['medianMainPrivateMiB']
    row['treeRssGrowthMiB']=row['recovered']['medianTreeRssMiB']-row['before']['medianTreeRssMiB']
    row['mainHandleGrowth']=row['recovered']['medianMainHandles']-row['before']['medianMainHandles']
    row['workerPidsUnchanged']=row['before']['childPids']==row['loaded']['childPids']==row['recovered']['childPids']
    row['recordingDropCounterFirstLast']=[case['samples'][0]['recordingDrops'],case['samples'][-1]['recordingDrops']]
    row['apiP95Ms']=float(np.percentile([s['apiMs'] for s in case['samples']],95))
    row['apiMaxMs']=max(s['apiMs'] for s in case['samples'])
    assert row['workerPidsUnchanged'] and row['mainPrivateGrowthMiB']<32 and row['treeRssGrowthMiB']<64 and row['mainHandleGrowth']<=10,row
    row['videoFiles']=[]
    for name in [case['id']+'.mkv',case['id']+'-local.mkv']:
        result=subprocess.run([args.ffmpeg,'-v','info','-i',str(args.report/name),'-an','-vf','scale=1:1,showinfo',
                               '-fps_mode','passthrough','-f','null','-'],capture_output=True,timeout=300,creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode==0,result.stderr[-1000:]
        times=np.array([float(v) for v in re.findall(rb'\bn:\s*\d+\s+pts:.*?pts_time:([-\d.]+)',result.stderr)])
        assert len(times)>1000 and np.all(np.diff(times)>0)
        gaps=np.diff(times);stats={'file':name,'seconds':float(times[-1]-times[0]),'frames':len(times),
            'fps':float((len(times)-1)/(times[-1]-times[0])),'p95GapMs':float(np.percentile(gaps,95)*1000),'maxGapMs':float(gaps.max()*1000)}
        assert stats['fps']>29.5 and stats['p95GapMs']<40 and stats['maxGapMs']<250,stats
        row['videoFiles'].append(stats)
    rows.append(row)
out={'sha256':report['sha256'],'passed':True,'cases':rows,'limits':'Finite ten-minute observation; tree RSS may double-count shared pages. Synthetic CPU contention is not an actual game or GPU benchmark.'}
(args.report/'soak-analysis.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
