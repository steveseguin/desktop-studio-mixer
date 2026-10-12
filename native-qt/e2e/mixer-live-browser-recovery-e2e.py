"""Suspend one browser host in an already-running private QA show.

Requires explicit --session and --source. Does not stop the app or other sources.
"""
import argparse,json,time,urllib.request
from pathlib import Path
import psutil
p=argparse.ArgumentParser();p.add_argument('--session',required=True);p.add_argument('--source',required=True);p.add_argument('--report',required=True)
a=p.parse_args();session=json.loads(Path(a.session).read_bytes());d=json.loads(Path(session['discovery']).read_bytes());root=psutil.Process(session['pid'])
def state():
    req=urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})
    with urllib.request.urlopen(req,timeout=15) as r:return json.load(r)
def browser():
    for child in root.children():
        try:
            if '--studio-worker' in child.cmdline() and 'browser' in child.cmdline() and any(a.source in ' '.join(p.cmdline()) for p in child.children(recursive=True)):return child
        except psutil.Error:pass
    return None
victim=browser();assert victim is not None
before=state();assert before['recording']['active'] and before['output']['live']
report=Path(a.report);report.mkdir(parents=True,exist_ok=True)
results={'oldPid':victim.pid,'samples':[]};start=time.monotonic();victim.suspend()
try:
    replacement=None
    while time.monotonic()-start<55:
        s=state();source=next(v for v in s['sources'] if v['id']==a.source)
        results['samples'].append({'seconds':time.monotonic()-start,'frames':source['frames'],'recording':s['recording'],'output':s['output']})
        assert s['recording']['active'] and s['output']['live']
        current=browser()
        if not victim.is_running() and current is not None and current.pid!=victim.pid:
            replacement=current;break
        time.sleep(.25)
    assert replacement is not None,'Watchdog did not replace the hung browser'
    frames=source['frames'];deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        s=state();source=next(v for v in s['sources'] if v['id']==a.source)
        if source['frames']>frames+20 and source['peak']>.01:break
        time.sleep(.25)
    assert source['frames']>frames+20 and source['peak']>.01
    assert s['recording']['active'] and s['output']['video_packets']>before['output']['video_packets']
    results.update(passed=True,recoverySeconds=time.monotonic()-start,newPid=replacement.pid,videoAndAudioResumed=True,outputsContinued=True)
finally:
    if victim.is_running():victim.resume()
    (report/'browser-hang.json').write_text(json.dumps(results,indent=2));print(json.dumps({k:v for k,v in results.items() if k!='samples'},indent=2))
