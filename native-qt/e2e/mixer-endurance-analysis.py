"""Analyze a completed endurance recording, including its synchronized flash/tone.

No live app or device access. Keeps measurements beside private input media.
"""
import argparse,json,re,subprocess
from pathlib import Path
import numpy as np

p=argparse.ArgumentParser();p.add_argument('--report',required=True)
p.add_argument('--ffmpeg',default='dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe')
a=p.parse_args();report=Path(a.report).resolve();media=report/'hour-program.mkv'
def ff(*args,timeout=600):
    r=subprocess.run([a.ffmpeg,'-v','error',*map(str,args)],capture_output=True,
                     creationflags=subprocess.CREATE_NO_WINDOW,timeout=timeout)
    assert r.returncode==0 and not r.stderr,r.stderr.decode(errors='replace')
    return r.stdout

hashes=ff('-i',media,'-an','-vf','scale=48:27','-fps_mode','passthrough',
          '-enc_time_base','1:1000000','-f','framemd5','-').decode()
rows=[line.split(',') for line in hashes.splitlines() if line and not line.startswith('#')]
pts=np.array([int(row[2]) for row in rows])/1e6;intervals=np.diff(pts)
assert len(pts)>30000 and np.all(intervals>0)
results={'video':{'frames':len(pts),'firstPts':float(pts[0]),'lastPts':float(pts[-1]),
    'averageFps':float((len(pts)-1)/(pts[-1]-pts[0])),
    'maximumFrameIntervalMs':float(intervals.max()*1000),
    'intervalsOver50ms':int(np.sum(intervals>.05)),
    'distinctSmallFrames':len(set(row[-1] for row in rows))}}

def av_window(start):
    # Resampling onto a 30 Hz timeline retains PTS gaps as repeated pictures.
    # The corner flash is driven by the same AudioContext clock as the 1 kHz beep.
    video=ff('-ss',start,'-i',media,'-t','20','-an','-vf',
             'crop=32:32:8:8,scale=1:1,fps=30,format=gray','-f','rawvideo','-')
    light=np.frombuffer(video,dtype=np.uint8)>150
    visual=np.where(np.diff(light.astype(int))==1)[0]/30+1/30
    raw=ff('-ss',start,'-i',media,'-t','20','-vn','-ac','1','-ar','48000','-f','f32le','-')
    audio=np.frombuffer(raw,dtype='<f4');audio=audio[:len(audio)//480*480].reshape(-1,480)
    basis=np.exp(-2j*np.pi*1000*np.arange(480)/48000)
    tone=np.abs(audio@basis)*2/480
    sounding=tone>max(.025,float(np.percentile(tone,95))*.4)
    audible=np.where(np.diff(sounding.astype(int))==1)[0]/100+.01
    lags=[float(t-audible[np.argmin(np.abs(audible-t))]) for t in visual if len(audible)]
    lags=[lag for lag in lags if abs(lag)<.4]
    assert len(lags)>=14,(start,visual.tolist(),audible.tolist())
    return {'startSeconds':start,'matchedEvents':len(lags),'medianVideoMinusAudioMs':float(np.median(lags)*1000),
            'minimumLagMs':min(lags)*1000,'maximumLagMs':max(lags)*1000,
            'tone95PercentileAmplitude':float(np.percentile(tone,95))}

results['avWindows']=[av_window(60),av_window(round(float(pts[-1])-40))]
results['avDriftMs']=results['avWindows'][-1]['medianVideoMinusAudioMs']-results['avWindows'][0]['medianVideoMinusAudioMs']
results['avDriftWithin100ms']=abs(results['avDriftMs'])<100
run=json.loads((report/'endurance.json').read_text());samples=run['samples']
late=[s for s in samples if s['seconds']>=2400]
cpu=[(b['cpuSeconds']-c['cpuSeconds'])/(b['seconds']-c['seconds'])*100 for c,b in zip(late,late[1:])
     if b['cpuSeconds']>=c['cpuSeconds'] and b['processes']==c['processes']]
if late:
    results['last20Minutes']={'samples':len(late),'medianPrivateMiB':float(np.median([s['privateMiB'] for s in late])),
        'minPrivateMiB':min(s['privateMiB'] for s in late),'maxPrivateMiB':max(s['privateMiB'] for s in late),
        'privateMiBPerMinuteSlope':float(np.polyfit([s['seconds']/60 for s in late],[s['privateMiB'] for s in late],1)[0]),
        'medianTreeCpuPercentOneCore':float(np.median(cpu)),
        'p95TreeCpuPercentOneCore':float(np.percentile(cpu,95)),
        'p95StateAndProcessSamplingMs':float(np.percentile([s['apiLatencyMs'] for s in late],95)),
        'recordingDroppedAtStart':late[0]['recording']['dropped_frames'],
        'recordingDroppedAtEnd':late[-1]['recording']['dropped_frames']}
results['passed']=bool(results['avDriftWithin100ms'] and pts[-1]>3500)
(report/'analysis.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
assert results['passed'],results
