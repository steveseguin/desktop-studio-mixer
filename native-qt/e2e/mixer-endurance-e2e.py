"""Hour-long packaged production with owned-process/receiver failures.

Private media stays in --report. Physical USB removal is a separate manual step.
Set STUDIO_SOAK_SECONDS and VIDEO_MIXER_E2E_VDO_URL; provide --mediamtx.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import base64, traceback
seconds=int(os.environ.get('STUDIO_SOAK_SECONDS','3600'))
assert seconds>=60 and args.mediamtx
results={'requestedSeconds':seconds,'events':[],'samples':[]};handles=[];receiver=None;mtx=None

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'''<!doctype html><style>body{margin:0;background:transparent}#flash{position:absolute;left:0;top:0;width:48px;height:48px;background:black}#chat{position:absolute;bottom:12px;left:18px;padding:8px;background:#18293ddd;color:white;font:24px sans-serif}</style><div id=flash></div><div id=chat>Private test chat</div><script>
const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();o.frequency.value=1000;o.connect(g);g.connect(a.destination);g.gain.value=0;o.start();a.resume();
let next=1;function schedule(){while(next<a.currentTime+2){g.gain.setValueAtTime(.25,next);g.gain.setValueAtTime(0,next+.12);next++}}setInterval(schedule,100);schedule();
function frame(){flash.style.background=a.currentTime%1<.12?'white':'black';chat.textContent='Private test chat / '+Math.floor(a.currentTime)+' s';requestAnimationFrame(frame)}frame();</script>'''
        self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*_):pass

def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]

def start_receiver_server():
    log=(report/('receiver-server-'+str(time.time_ns())+'.log')).open('wb');handles.append(log)
    process=subprocess.Popen([args.mediamtx,str(report/'receiver.yml')],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    owned.append(psutil.Process(process.pid));return process

def receive(name,duration=12):
    path=report/(name+'.mkv')
    for attempt in range(5):
        result=run([ffmpeg,'-v','error','-y','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/endurance','-t',str(duration),'-map','0','-c','copy',path],timeout=duration+30)
        if result.returncode==0:return path
        time.sleep(1)
    raise AssertionError(result.stderr.decode(errors='replace'))

def workers(kind):
    answer=[]
    for p in root.children():
        try:
            c=p.cmdline()
            if '--studio-worker' in c and c[c.index('--studio-worker')+1]==kind:answer.append(p)
        except psutil.Error:pass
    return answer

def browser_worker(source,allow_starting=False):
    for p in workers('browser'):
        try:
            if any(source in ' '.join(c.cmdline()) for c in p.children(recursive=True)):return p
        except psutil.Error:pass
    if not allow_starting:raise AssertionError('Browser worker not found')

def wait_for(fn,label,timeout=45):
    stop=time.monotonic()+timeout
    while time.monotonic()<stop:
        if fn():return
        time.sleep(.25)
    raise AssertionError(label)

def replacement(victim,kind):
    old={p.pid for p in workers(kind)};pid=victim.pid;victim.kill();victim.wait(timeout=10)
    wait_for(lambda:any(p.pid not in old for p in workers(kind)),kind+' did not restart')
    return pid

def evidence():
    data=state();total={'cpuSeconds':0,'privateMiB':0,'handles':0,'processes':0}
    for p in [root,*root.children(recursive=True)]:
        try:
            c=p.cpu_times();total['cpuSeconds']+=c.user+c.system;total['privateMiB']+=p.memory_info().private/2**20
            total['handles']+=p.num_handles();total['processes']+=1
        except psutil.Error:pass
    total.update(seconds=round(time.monotonic()-started,2),system=data.get('system'),
        recording=data['recording'],output=data['output'],sources=[{k:s[k] for k in ('id','kind','frames','peak','error','starting')} for s in data['sources']])
    return total

def save():
    (report/'endurance.json').write_text(json.dumps(results,indent=2),encoding='utf-8')

def full_decode(path):
    decoded=run([ffmpeg,'-v','error','-i',path,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=max(90,seconds))
    return {'file':path.name,'bytes':path.stat().st_size,'clean':decoded.returncode==0 and not decoded.stderr,'decoderErrors':decoded.stderr.decode(errors='replace')[-2000:]}

try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    (report/'session.json').write_text(json.dumps(target),encoding='utf-8')
    command('canvas.set',format=1,renderer='cpu');command('workspace.set',mode='single')
    cameras=api('/sources/cameras')['sources'];micros=api('/sources/audio-inputs')['sources']
    camera=next(c for c in cameras if 'C920' in c['name']);micro=next(c for c in micros if 'C920' in c['name'])
    cam=command('source.add',kind='camera',name='Logitech endurance camera',locator=camera['id'])['id']
    mic=command('source.add',kind='microphone',name='Logitech endurance microphone',locator=micro['id'])['id']
    command('audio.set',source=mic,gain=.1,monitored=False)
    vdo=command('source.add',kind='browser',name='VDO.Ninja endurance viewer',locator=os.environ['VIDEO_MIXER_E2E_VDO_URL'])['id']
    command('audio.set',source=vdo,gain=.1,monitored=False)
    browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=browser_server.serve_forever,daemon=True).start()
    overlay=command('source.add',kind='browser',name='Transparent chat and AV clock',locator=f'http://127.0.0.1:{browser_server.server_port}/')['id']
    command('audio.set',source=overlay,monitored=False)
    for scene_id,reverse in [(red,False),(green,True)]:
        command('layer.set',source=cam,scene=scene_id,x=.5 if reverse else 0,y=.08,w=.5,h=.84)
        command('layer.set',source=vdo,scene=scene_id,x=0 if reverse else .5,y=.08,w=.5,h=.84)
        command('layer.set',source=overlay,scene=scene_id,x=0,y=0,w=1,h=1)
    wait_for(lambda:all(next(s for s in state()['sources'] if s['id']==sid)['frames']>30 for sid in (cam,vdo,overlay)),'Sources did not begin rendering',75)
    command('scene.select',id=red)
    rtsp,rtmp,whip,udp,srt=[port() for _ in range(5)]
    (report/'receiver.yml').write_text(f'''logLevel: warn
rtspAddress: 127.0.0.1:{rtsp}
rtspTransports: [tcp]
rtmpAddress: 127.0.0.1:{rtmp}
webrtcAddress: 127.0.0.1:{whip}
webrtcLocalUDPAddress: 127.0.0.1:{udp}
webrtcIPsFromInterfaces: false
webrtcAdditionalHosts: [127.0.0.1]
webrtcICEServers2: []
srtAddress: 127.0.0.1:{srt}
hls: false
moq: false
paths:
  all_others:
''')
    mtx=start_receiver_server();time.sleep(1)
    recording=report/'hour-program.mkv';command('record.start',path=str(recording))
    wait_for(lambda:state()['recording']['status'].startswith('Recording:'),'Independent recording did not start')
    record_encoder=workers('encoder')[0].pid
    command('output.configure',protocol='rtmp',url=f'rtmp://127.0.0.1:{rtmp}/endurance',encoder='nvenc',bitrate_kbps=9000)
    command('output.start');wait_for(lambda:state()['output']['live'],'RTMP did not become live')
    started=time.monotonic();results['startedUtc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
    results['sources']={'camera':cam,'microphone':mic,'vdo':vdo,'overlay':overlay};results['recordEncoderPid']=record_encoder
    results['firstReceiver']=full_decode(receive('receiver-start'))
    results['configuration']={'width':1920,'height':1080,'fps':30,'renderer':'cpu','publishEncoder':'nvenc','windowless':True}
    results['initialScreenshot']=cli_call('screenshot','program','--out',str(report/'start.png'))
    psutil.cpu_percent();last_switch=-1;last_picture=0;done=set();receptions=[]
    while time.monotonic()-started<seconds:
        elapsed=time.monotonic()-started
        step=int(elapsed//120)
        if step!=last_switch:
            command('scene.select',id=red if step%2==0 else green);last_switch=step
        picture=int(elapsed//300)
        if picture!=last_picture:
            cli_call('screenshot','program','--out',str(report/f'program-{picture:02d}.png'), '--width','640');last_picture=picture
        for at,kind in [(300,'browser'),(600,'microphone'),(900,'camera'),(1200,'publish-encoder'),(1500,'network'),(1800,'record-encoder'),(2100,'browser-hang')]:
            if elapsed<at or kind in done:continue
            event={'kind':kind,'seconds':round(elapsed,2)};done.add(kind);event_start=time.monotonic()
            try:
                if kind=='browser':event['oldPid']=replacement(browser_worker(overlay),'browser')
                elif kind in ('microphone','camera'):
                    worker_kind='audio' if kind=='microphone' else 'camera';event['oldPid']=replacement(workers(worker_kind)[0],worker_kind)
                elif kind.endswith('encoder'):
                    choices=workers('encoder');victim=next(p for p in choices if (p.pid==record_encoder)==(kind=='record-encoder'))
                    event['oldPid']=replacement(victim,'encoder')
                elif kind=='network':
                    mtx.terminate();mtx.wait(timeout=10);time.sleep(12);mtx=start_receiver_server()
                elif kind=='browser-hang':
                    victim=browser_worker(overlay);event['oldPid']=victim.pid;victim.suspend()
                    try:wait_for(lambda:not victim.is_running() and (current:=browser_worker(overlay,True)) is not None and current.pid!=victim.pid,'Hung browser not replaced',55)
                    finally:
                        if victim.is_running():victim.resume()
                wait_for(lambda:state()['recording']['active'] and state()['output']['live'],'Other outputs did not survive/recover',60)
                if kind in ('browser','browser-hang','camera'):
                    sid=cam if kind=='camera' else overlay
                    before=next(s for s in state()['sources'] if s['id']==sid)['frames'];time.sleep(3)
                    assert next(s for s in state()['sources'] if s['id']==sid)['frames']>before
                if kind in ('network','publish-encoder'):receptions.append(receive('receiver-after-'+kind))
                event['passed']=True
            except Exception:event['passed']=False;event['error']=traceback.format_exc()
            event['recoverySeconds']=round(time.monotonic()-event_start,2);results['events'].append(event);save();print(json.dumps(event),flush=True)
        before=time.perf_counter();sample=evidence();sample['apiLatencyMs']=(time.perf_counter()-before)*1000
        sample['independentSystemCpuPercent']=psutil.cpu_percent();results['samples'].append(sample)
        assert sample['recording']['active'],'Recording stopped unexpectedly: '+str(sample['recording'])
        save();print(json.dumps({'seconds':sample['seconds'],'privateMiB':round(sample['privateMiB'],1),'processes':sample['processes'],'dropped':sample['recording']['dropped_frames']}),flush=True)
        time.sleep(min(10,max(0,seconds-(time.monotonic()-started))))
    results['measuredSeconds']=time.monotonic()-started;receptions.append(receive('receiver-end'))
    command('output.stop');wait_for(lambda:not state()['output']['active'] and not state()['output']['stopping'],'Publisher did not stop')
    assert state()['recording']['active'];command('record.stop');wait_for(lambda:not state()['recording']['busy'],'Recording did not finalize',45)
    results['recording']=full_decode(recording);results['receiverFiles']=[full_decode(p) for p in receptions]
    results['passed']=results['recording']['clean'] and all(e['passed'] for e in results['events']) and all(r['clean'] for r in results['receiverFiles'])
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    if mtx and mtx.poll() is None:mtx.terminate();mtx.wait(timeout=15)
    if browser_server:browser_server.shutdown();browser_server.server_close()
    for h in handles:h.close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    results['originalShowUnchanged']=True;save()
print(json.dumps({k:v for k,v in results.items() if k!='samples'},indent=2),flush=True)
assert results.get('passed'), 'Endurance workflow did not pass; inspect its report'
