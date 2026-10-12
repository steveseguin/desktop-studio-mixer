"""Ten-minute packaged-app recording with camera, browser, alpha overlay and mic.

Checks private commit, handles, child counts, UI responsiveness, actual changing
canvas pixels and the fully decoded recording. Set STUDIO_SOAK_SECONDS to vary
duration; reports always state the actual duration.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import os,ctypes,psutil,numpy as np
root=psutil.Process(args.pid);results={};samples=[]
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'''<!doctype html><body style="background:#24b46a;color:white;font:38px sans-serif"><div id=t></div><script>setInterval(()=>t.textContent=Date.now(),33);const c=new AudioContext(),o=c.createOscillator(),g=c.createGain();o.frequency.value=731;g.gain.value=.08;o.connect(g);g.connect(c.destination);o.start();c.resume()</script>'''
        if self.path.startswith('/overlay'):body=b'<!doctype html><body style="margin:0;background:transparent"><div style="position:absolute;bottom:10px;left:10px;background:#e25;color:white;padding:12px;font:30px sans-serif">Soak overlay</div>'
        self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body)
    def log_message(self,*_):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=server.serve_forever,daemon=True).start()
def snapshot():
    result={'cpuSeconds':0,'rssMiB':0,'privateMiB':0,'handles':0,'processes':0,'workerPids':[]}
    for p in [root]+root.children(recursive=True):
        try:
            cpu=p.cpu_times();memory=p.memory_info();result['cpuSeconds']+=cpu.user+cpu.system
            result['rssMiB']+=memory.rss/2**20;result['privateMiB']+=getattr(memory,'private',memory.pagefile)/2**20
            result['handles']+=p.num_handles();result['processes']+=1
            if p.name().lower()=='desktop-studio.exe' and p.pid!=root.pid:result['workerPids'].append(p.pid)
        except psutil.NoSuchProcess:pass
    return result
def save():
    results['samples']=samples;(report/'soak.json').write_text(json.dumps(results,indent=2))
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))
choose_dialog(window,'canvasFormat','1280x720 / 30 fps');choose_dialog(window,'compositorBackend','CPU composition');enabled(control('guestProgramReturn'),False)
try:
    add_source('Camera / webcam','QA soak camera',device='HD Pro Webcam C920')
    add_source('Microphone / audio input','QA soak mic',device='HD Pro Webcam C920')
    add_source('Browser /','QA soak browser',f'http://127.0.0.1:{server.server_port}/')
    add_source('Browser /','QA soak overlay',f'http://127.0.0.1:{server.server_port}/overlay')
    press('editSource');dialog=window.child_window(title='Edit source',control_type='Window');enabled(control('browserTransparency',dialog),True);dialog.child_window(title='OK',control_type='Button').invoke();time.sleep(3)
    path=report/'soak.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').invoke()
    end=time.time()+30
    while time.time()<end and not control('recordingStatus').window_text().startswith('Recording:'):time.sleep(.3)
    assert control('recordingStatus').window_text().startswith('Recording:')
    time.sleep(10);initial=snapshot();start=time.monotonic();previous=None;changes=0;latency=[]
    duration=int(os.environ.get('STUDIO_SOAK_SECONDS','600'));results['requestedSeconds']=duration
    while time.monotonic()-start<duration:
        time.sleep(2);response=ctypes.c_size_t();begin=time.perf_counter()
        assert ctypes.windll.user32.SendMessageTimeoutW(window.handle,0,0,0,2,1500,ctypes.byref(response))
        latency.append((time.perf_counter()-begin)*1000)
        assert control('studioRecord').window_text()=='Stop recording',control('recordingStatus').window_text()
        sample=snapshot();sample['seconds']=round(time.monotonic()-start,2);samples.append(sample)
        if len(samples)%15==0:
            current=np.asarray(control('previewCanvas').capture_as_image()).astype(np.int16)
            if previous is not None:
                assert np.mean(abs(current-previous))>.005,'canvas stopped updating';changes+=1
            previous=current;save();print('soak',sample['seconds'],'s, private',round(sample['privateMiB'],1),'MiB, handles',sample['handles'],flush=True)
    elapsed=time.monotonic()-start;final=snapshot()
    results['summary']={'seconds':round(elapsed,2),'cpuConvention':'100% = one logical core','treeCpuPercent':round((final['cpuSeconds']-initial['cpuSeconds'])/elapsed*100,2),
        'privateGrowthMiB':round(final['privateMiB']-initial['privateMiB'],2),'handleGrowth':final['handles']-initial['handles'],
        'processGrowth':final['processes']-initial['processes'],'uiP95Ms':round(float(np.percentile(latency,95)),2),'uiMaxMs':round(max(latency),2),'canvasChanges':changes}
    assert final['processes']==initial['processes'],'workers accumulated'
    assert set(final['workerPids'])==set(initial['workerPids']),'worker restarted during the soak'
    results['summary']['unexpectedWorkerRestarts']=0
    assert final['handles']-initial['handles']<100,'handles grew unexpectedly'
    assert final['privateMiB']-initial['privateMiB']<160,'large memory growth; investigate'
    press('studioRecord');end=time.time()+30
    while time.time()<end and 'saved' not in control('recordingStatus').window_text().lower():time.sleep(.3)
    assert 'saved' in control('recordingStatus').window_text().lower()
    ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
    decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,text=True,timeout=300)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    timestamps=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-map','0:v:0','-c:v','copy','-f','framecrc','-'],text=True,timeout=90)
    packets=[line for line in timestamps.splitlines() if line and not line.startswith('#')]
    assert len(packets)>duration*27,'sustained recording lost too many video frames'
    pts=[int(line.split(',')[2]) for line in packets]
    assert all(b>a for a,b in zip(pts,pts[1:])),'non-monotonic video timestamps'
    results['recordedVideoPackets']=len(packets)
    results['monotonicVideoTimestamps']=True
    results['fullRecordingDecode']='passed'
    audio=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-ss',str(max(0,duration-10)),'-i',str(path),'-t','5','-vn','-f','f32le','-ac','1','-ar','48000','pipe:1']),np.float32)
    rms=float(np.sqrt(np.mean(audio*audio)))
    # Independent device/browser clocks need not preserve oscillator phase over
    # five seconds. Check short windows; a long coherent sum can cancel the tone.
    amplitudes=[float(abs(np.sum(audio[i:i+4800]*np.exp(-2j*np.pi*731*np.arange(4800)/48000)))*2/4800) for i in range(0,len(audio)-4800,4800)]
    tone=float(np.median(amplitudes));quiet=float(np.percentile(amplitudes,10))
    results['recordedAudio']={'rms':rms,'tone731Hz100msMedian':tone,'tone731Hz100msP10':quiet};save()
    assert rms>.005 and tone>.02 and quiet>.01,(rms,tone,quiet)
finally:
    save();server.shutdown()
    try:
        if control('studioRecord').window_text()=='Stop recording':press('studioRecord')
    except Exception:pass
