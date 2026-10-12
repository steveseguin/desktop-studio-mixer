"""Packaged browser GPU decode/audio, CPU exhaustion, hangs and crash containment.

Private loopback media only. Launch with QA loopback WebView2 debugging; the
caller backs up/restores the show and settings. Does not capture local devices.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import os,sys,psutil,ctypes,numpy as np
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
results={};root=psutil.Process(args.pid)
main_hwnd=window.handle
show=show_path
ffmpeg=str(Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve())
clip=media_fixture()


class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/video.mp4'):
            body=clip.read_bytes();mime='video/mp4'
        elif self.path.startswith('/fault'):
            body=b'''<!doctype html><style>body{background:red}.card{position:absolute;top:5%;left:5%;background:#ab2266;color:white;padding:12px}</style><div class=card>Isolated browser load</div>''';mime='text/html'
        else:
            body=b'''<!doctype html><style>body{margin:0;background:#663399}video{width:100vw;height:100vh;object-fit:fill}</style><video id=v autoplay loop playsinline src=/video.mp4></video>''';mime='text/html'
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*_):pass


def responsive():
    result=ctypes.c_size_t();start=time.perf_counter()
    assert ctypes.windll.user32.SendMessageTimeoutW(main_hwnd,0,0,0,2,1500,ctypes.byref(result)), 'Main application stopped responding'
    return (time.perf_counter()-start)*1000


def connect(pw,id,path):
    deadline=time.time()+25
    while time.time()<deadline:
        try:
            port=int(((Path(os.environ['LOCALAPPDATA'])/'VideoMixer/browser')/id/'EBWebView/DevToolsActivePort').read_text().splitlines()[0])
            browser=pw.chromium.connect_over_cdp(f'http://127.0.0.1:{port}',timeout=1500)
            page=next(p for c in browser.contexts for p in c.pages if path in p.url)
            page.wait_for_load_state('load',timeout=3000);return browser,page
        except Exception:time.sleep(.3)
    raise AssertionError('Browser did not recover: '+path)


def worker_for(id):
    for process in root.children():
        try:
            if '--studio-worker' not in process.cmdline() or 'browser' not in process.cmdline():continue
            if any(id in ' '.join(p.cmdline()) for p in process.children(recursive=True)):return process
        except psutil.Error:pass
    raise AssertionError('Browser worker missing')


def cpu(process):
    total=0
    for p in [process]+process.children(recursive=True):
        try:t=p.cpu_times();total+=t.user+t.system
        except psutil.Error:pass
    return total


server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=server.serve_forever,daemon=True).start()
url=f'http://127.0.0.1:{server.server_port}'
recording=False
try:
    window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))
    enabled(control('monitorEnable'),False)
    add_source('Browser /','QA browser hardware',url+'/player',transparent=None)
    hardware_id=next(s['id'] for s in json.loads(show.read_text())['sources'] if s['name']=='QA browser hardware')
    with sync_playwright() as pw:
        hardware_browser,hardware_page=connect(pw,hardware_id,'/player')
        cdp=hardware_page.context.new_cdp_session(hardware_page);properties={}
        def media_properties(event):
            for p in event['properties']:properties[p['name']]=p['value']
        cdp.on('Media.playerPropertiesChanged',media_properties);cdp.send('Media.enable')
        hardware_page.reload();hardware_page.wait_for_function('v.videoWidth>0 && v.currentTime>1',timeout=20000)
        hardware_page.wait_for_timeout(2000)
        results['hardwareDecoder']=properties
        assert properties.get('kIsPlatformVideoDecoder')=='true',properties
        results['gpu']=hardware_browser.new_browser_cdp_session().send('SystemInfo.getInfo')['gpu']['featureStatus']
        assert results['gpu']['video_decode']=='enabled' and results['gpu']['gpu_compositing']=='enabled'
        assert hardware_page.evaluate('getComputedStyle(document.body).backgroundColor')=='rgba(0, 0, 0, 0)'
        assert hardware_page.evaluate('!v.muted && v.volume===1 && !v.paused')
        print('Hardware decoder verified:',properties.get('kVideoDecoderName'),flush=True)
        healthy_worker=worker_for(hardware_id);healthy_pid=healthy_worker.pid
        add_source('Browser /','QA browser faults',url+'/fault',transparent=None)
        fault_id=next(s['id'] for s in json.loads(show.read_text())['sources'] if s['name']=='QA browser faults')
        fault_browser,page=connect(pw,fault_id,'/fault');worker=worker_for(fault_id)
        output=report/'browser-resilience.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(output));dialog.child_window(title='Save',control_type='Button').invoke();recording=True;time.sleep(3)
        # Keep the page event loop responsive but saturate many renderer threads.
        page.evaluate('''() => {const code=URL.createObjectURL(new Blob(['while(true){Math.sqrt(Math.random())}'],{type:'text/javascript'})); window.loadWorkers=Array.from({length:24},()=>new Worker(code));}''')
        time.sleep(3);begin=time.perf_counter();initial=cpu(worker);latencies=[]
        for _ in range(60):time.sleep(.2);latencies.append(responsive())
        usage=(cpu(worker)-initial)/(time.perf_counter()-begin)*100
        expected=min(4,psutil.cpu_count()*.5)*100
        assert usage<expected*1.3,(usage,expected)
        assert usage>expected*.4, 'CPU fixture did not create substantial load'
        health=control('sourceHealth').window_text()
        assert 'CPU limit active' in health,health
        results['cpuStress']={'treeCpuPercentOneCore':round(usage,2),'nominalCpuCapOneCore':expected,'uiMaxMs':round(max(latencies),2),'uiP95Ms':round(float(np.percentile(latencies,95)),2),'health':health}
        print('CPU stress:',json.dumps(results['cpuStress']),flush=True)
        page.evaluate('loadWorkers.forEach(w=>w.terminate())')
        assert worker_for(hardware_id).pid==healthy_pid
        # A renderer loop must not be mistaken for a healthy native heartbeat.
        old=worker.pid;begin=time.perf_counter()
        page.evaluate('setTimeout(()=>{while(true){}},100)')
        deadline=time.time()+28
        while time.time()<deadline and worker.is_running():responsive();time.sleep(.25)
        assert not worker.is_running(),'Hung renderer was not retired'
        fault_browser,page=connect(pw,fault_id,'/fault');worker=worker_for(fault_id)
        results['rendererHang']={'recoverySeconds':round(time.perf_counter()-begin,2),'workerReplaced':worker.pid!=old}
        assert worker.pid!=old and control('studioRecord').window_text()=='Stop recording'
        # Kill the dedicated Chromium GPU process. Siblings must survive.
        gpu=next(p for p in worker.children(recursive=True) if '--type=gpu-process' in p.cmdline())
        gpu.kill();gpu.wait(timeout=8);time.sleep(4)
        fault_browser,page=connect(pw,fault_id,'/fault')
        assert worker_for(hardware_id).pid==healthy_pid
        results['gpuProcessCrash']='source recovered; healthy browser and recording survived'
        # Fresh manual reconnect resets the retry budget before repeated crashes.
        source_row('QA browser faults').select();press('reconnectSource');time.sleep(4)
        for index in range(4):
            fault_browser,page=connect(pw,fault_id,'/fault');worker=worker_for(fault_id)
            worker.kill();worker.wait(timeout=8);time.sleep(3)
            responsive();assert control('studioRecord').window_text()=='Stop recording'
        time.sleep(2)
        assert 'Automatic reconnect paused' in control('sourceHealth').window_text(),control('sourceHealth').window_text()
        time.sleep(5)
        assert 'Automatic reconnect paused' in control('sourceHealth').window_text()
        results['restartBudget']='three automatic reconnects, then source paused after fourth crash'
        press('reconnectSource');time.sleep(4);fault_browser,page=connect(pw,fault_id,'/fault')
        results['manualReconnect']='recovered from paused state'
        assert worker_for(hardware_id).pid==healthy_pid
        # A page can allocate in Web Workers even with its UI responsive. Try
        # up to 5 GiB, exceeding the 4 GiB job commit ceiling on this QA machine.
        assert psutil.virtual_memory().available > 8*1024**3, 'Memory fault requires 8 GiB available'
        worker=worker_for(fault_id);old=worker.pid;peak_private=0
        page.evaluate('''() => {
            window.memoryTest={}; const script=`const held=[];try{for(let i=0;i<80;i++){const b=new Uint8Array(64*1024*1024);b.fill(42);held.push(b);postMessage({allocatedMiB:(i+1)*64});}}catch(e){postMessage({error:e.name});}`;
            window.memoryWorker=new Worker(URL.createObjectURL(new Blob([script],{type:'text/javascript'})));
            memoryWorker.onmessage=e=>window.memoryTest=e.data;
        }''')
        deadline=time.time()+30;last={}
        while time.time()<deadline and worker.is_running():
            current=0
            for process in [worker]+worker.children(recursive=True):
                try:current+=process.memory_info().private
                except psutil.Error:pass
            peak_private=max(peak_private,current);responsive()
            try:
                last=page.evaluate('window.memoryTest')
                if last and (last.get('error') or last.get('allocatedMiB',0)>=5120):break
            except Exception:break
            time.sleep(.1)
        assert not last or last.get('allocatedMiB',0)<5120,last
        assert peak_private<4.3*1024**3,peak_private
        if not (last and last.get('error')):
            deadline=time.time()+12
            while worker.is_running() and time.time()<deadline:responsive();time.sleep(.2)
        assert not worker.is_running() or (last and last.get('error')), 'Allocation limit did not interrupt the memory fixture'
        fault_browser,page=connect(pw,fault_id,'/fault')
        assert worker_for(hardware_id).pid==healthy_pid and control('studioRecord').window_text()=='Stop recording'
        results['memoryExhaustion']={'peakTreePrivateMiB':round(peak_private/1024**2,2),'lastAllocation':last,'workerReplaced':worker_for(fault_id).pid!=old,'healthyBrowserAndRecordingSurvived':True}
        press('studioRecord');recording=False;deadline=time.time()+25
        while time.time()<deadline and 'saved' not in control('recordingStatus').window_text().lower():time.sleep(.3)
        assert 'saved' in control('recordingStatus').window_text().lower()
        decoded=subprocess.run([ffmpeg,'-v','error','-i',str(output),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,timeout=90)
        assert not decoded.returncode and not decoded.stderr,decoded.stderr
        pcm=subprocess.run([ffmpeg,'-v','error','-i',str(output),'-vn','-ac','1','-ar','48000','-f','f32le','-'],capture_output=True,check=True,timeout=45).stdout
        audio=np.frombuffer(pcm,dtype='<f4');rms=float(np.sqrt(np.mean(audio*audio)));assert rms>.01,rms
        blocks=audio[:len(audio)//48000*48000].reshape(-1,48000);block_rms=np.sqrt(np.mean(blocks*blocks,axis=1))
        assert min(block_rms[2:-2])>.005,block_rms.tolist()
        results['recording']={'fullDecode':'passed','audioRms':rms,'minimumOneSecondAudioRms':float(min(block_rms[2:-2])),'healthyBrowserWorkerUnchanged':True}
        frames=subprocess.run([ffmpeg,'-v','error','-i',str(output),'-an','-vf','scale=64:36','-pix_fmt','gray','-fps_mode','passthrough','-f','rawvideo','-'],capture_output=True,check=True,timeout=60).stdout
        video=np.frombuffer(frames,np.uint8).reshape(-1,36*64).astype(np.int16)
        # Exclude the first/last second around recording start/stop.
        delta=np.mean(abs(np.diff(video,axis=0)),axis=1)[30:-30]
        streak=longest=0
        for change in delta:
            streak=streak+1 if change<.02 else 0;longest=max(longest,streak)
        assert longest<15,('Healthy browser froze during fault injection',longest)
        results['recording']['maximumNearlyIdenticalFrameRun']=longest+1
finally:
    if recording:
        try:press('studioRecord');time.sleep(3)
        except Exception:pass
    server.shutdown();(report/'browser-resilience.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print(json.dumps(results,indent=2),flush=True)
