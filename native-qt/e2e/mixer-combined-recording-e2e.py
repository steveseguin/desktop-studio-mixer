"""Sustain maximum recording tracks alongside browser audio and private RTMP."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import re
duration=int(os.environ.get('STUDIO_COMBINED_SECONDS','600'))
canvas_format=int(os.environ.get('STUDIO_COMBINED_FORMAT','0'));fps=60 if canvas_format in (2,3,6,7) else 30
results={'requestedSeconds':duration,'canvasFormat':canvas_format,'executableSha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),'samples':[]}
root=None;mtx=None;log=None

def free_port():
    with socket.socket() as reserve:reserve.bind(('127.0.0.1',0));return reserve.getsockname()[1]

def profile():
    total={'cpuSeconds':0,'privateMiB':0,'handles':0,'processes':0,'mainPrivateMiB':root.memory_info().private/2**20}
    for process in [root,*root.children(recursive=True)]:
        try:
            cpu=process.cpu_times();total['cpuSeconds']+=cpu.user+cpu.system
            total['privateMiB']+=process.memory_info().private/2**20
            total['handles']+=process.num_handles();total['processes']+=1
        except psutil.NoSuchProcess:pass
    return total

def decode_all(path):
    decoded=run([ffmpeg,'-v','error','-i',path,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=300)
    assert decoded.returncode==0 and not decoded.stderr,(str(path),decoded.stderr)

def receive(name):
    # The output process can be live before MediaMTX has registered its path.
    for attempt in range(10):
        output=report/(name+f'-{attempt}.mkv')
        receiver=run([ffmpeg,'-v','error','-n','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/combined','-t','4','-map','0','-c','copy',output],timeout=30)
        if receiver.returncode==0:break
        if b'404' not in receiver.stderr:break
        time.sleep(.3)
    assert receiver.returncode==0,receiver.stderr
    decode_all(output)

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'''<!doctype html><body style="background:red;color:white;font:32px sans-serif"><span id=t></span><script>setInterval(()=>t.textContent=Date.now(),33);const c=new AudioContext(),o=c.createOscillator(),g=c.createGain();o.frequency.value=1150;g.gain.value=.12;o.connect(g);g.connect(c.destination);o.start();c.resume()</script>'''
        self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*_):pass

try:
    assert args.mediamtx,'Specify private MediaMTX receiver'
    rtsp,rtmp=free_port(),free_port()
    config=report/'receiver.yml';config.write_text(f'logLevel: warn\nrtspAddress: 127.0.0.1:{rtsp}\nrtspTransports: [tcp]\nrtmpAddress: 127.0.0.1:{rtmp}\nwebrtc: false\nsrt: false\nhls: false\nmoq: false\npaths:\n  all_others:\n')
    log=(report/'receiver.log').open('wb');mtx=subprocess.Popen([args.mediamtx,str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(.7);assert mtx.poll() is None
    browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=browser_server.serve_forever,daemon=True).start()
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    command('canvas.set',format=canvas_format)
    command('monitor.set',enabled=False)
    ids=[]
    for n in range(15):
        clip=report/f'track-{n}{".mp4" if n<4 else ".wav"}'
        video=['-f','lavfi','-i','testsrc2=s=640x360:r=30'] if n<4 else []
        codecs=['-c:v','libopenh264','-c:a','aac'] if video else []
        made=run([ffmpeg,'-v','error','-n',*video,'-f','lavfi','-i',f'sine=frequency={400+50*n}:sample_rate=48000','-t','12',*codecs,clip],timeout=40)
        assert made.returncode==0,made.stderr
        sid=command('source.add',kind='media',name=f'Combined track {n+1}',locator=str(clip))['id'];ids.append(sid)
        command('audio.set',source=sid,gain=.2,monitored=False)
        if n<4:command('layer.set',source=sid,x=(n%2)*.5,y=(n//2)*.5,w=.5,h=.5)
    sid=command('source.add',kind='browser',name='Combined browser track',locator=f'http://127.0.0.1:{browser_server.server_port}/')['id'];ids.append(sid)
    command('audio.set',source=sid,gain=.2,monitored=False)
    until(lambda:all(s['peak']>.001 for s in state()['sources']),'sixteen live source tracks',45)
    output=report/'combined.mkv';recording_start=time.monotonic();command('record.start',path=str(output),audioSources=ids,isoSources=ids[:4])
    until(lambda:state()['recording']['status'].startswith('Recording:') and all(s['status'].startswith('Recording:') for s in state()['recording']['separate_videos']),'five recorders',45)
    command('output.configure',protocol='rtmp',url=f'rtmp://127.0.0.1:{rtmp}/combined',encoder='auto',bitrate_kbps=6000)
    command('output.start');until(lambda:state()['output']['live'],'private RTMP live',45)
    results['encoder']=state()['output']['encoder'];receive('receiver-start')
    time.sleep(5);initial=profile();results['initialProfile']=initial;started=time.monotonic();latencies=[];snapshots=[];last_action=-1
    workers={p.pid for p in root.children()};privacy_times=[];results['privacyTimes']=privacy_times;private=False
    while time.monotonic()-started<duration:
        time.sleep(2)
        begin=time.perf_counter();s=state();latencies.append((time.perf_counter()-begin)*1000)
        assert s['recording']['program_active'] and all(v['active'] for v in s['recording']['separate_videos']),s['recording']
        assert s['output']['live'],s['output']
        sample=profile();sample['seconds']=time.monotonic()-started;sample['droppedFrames']=s['recording']['dropped_frames'];results['samples'].append(sample)
        action=int(sample['seconds']//30)
        if action!=last_action:
            last_action=action
            command('workspace.set',mode='studio' if action%2 else 'single')
            command('monitor.set',enabled=bool(action%2),gain=0)
            command('production.set',privateBreak=False)
            picture=api('/studio/screenshot',{'target':'program','max_width':320});snapshots.append(hashlib.sha256(picture['data'].encode()).hexdigest())
            print(json.dumps({'seconds':round(sample['seconds']), 'privateMiB':round(sample['privateMiB'],1),'mainPrivateMiB':round(sample['mainPrivateMiB'],1),'droppedFrames':sample['droppedFrames']}),flush=True)
            (report/'combined-recording.json').write_text(json.dumps(results,indent=2))
        # Exercise privacy without leaving it active for the final audio sample.
        if duration>120 and 55<sample['seconds']%120<60:
            command('production.set',privateBreak=True)
            if not private:privacy_times.append(time.monotonic()-recording_start+1.2)
            private=True
        elif duration>120 and 60<sample['seconds']%120<65:command('production.set',privateBreak=False);private=False
    command('production.set',privateBreak=False);time.sleep(2)
    final=profile();elapsed=time.monotonic()-started
    assert {p.pid for p in root.children()}==workers,'Workers restarted unexpectedly'
    results['runtimeCompleted']={'finalProfile':final,'elapsedSeconds':elapsed,'apiLatenciesMs':latencies,'uniqueProgramScreenshots':len(set(snapshots)),'workerRestarts':0}
    (report/'combined-recording.json').write_text(json.dumps(results,indent=2))
    receive('receiver-end')
    command('output.stop');until(lambda:not state()['output']['active'],'publishing stopped')
    paths=[output]+[Path(s['path']) for s in state()['recording']['separate_videos']]
    command('record.stop');until(lambda:not state()['recording']['busy'],'five recordings finalized',60)
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=40);target=None
    _,alive=psutil.wait_procs(children,timeout=15);assert not alive,[p.pid for p in alive]
    for path in paths:decode_all(path)
    if privacy_times:
        privacy_rms=[]
        for path,indices in [(output,range(17))]+[(p,range(1)) for p in paths[1:]]:
            for index in indices:
                raw=run([ffmpeg,'-v','error','-ss',str(privacy_times[0]),'-i',path,'-map',f'0:a:{index}','-t','0.5','-ac','1','-ar','48000','-f','f32le','-'],timeout=30)
                assert raw.returncode==0 and len(raw.stdout)==24000*4,(path,index,raw.stderr)
                rms=float(np.sqrt(np.mean(np.frombuffer(raw.stdout,np.float32)**2)));assert rms<.0001,(path,index,rms)
                privacy_rms.append(rms)
        results['privateBreakSilentAudioStreams']=len(privacy_rms)
    tones=[]
    for n in range(16):
        raw=run([ffmpeg,'-v','error','-ss',str(duration-5),'-i',output,'-map',f'0:a:{n+1}','-t','1','-ac','1','-ar','48000','-f','f32le','-'],timeout=45)
        assert raw.returncode==0 and len(raw.stdout)==48000*4,(n,raw.stderr)
        samples=np.frombuffer(raw.stdout,np.float32)
        # Short windows tolerate independent WebAudio clock phase changes.
        amplitudes=[float(abs(samples[start:start+4800]@np.exp(-2j*np.pi*(400+50*n)*np.arange(4800)/48000))*2/4800) for start in range(0,48000,4800)]
        tone=float(np.median(amplitudes));assert tone>.012,(n,tone);tones.append(tone)
    packets=run([ffmpeg,'-v','error','-i',output,'-map','0:v:0','-c:v','copy','-f','framecrc','-'],text=True,encoding='utf-8',timeout=90)
    rows=[line.split(',') for line in packets.stdout.splitlines() if line and not line.startswith('#')]
    dimensions=re.search(r'#dimensions 0: (\d+x\d+)',packets.stdout)
    expected_dimensions=('1280x720','1920x1080','1920x1080','1280x720','1080x1920','720x1280','720x1280','1080x1920')[canvas_format]
    assert dimensions and dimensions[1]==expected_dimensions,(dimensions,expected_dimensions)
    tb=re.search(r'#tb 0: (\d+)/(\d+)',packets.stdout);scale=int(tb[1])/int(tb[2]);pts=[int(row[2])*scale for row in rows]
    gaps=np.diff(pts);assert len(rows)>duration*fps*.9 and min(gaps)>0
    results['summary']={'elapsedSeconds':elapsed,'cpuLogicalCores':(final['cpuSeconds']-initial['cpuSeconds'])/elapsed,'treePrivateGrowthMiB':final['privateMiB']-initial['privateMiB'],'mainPrivateGrowthMiB':final['mainPrivateMiB']-initial['mainPrivateMiB'],'handleGrowth':final['handles']-initial['handles'],'apiP95Ms':float(np.percentile(latencies,95)),'apiMaxMs':max(latencies),'uniqueProgramScreenshots':len(set(snapshots)),'videoPackets':len(rows),'maxVideoGapMs':float(max(gaps)*1000),'p95VideoGapMs':float(np.percentile(gaps,95)*1000),'sourceAudioTones':tones,'allFiveRecordingsDecoded':True,'bothReceiverCapturesDecoded':True,'workerRestarts':0}
    results['passed']=True
finally:
    if target:
        cli_call('quit');root.wait(timeout=40)
    if browser_server:browser_server.shutdown();browser_server.server_close()
    if mtx:mtx.terminate();mtx.wait(timeout=10)
    if log:log.close()
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'combined-recording.json').write_text(json.dumps(results,indent=2));print(json.dumps(results.get('summary',{})),flush=True)
