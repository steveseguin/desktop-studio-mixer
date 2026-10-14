"""Packaged live mix: huge surfaces, reduced CPU affinity and capture-worker loss."""
from pathlib import Path
source=Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0]
source=source.replace('args=parser.parse_args()',"parser.add_argument('--fixture',required=True)\nparser.add_argument('--max-video-gap-ms',type=float,default=250)\nparser.add_argument('--cpu-seconds',type=int,default=15)\nargs=parser.parse_args()")
exec(source)
assert 1<=args.cpu_seconds<=180
import sys,wave,win32gui,re
root=None;fixture_process=None;receiver=None;logs=[]
results={'sha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),'phases':[],'operations':[]}
game=Path(__file__).resolve().parents[3]/'game-capture/native-qt'
base_command=command
def command(operation,**kwargs):
    started=time.time();tick=time.perf_counter()
    reply=base_command(operation,**kwargs)
    results['operations'].append({'command':operation,'wall':started,'apiMs':(time.perf_counter()-tick)*1000})
    return reply
def launch(argv,name):
    log=(report/(name+'.log')).open('wb');logs.append(log)
    return subprocess.Popen([str(v) for v in argv],stdin=subprocess.PIPE,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
def size(value):
    temp=report/'next.json';temp.write_text(json.dumps({'size':value,'hidden':False}));os.replace(temp,report/'fixture.json')
def workers(kind):
    found=[]
    for p in root.children(recursive=True):
        try:
            c=p.cmdline()
            if '--studio-worker' in c and kind in c:found.append(p)
        except psutil.NoSuchProcess:pass
    return found
def phase(name,seconds=8):
    began=time.monotonic();wall=time.time();rows=[]
    while time.monotonic()-began<seconds:
        tick=time.perf_counter();s=state();ms=(time.perf_counter()-tick)*1000
        assert s['recording']['program_active'] and s['output']['live'],(name,s['recording'],s['output'])
        assert [p.pid for p in workers('media')]==media_pids,'Healthy media worker restarted'
        rows.append({'seconds':time.monotonic()-began,'apiMs':ms,'drops':s['recording']['dropped_frames'],'repeats':s['recording'].get('repeated_frames'),
                     'privateMiB':root.memory_info().private/2**20,'sources':s['sources']})
        time.sleep(.3)
    results['phases'].append({'name':name,'wall':wall,'samples':rows,'maxApiMs':max(r['apiMs'] for r in rows),'extraDrops':rows[-1]['drops']-rows[0]['drops']})
    print(name,'API max',round(max(r['apiMs'] for r in rows),1),'drops',rows[-1]['drops']-rows[0]['drops'],flush=True)
try:
    # Generate before launching/profiling; no decode or compression competes with the app.
    t=np.arange(96000)/48000;pcm=np.stack([.04*np.sin(2*np.pi*f*t) for f in (440,880)],axis=1)
    with wave.open(str(report/'tone.wav'),'wb') as f:f.setparams((2,2,48000,0,'NONE','not compressed'));f.writeframes((pcm*32767).astype('<i2').tobytes())
    image_file=report/'8192x4096.png';Image.new('RGB',(8192,4096),(150,40,90)).save(image_file)
    (report/'broken.png').write_bytes(image_file.read_bytes()[:32])
    size([1280,720]);fixture_process=launch([sys.executable,game/'e2e/capture-resilience-packaged.py','--fixture',report],'window-fixture')
    end=time.monotonic()+10
    while not (hwnd:=win32gui.FindWindow(None,'Game Capture Resilience Fixture')) and time.monotonic()<end:time.sleep(.1)
    assert hwnd
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
    command('monitor.set',enabled=False)
    healthy=command('source.add',kind='media',name='Healthy source',locator=str(Path(args.fixture).resolve()))['id']
    window=command('source.add',kind='window',name='Resizable capture',locator=str(hwnd))['id']
    # Both fixtures contain the same tones. Avoid phase cancellation between
    # them when checking that the independent healthy track stays continuous.
    command('audio.set',source=window,muted=True)
    command('layer.set',source=window,x=0,y=0,w=1,h=1)
    until(lambda:all(s['frames']>30 for s in state()['sources']),'both video sources')
    media_pids=[p.pid for p in workers('media')]
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    receiver=launch([ffmpeg,'-v','warning','-i',f'udp://127.0.0.1:{port}?fifo_size=8192&overrun_nonfatal=1','-map','0','-c','copy',report/'received.mkv'],'receiver')
    command('output.configure',protocol='udp',url=f'udp://127.0.0.1:{port}',encoder='software',bitrate_kbps=4000)
    command('record.start',path=str(report/'local.mkv'));until(lambda:state()['recording']['program_active'],'recording')
    command('output.start');until(lambda:state()['output']['live'],'publishing',45)
    phase('baseline')
    for dimensions in ([3840,2160],[7680,4320],[2160,3840],[641,479]):size(dimensions);phase(str(dimensions))
    for n in range(20):size([641,479] if n%2 else [3840,2160]);time.sleep(.1)
    size([1280,720]);phase('resize-storm-recovered')
    picture=command('source.add',kind='image',name='8K still',locator=str(image_file))['id'];phase('8K-still',5)
    command('source.remove',id=picture)
    picture=command('source.add',kind='image',name='Corrupt image',locator=str(report/'broken.png'))['id'];phase('broken-still',3)
    results['brokenImageError']=next(s['error'] for s in state()['sources'] if s['id']==picture)
    command('source.remove',id=picture)
    allowed=root.cpu_affinity();restricted=allowed[-2:];results['restrictedCpus']=restricted
    for p in [root]+root.children(recursive=True):
        try:p.cpu_affinity(restricted)
        except psutil.NoSuchProcess:pass
    phase('two-logical-cpus',args.cpu_seconds)
    for p in [root]+root.children(recursive=True):
        try:p.cpu_affinity(allowed)
        except psutil.NoSuchProcess:pass
    window_pid=workers('window')[0].pid;workers('window')[0].kill();phase('window-worker-killed',12)
    assert any(p.pid!=window_pid for p in workers('window')),'Window worker did not recover'
    audio_pid=workers('audio')[0].pid;workers('audio')[0].suspend();phase('audio-worker-hung',14)
    assert any(p.pid!=audio_pid for p in workers('audio')),'Audio worker did not recover'
    fixture_process.terminate();fixture_process.wait(timeout=10);phase('window-disappeared',6)
    results['unavailableWindow']=next(s['error'] for s in state()['sources'] if s['id']==window)
    command('source.remove',id=window);phase('healthy-source-only',6)
    receiver.stdin.write(b'q\n');receiver.stdin.flush();assert receiver.wait(timeout=10)==0
    command('output.stop');until(lambda:not state()['output']['active'],'publishing stopped')
    command('record.stop');until(lambda:not state()['recording']['busy'],'recording finalized')
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(children,timeout=10);assert not alive
    results['files']=[]
    for file in (report/'local.mkv',report/'received.mkv'):
        decoded=run([ffmpeg,'-v','error','-xerror','-i',file,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=180)
        assert decoded.returncode==0 and not decoded.stderr,(file,decoded.stderr)
        raw=run([ffmpeg,'-v','error','-i',file,'-map','0:a:0','-ar','48000','-ac','2','-f','f32le','-'],timeout=90)
        assert raw.returncode==0
        pcm=np.frombuffer(raw.stdout,np.float32).reshape(-1,2)[48000:-48000];assert np.isfinite(pcm).all()
        windows=pcm[:len(pcm)//480*480].reshape(-1,480,2);levels=np.sqrt(np.mean(windows**2,axis=1));rms=float(levels.min())
        short=pcm[:len(pcm)//48*48].reshape(-1,48,2);short_levels=np.sqrt(np.mean(short**2,axis=1)).min(axis=1)
        cadence=run([ffmpeg,'-v','info','-i',file,'-an','-vf','scale=1:1,showinfo','-fps_mode','passthrough','-f','null','-'],timeout=180)
        assert cadence.returncode==0
        times=np.array([float(v) for v in re.findall(rb'\bn:\s*\d+\s+pts:.*?pts_time:([-\d.]+)',cadence.stderr)])
        assert len(times)>1000 and np.all(np.diff(times)>0)
        gaps=np.diff(times);steady=gaps[times[1:]>2]
        row={'name':file.name,'cleanDecode':True,'minimum10msRms':rms,'lowEnergySeconds':(1+np.flatnonzero(np.min(levels,axis=1)<=.005)*.01).tolist(),
             'minimum1msRms':float(short_levels.min()),'lowEnergy1msSeconds':(1+np.flatnonzero(short_levels<=.005)*.001).tolist(),'frames':len(times),
             'fps':float(1/gaps.mean()),'p95GapMs':float(np.percentile(steady,95)*1000),'maxGapMs':float(steady.max()*1000),
             'longGaps':[{'seconds':float(t),'gapMs':float(g*1000)} for t,g in zip(times[1:],gaps) if g>.07]}
        results['files'].append(row)
    assert all(row['minimum1msRms']>.005 and row['maxGapMs']<=args.max_video_gap_ms for row in results['files']),results['files']
    assert max(p['maxApiMs'] for p in results['phases'])<1000
    assert results['brokenImageError'] and results['unavailableWindow']
    results['passed']=True
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:
            if root and root.is_running():root.kill();root.wait(timeout=10)
    for proc in (receiver,fixture_process):
        if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=10)
    for log in logs:log.close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'source-resilience.json').write_text(json.dumps(results,indent=2))
