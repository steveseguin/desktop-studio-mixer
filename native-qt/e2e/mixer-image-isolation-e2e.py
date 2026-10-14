"""Packaged image decoding, transparency, stale-load rejection and worker faults.

Healthy media, local recording and private UDP publishing must keep running.
"""
from pathlib import Path
helpers=Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0]
helpers=helpers.replace('args=parser.parse_args()',"parser.add_argument('--fixture',required=True)\nargs=parser.parse_args()")
exec(helpers)
import base64,re
from PIL import ImageDraw
root=None;receiver=None;receiver_log=None
results={'sha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),'cases':[]}

def source_state(sid):return next(s for s in state()['sources'] if s['id']==sid)
def loaded(sid):return until(lambda:not source_state(sid)['starting'],'image decode',25)
def picture(name):
    data=api('/studio/screenshot',{'target':'program','max_width':640})
    file=report/(name+'.png');file.write_bytes(base64.b64decode(data['data']))
    return Image.open(file).convert('RGB')
def healthy_running():
    s=state();assert s['recording']['program_active'] and s['output']['live']
    assert {p.pid for p in root.children() if '--studio-worker' in p.cmdline() and 'media' in p.cmdline()}==media_pids
    return s
try:
    overlay=Image.new('RGBA',(256,144),(0,0,0,0));ImageDraw.Draw(overlay).rectangle((64,36,191,107),fill=(20,200,50,255));overlay.save(report/'transparent.png')
    Image.new('RGBA',(640,360),(20,200,50,128)).save(report/'half-alpha.png')
    Image.new('RGB',(8192,4096),(150,40,90)).save(report/'large.png')
    Image.new('RGB',(640,360),(20,200,50)).save(report/'green.jpg',quality=95)
    Image.new('RGB',(640,360),(40,90,180)).save(report/'blue.png')
    (report/'corrupt.png').write_bytes((report/'large.png').read_bytes()[:32])
    (report/'text.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="640" height="360"><rect width="640" height="360" fill="#14c832"/><text x="20" y="60">Image worker text</text></svg>')
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
    command('monitor.set',enabled=False)
    healthy=command('source.add',kind='media',name='Healthy media',locator=str(Path(args.fixture).resolve()))['id']
    command('audio.set',source=healthy,followsScene=False)
    until(lambda:source_state(healthy)['frames']>30 and state()['master_peak']>.01,'media')
    media_pids={p.pid for p in root.children() if '--studio-worker' in p.cmdline() and 'media' in p.cmdline()}
    background=command('source.add',kind='color',name='Background',locator='#285ab4')['id']
    command('layer.set',source=background,x=0,y=0,w=1,h=1)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    receiver_log=(report/'receiver.log').open('wb')
    receiver=subprocess.Popen([str(ffmpeg),'-v','warning','-i',f'udp://127.0.0.1:{port}?fifo_size=8192&overrun_nonfatal=1','-map','0','-c','copy',str(report/'received.mkv')],stdin=subprocess.PIPE,stdout=receiver_log,stderr=receiver_log,creationflags=subprocess.CREATE_NO_WINDOW)
    command('record.start',path=str(report/'local.mkv'))
    command('output.configure',protocol='udp',url=f'udp://127.0.0.1:{port}',encoder='software',bitrate_kbps=4000)
    command('output.start');until(lambda:state()['output']['live'],'publishing',45);time.sleep(2)
    for name in ('transparent.png','half-alpha.png','green.jpg','large.png','text.svg','corrupt.png'):
        tick=time.perf_counter();sid=command('source.add',kind='image',name=name,locator=str(report/name))['id'];ms=(time.perf_counter()-tick)*1000
        command('layer.set',source=sid,x=0,y=0,w=1,h=1);loaded(sid);s=source_state(sid)
        if name=='corrupt.png':assert s['error']
        elif name=='text.svg' and not (package/'imageformats/qsvg.dll').exists():assert s['error']
        else:
            assert not s['error'],s
            im=picture(name)
            expected=(150,40,90) if name=='large.png' else (30,145,115) if name=='half-alpha.png' else (20,200,50)
            assert max(abs(a-b) for a,b in zip(im.getpixel((320,180)),expected))<4
            if name=='transparent.png':assert im.getpixel((10,10))==(40,90,180),'Transparent background was lost'
        healthy_running();results['cases'].append({'name':name,'addApiMs':ms,'error':s['error']});command('source.remove',id=sid)
    # Updating/removing a source while its earlier load is pending must not
    # install an obsolete picture or resurrect the removed source.
    sid=command('source.add',kind='image',name='Updated while loading',locator=str(report/'large.png'))['id']
    command('source.update',id=sid,locator=str(report/'blue.png'));loaded(sid)
    command('layer.set',source=sid,x=0,y=0,w=1,h=1);assert picture('updated').getpixel((320,180))==(40,90,180)
    command('source.remove',id=sid)
    for n in range(12):
        sid=command('source.add',kind='image',name='Cancelled load',locator=str(report/'large.png'))['id']
        command('source.remove',id=sid);healthy_running()
    time.sleep(1);assert len(state()['sources'])==2
    results['cancelledLoads']=12
    for fault in ('kill','hang'):
        until(lambda:not any('--studio-worker' in p.cmdline() and 'image' in p.cmdline() for p in root.children()),'prior image workers stopped',25)
        found=threading.Event();stop_scan=threading.Event();victim=[]
        def intercept():
            while not stop_scan.is_set():
                for p in root.children():
                    try:
                        if '--studio-worker' in p.cmdline() and 'image' in p.cmdline():
                            (p.kill if fault=='kill' else p.suspend)();victim.append(p.pid);found.set();return
                    except psutil.NoSuchProcess:pass
                time.sleep(.002)
        scan=threading.Thread(target=intercept,daemon=True);scan.start()
        sid=command('source.add',kind='image',name='Fault '+fault,locator=str(report/'large.png'))['id']
        assert found.wait(5),'Did not intercept the owned image worker'
        stop_scan.set();scan.join(timeout=2)
        deadline=time.monotonic()+25;samples=[]
        while source_state(sid)['starting'] and time.monotonic()<deadline:
            s=healthy_running();samples.append(s['recording']);time.sleep(.2)
        assert not source_state(sid)['starting'] and source_state(sid)['error'],source_state(sid)
        results['cases'].append({'fault':fault,'pid':victim[0],'error':source_state(sid)['error'],'observations':len(samples)})
        command('source.remove',id=sid)
    results['finalRecording']=healthy_running()['recording']
    receiver.stdin.write(b'q\n');receiver.stdin.flush();assert receiver.wait(timeout=10)==0
    command('output.stop');until(lambda:not state()['output']['active'],'output stopped')
    command('record.stop');until(lambda:not state()['recording']['busy'],'record saved')
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(children,timeout=15);assert not alive
    results['files']=[]
    for file in (report/'local.mkv',report/'received.mkv'):
        dec=run([ffmpeg,'-v','error','-xerror','-i',file,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=120);assert dec.returncode==0 and not dec.stderr,dec.stderr
        raw=run([ffmpeg,'-v','error','-i',file,'-map','0:a:0','-ar','48000','-ac','2','-f','f32le','-'],timeout=60);assert raw.returncode==0
        pcm=np.frombuffer(raw.stdout,np.float32).reshape(-1,2)[48000:-48000];assert np.isfinite(pcm).all()
        windows=pcm[:len(pcm)//480*480].reshape(-1,480,2);minimum=float(np.sqrt(np.mean(windows**2,axis=1)).min());assert minimum>.005,minimum
        results['files'].append({'name':file.name,'cleanDecode':True,'minimum10msRms':minimum})
    results['passed']=True
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:
            if root and root.is_running():root.kill();root.wait(timeout=10)
    if receiver and receiver.poll() is None:receiver.kill();receiver.wait(timeout=10)
    if receiver_log:receiver_log.close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'image-isolation.json').write_text(json.dumps(results,indent=2))
print('Image isolation passed',flush=True)
