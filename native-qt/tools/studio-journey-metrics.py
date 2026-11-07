import argparse, json, os, time, urllib.request
from pathlib import Path
import psutil
p=argparse.ArgumentParser();p.add_argument('--session',required=True);p.add_argument('--output',required=True);a=p.parse_args()
folder=Path(a.output);folder.mkdir(parents=True,exist_ok=True)
previous={};last=time.monotonic();samples=[];psutil.cpu_percent()
while not (folder/'stop').exists():
    start=time.monotonic();sample={'utc':time.time(),'phase':(folder/'phase.txt').read_text().strip() if (folder/'phase.txt').exists() else 'unlabelled'}
    try:
        session=json.loads(Path(a.session).read_text(encoding='utf-8-sig'));root=psutil.Process(session['pid']);current={};cpu=0;private=0;roles={}
        for proc in [root,*root.children(recursive=True)]:
            try:
                key=(proc.pid,proc.create_time());ticks=proc.cpu_times();current[key]=ticks.user+ticks.system
                delta=max(0,current[key]-previous.get(key,current[key]));cpu+=delta
                role='main' if proc.pid==root.pid else proc.name()
                args=proc.cmdline()
                if '--studio-worker' in args:role=args[args.index('--studio-worker')+1]
                roles[role]=roles.get(role,0)+delta
                private+=proc.memory_info().private
            except psutil.Error:pass
        elapsed=start-last;previous=current
        sample.update(pid=root.pid,cpuPercentHost=100*cpu/elapsed/(psutil.cpu_count() or 1),privateMiB=private/1048576,processes=len(current),roleCpuPercentHost={k:100*v/elapsed/(psutil.cpu_count() or 1) for k,v in roles.items()},systemCpuPercent=psutil.cpu_percent())
        d=json.loads(Path(session['control']).read_text(encoding='utf-8-sig'))
        req=urllib.request.Request(d['base_url'].rstrip('/')+'/studio/state',headers={'Authorization':'Bearer '+d['token']})
        before=time.monotonic()
        with urllib.request.urlopen(req,timeout=2) as response:s=json.load(response)
        sample.update(apiMs=1000*(time.monotonic()-before),recording=s['recording'],output=s['output'],canvas=s['canvas'],sourceFrames=[{'kind':v['kind'],'frames':v['frames'],'error':v['error'],'peak':v['peak']} for v in s['sources']])
    except Exception as e:sample['unavailable']=type(e).__name__
    last=start;samples.append(sample)
    (folder/'samples.json').write_text(json.dumps({'logicalProcessors':psutil.cpu_count(),'samples':samples},indent=2))
    time.sleep(max(.05,1-(time.monotonic()-start)))
print('Saved %d resource samples'%len(samples))
