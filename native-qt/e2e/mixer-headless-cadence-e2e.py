"""Measure real headless recording cadence, optionally probing Windows timer policy.
The optional Frida probe changes only this owned test process, never app files.
"""
from pathlib import Path
source=Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0]
source=source.replace('args=parser.parse_args()',"parser.add_argument('--fixture',required=True)\nparser.add_argument('--timer-experiment',action='store_true')\nargs=parser.parse_args()")
exec(source)
import re
root=None;session=None
results={'sha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest()}
previous_cpu={}
def tree_cpu():
    total=0.;now=time.monotonic()
    for process in [root]+root.children(recursive=True):
        try:
            ticks=process.cpu_times();ticks=ticks.user+ticks.system
            key=(process.pid,process.create_time());last=previous_cpu.get(key)
            if last:total+=(ticks-last[0])/(now-last[1])*100/psutil.cpu_count()
            previous_cpu[key]=(ticks,now)
        except psutil.NoSuchProcess:pass
    return total
try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    command('monitor.set',enabled=False)
    sid=command('source.add',kind='media',name='Frame clock',locator=str(Path(args.fixture).resolve()))['id']
    until(lambda:state()['sources'][0]['frames']>30,'source frames')
    command('layer.set',source=sid,x=0,y=0,w=1,h=1)
    output=report/'headless.mkv';command('record.start',path=str(output))
    until(lambda:state()['recording']['program_active'],'recording')
    started=time.monotonic();samples=[];switched=False;tree_cpu()
    while time.monotonic()-started<60:
        elapsed=time.monotonic()-started
        if args.timer_experiment and elapsed>=30 and not switched:
            import frida
            session=frida.attach(root.pid)
            script=session.create_script(r"""
const winmm=Process.getModuleByName('winmm.dll');
const kernel=Process.getModuleByName('kernel32.dll');
const begin=new NativeFunction(winmm.getExportByName('timeBeginPeriod'),'uint',['uint']);
const set=new NativeFunction(kernel.getExportByName('SetProcessInformation'),'int',['pointer','uint','pointer','uint']);
const self=new NativeFunction(kernel.getExportByName('GetCurrentProcess'),'pointer',[]);
rpc.exports={enable(){const p=Memory.alloc(12);p.writeU32(1);p.add(4).writeU32(4);p.add(8).writeU32(0);
return {beginResult:begin(1),policyAccepted:!!set(self(),4,p,12)};}};
""")
            script.load();results['experiment']=script.exports_sync.enable();switched=True
            results['experiment']['seconds']=time.monotonic()-started;print(results['experiment'],flush=True)
        s=state();assert s['recording']['program_active']
        samples.append({'seconds':elapsed,'sourceFrames':s['sources'][0]['frames'],'drops':s['recording']['dropped_frames'],'treeCpu':tree_cpu()})
        time.sleep(.5)
    command('record.stop');until(lambda:not state()['recording']['busy'],'recording saved')
    if session:session.detach();session=None
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(children,timeout=10);assert not alive
    decoded=run([ffmpeg,'-v','error','-xerror','-i',output,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=120)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    decoded=run([ffmpeg,'-v','info','-i',output,'-an','-vf','scale=640:360,crop=320:24:0:0,format=gray,showinfo',
                 '-fps_mode','passthrough','-f','rawvideo','-'],timeout=120)
    assert decoded.returncode==0
    pts=np.array([float(v) for v in re.findall(rb'\bn:\s*\d+\s+pts:.*?pts_time:([-\d.]+)',decoded.stderr)])
    frames=np.frombuffer(decoded.stdout,np.uint8).reshape(-1,24,320);assert len(pts)==len(frames)
    ids=sum((frames[:,8:16,n*20+6:n*20+14].mean(axis=(1,2))>128).astype(np.int64)*(1<<n) for n in range(16))
    assert np.all(ids<1800) and np.all(np.diff(pts)>0)
    results['samples']=samples;results['windows']=[]
    for name,start,end in [('first',2,25),('second',35,58)]:
        mask=(pts[1:]>start)&(pts[1:]<end);gaps=np.diff(pts)[mask];changes=np.diff(ids)[mask]
        row={'name':name,'frames':len(gaps),'fps':float(1/gaps.mean()),'p95GapMs':float(np.percentile(gaps,95)*1000),
             'maxGapMs':float(gaps.max()*1000),'duplicateFraction':float(np.mean(changes==0))}
        results['windows'].append(row)
    results['treeCpuMean']=float(np.mean([s['treeCpu'] for s in samples if s['seconds']>2]))
    results['cleanDecode']=True;results['completed']=True
    if not args.timer_experiment:
        assert all(29.5<w['fps']<30.5 and w['p95GapMs']<=40 for w in results['windows']),results['windows']
        results['passed']=True
finally:
    if session:
        try:session.detach()
        except Exception:pass
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:
            if root and root.is_running():root.kill();root.wait(timeout=10)
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'cadence.json').write_text(json.dumps(results,indent=2))
print(json.dumps({k:v for k,v in results.items() if k!='samples'},indent=2))
