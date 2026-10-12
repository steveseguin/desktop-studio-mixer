"""Inspect and record the user-provided VDO.Ninja viewer in the packaged app.

Private report only: received video/audio and connection stats are not published.
Launch with loopback-only WebView2 debugging and restore show/settings afterward.
Set VIDEO_MIXER_E2E_VDO_URL to a live viewer URL that you are authorized to test.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import os,sys,psutil,base64,collections,numpy as np
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
show=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/studio.mixer.json'
viewer_url=os.environ['VIDEO_MIXER_E2E_VDO_URL']
results={}
def decode_trace(browser,page):
    cdp=browser.new_browser_cdp_session();complete={}
    cdp.on('Tracing.tracingComplete',lambda event:complete.update(event))
    cdp.send('Tracing.start',{'categories':'gpu,media,webrtc','transferMode':'ReturnAsStream','streamFormat':'json'})
    page.wait_for_timeout(8000);cdp.send('Tracing.end');deadline=time.time()+15
    while 'stream' not in complete and time.time()<deadline:page.wait_for_timeout(100)
    chunks=[];handle=complete['stream']
    while True:
        part=cdp.send('IO.read',{'handle':handle,'size':1048576})
        chunks.append(base64.b64decode(part['data']) if part.get('base64Encoded') else part['data'].encode())
        if part.get('eof'):break
    cdp.send('IO.close',{'handle':handle});cdp.detach()
    names=collections.Counter(e.get('name','') for e in json.loads(b''.join(chunks))['traceEvents'])
    return {k:v for k,v in names.items() if 'D3D' in k and 'Decode' in k}
window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
remove_sources(('QA ',))
add_source('Browser /','QA VDO live viewer',viewer_url,transparent=None)
time.sleep(5)
source=next(s for s in json.loads(show.read_text())['sources'] if s['name']=='QA VDO live viewer')
try:
    with sync_playwright() as pw:
        port=int((show.parent/'browser'/source['id']/'EBWebView/DevToolsActivePort').read_text().splitlines()[0])
        browser=pw.chromium.connect_over_cdp(f'http://127.0.0.1:{port}')
        page=next(p for c in browser.contexts for p in c.pages if 'vdo.ninja' in p.url)
        gpu=browser.new_browser_cdp_session().send('SystemInfo.getInfo')['gpu']
        results['gpu']={k:gpu.get(k) for k in ['devices','featureStatus','videoDecoding']}
        # Capture peer references only for inspection of decoder statistics.
        page.add_init_script('''(() => { const Real=window.RTCPeerConnection; window.__qaPeers=[];
          window.RTCPeerConnection=new Proxy(Real,{construct(target,args){const pc=new target(...args); window.__qaPeers.push(pc); return pc;}}); })();''')
        page.reload();time.sleep(25)
        results['elements']=page.evaluate('''() => [...document.querySelectorAll('video,audio')].map(e=>({tag:e.tagName,muted:e.muted,volume:e.volume,paused:e.paused,width:e.videoWidth,height:e.videoHeight,time:e.currentTime,audio:e.srcObject?.getAudioTracks().map(t=>({enabled:t.enabled,muted:t.muted,readyState:t.readyState}))}))''')
        results['backgrounds']=page.evaluate('''() => [document.documentElement,document.body].map(e=>getComputedStyle(e).backgroundColor)''')
        assert results['backgrounds']==['rgba(0, 0, 0, 0)']*2
        results['stats']=page.evaluate('''async () => { const out=[]; for(const pc of window.__qaPeers) { const stats=await pc.getStats(); out.push([...stats.values()].filter(s=>['inbound-rtp','codec'].includes(s.type))); } return out; }''')
        results['health']=control('sourceHealth').window_text()
        video=[s for peer in results['stats'] for s in peer if s['type']=='inbound-rtp' and s.get('kind')=='video']
        assert video and sum(s.get('framesDecoded',0) for s in video)>0, 'No live decoded video from the requested source'
        results['defaultAudioSettings']={k:source.get(k) for k in ['muted','gain','transparent','followsScene']}
        assert source['transparent'] and not source['muted'] and source['gain']==1
        output=report/'vdo-live.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(output));dialog.child_window(title='Save',control_type='Button').invoke()
        time.sleep(2)
        root=psutil.Process(args.pid)
        def cpu_time():
            total=0
            for process in [root]+root.children(recursive=True):
                try: usage=process.cpu_times();total+=usage.user+usage.system
                except psutil.Error:pass
            return total
        begin=time.perf_counter();initial_cpu=cpu_time()
        # Windows engine counters provide independent GPU evidence even when
        # receive-only WebRTC stats suppress decoderImplementation for privacy.
        gpu_pids=[p.pid for p in psutil.Process(args.pid).children(recursive=True) if '--type=gpu-process' in p.cmdline()]
        wanted=','.join(str(pid) for pid in gpu_pids)
        query="$wanted=@("+wanted+"); (Get-Counter '\\GPU Engine(*)\\Utilization Percentage' -SampleInterval 1 -MaxSamples 12).CounterSamples | Where-Object { $_.InstanceName -match '^pid_(\\d+)_' -and $wanted -contains [int]$matches[1] } | Select-Object InstanceName,CookedValue | ConvertTo-Json -Compress"
        counters=subprocess.run(['powershell','-NoProfile','-Command',query],capture_output=True,text=True,timeout=30)
        if counters.returncode==0:
            values=json.loads(counters.stdout or '[]')
            if isinstance(values,dict): values=[values]
            results['gpuEngineSamples']=values
            results['gpuEngineCounters']={engine:round(sum(float(v['CookedValue']) for v in values if engine.lower() in v['InstanceName'].lower())/12,5) for engine in ['engtype_VideoDecode','engtype_3D','engtype_Copy']}
        else: results['gpuCounterError']=counters.stderr[-300:]
        results['recordingTreeCpuPercentOneCore']=round((cpu_time()-initial_cpu)/(time.perf_counter()-begin)*100,2)
        press('studioRecord');deadline=time.time()+25
        while time.time()<deadline and 'saved' not in control('recordingStatus').window_text().lower(): time.sleep(.3)
        assert 'saved' in control('recordingStatus').window_text().lower()
        ffmpeg=str(Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve())
        decoded=subprocess.run([ffmpeg,'-v','error','-i',str(output),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,timeout=60)
        assert not decoded.returncode and not decoded.stderr,decoded.stderr
        pcm=subprocess.run([ffmpeg,'-v','error','-i',str(output),'-vn','-ac','1','-ar','48000','-f','f32le','-'],capture_output=True,check=True,timeout=30).stdout
        audio=np.frombuffer(pcm,dtype='<f4');results['recordingAudioRms']=float(np.sqrt(np.mean(audio*audio)))
        assert results['recordingAudioRms']>.001, 'Live incoming audio did not reach the recording'
        results['recording']='fully decoded'
        if os.environ.get('VIDEO_MIXER_E2E_EXPECT_HW')=='1':
            results['webrtcHardwareDecode']=decode_trace(browser,page)
            assert results['webrtcHardwareDecode'], 'WebRTC hardware decode was not observed'
            # Verify the user's per-source metadata opt-out, its persistence,
            # and that restoring the default enables GPU decoding again.
            for preserve in [True,False]:
                source_row('QA VDO live viewer').select();press('editSource')
                dialog=window.child_window(title='Edit source',control_type='Window')
                enabled(control('browserColorMetadata',dialog),preserve)
                dialog.child_window(title='OK',control_type='Button').invoke();time.sleep(5)
                saved=next(s for s in json.loads(show.read_text())['sources'] if s['id']==source['id'])
                assert saved['preserveColorMetadata']==preserve
                port=int((show.parent/'browser'/source['id']/'EBWebView/DevToolsActivePort').read_text().splitlines()[0])
                browser=pw.chromium.connect_over_cdp(f'http://127.0.0.1:{port}')
                page=next(p for c in browser.contexts for p in c.pages if 'vdo.ninja' in p.url)
                page.wait_for_function("[...document.querySelectorAll('video')].some(v=>v.videoWidth>0 && v.currentTime>3)",timeout=25000)
                observed=decode_trace(browser,page)
                assert bool(observed)!=preserve,(preserve,observed)
                results['preserveColorMetadata' if preserve else 'restoreHardwareDefault']={'saved':preserve,'decodeEvents':observed}
        print(json.dumps({'video':video,'audioRms':results['recordingAudioRms'],'health':results['health'],'gpu':results['gpu']['featureStatus']},indent=2),flush=True)
finally:
    (report/'vdo-browser.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
