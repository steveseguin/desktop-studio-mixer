"""Route browser audio to real outputs; fail monitoring during stream/record."""
from pathlib import Path
import soundcard as sc
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np,psutil,ctypes,socket
root=psutil.Process(args.pid);results={};receiver=None;capture=None;logs=[]
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
clip=media_fixture()
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        data=clip.read_bytes() if self.path.startswith('/clip') else b'<body><video autoplay loop playsinline src="/clip.mp4" style="width:100vw;height:100vh"></video></body>'
        self.send_response(200);self.send_header('Content-Type','video/mp4' if self.path.startswith('/clip') else 'text/html');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
threading.Thread(target=server.serve_forever,daemon=True).start()
def free_port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
rtmp,rtsp=free_port(),free_port()
config=report/'receiver.yml';config.write_text(f'logLevel: warn\nrtspAddress: 127.0.0.1:{rtsp}\nrtspTransports: [tcp]\nrtmpAddress: 127.0.0.1:{rtmp}\nwebrtc: false\nsrt: false\nhls: false\nmoq: false\npaths:\n  all_others:\n')
def until(fn,seconds=10):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if fn():return
        time.sleep(.1)
    raise AssertionError('Condition timed out')
def worker():
    for p in root.children():
        try:
            cmd=p.cmdline()
            if '--studio-worker' in cmd and 'monitor' in cmd:return p
        except psutil.Error:pass
    return None
def levels(speakers):
    values={}
    for speaker in speakers:
        loop=sc.get_microphone(id=speaker.id,include_loopback=True)
        audio=loop.record(samplerate=48000,numframes=48000).mean(axis=1)
        chunks=audio.reshape(-1,4800)
        amplitudes=abs(np.sum(chunks*np.exp(-2j*np.pi*731*np.arange(4800)/48000),axis=1))*2/4800
        values[speaker.name]=float(np.sqrt(np.mean(amplitudes**2)))
    return values
recording=report/'monitor-failures.mkv';received=report/'receiver.mkv'
try:
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(True);scene('Main')
    time.sleep(1)
    add_source('Browser /','QA monitored browser',f'http://127.0.0.1:{server.server_port}/',transparent=None);press('takeScene');time.sleep(4)
    assert channel('QA monitored browser','Listen').get_toggle_state()
    enabled(control('monitorEnable'),False)
    speakers=sc.all_speakers();assert len(speakers)>=2,'Needs two real Windows playback endpoints'
    results['monitorOff']=levels(speakers);assert max(results['monitorOff'].values())<.001,results
    press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(recording));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
    log=(report/'receiver.log').open('wb');logs.append(log)
    receiver=subprocess.Popen([os.environ['MEDIAMTX_PATH'],str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    configure('RTMP / RTMPS',f'rtmp://127.0.0.1:{rtmp}/qa');press('studioStartOutput');wait_output(True);time.sleep(2)
    log=(report/'capture.log').open('wb');logs.append(log)
    capture=subprocess.Popen([str(ffmpeg),'-v','error','-y','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/qa','-c','copy',str(received)],stdin=subprocess.PIPE,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    control('monitorDevice').expand()
    time.sleep(.5)
    first=next(c for top in app.windows() for c in top.descendants(control_type='ListItem') if c.is_visible() and c.window_text()=='Default Windows playback device')
    output_items=[c.window_text() for c in first.parent().descendants(control_type='ListItem')]
    window.type_keys('{ESC}');results['visibleOutputChoices']=output_items
    for index,speaker in enumerate(speakers):
        label=next(x for x in output_items if speaker.name.startswith(x))
        choose_dialog(window,'monitorDevice',label);enabled(control('monitorEnable'),True);time.sleep(2)
        values=levels(speakers);results['selected-'+str(index)]=values
        assert values[speaker.name]>.005,values
        assert all(v<.001 for k,v in values.items() if k!=speaker.name),values
        press('refreshMonitorDevices');assert control('monitorDevice').selected_text()==label
        window.capture_as_image().save(str(report/('output-'+str(index)+'.png')))
    faults=[]
    for mode in ('crash','hang'):
        until(lambda:worker() is not None);victim=worker();start=time.perf_counter()
        if mode=='crash':victim.kill()
        else:victim.suspend()
        try:until(lambda:not control('monitorEnable').get_toggle_state(),8)
        finally:
            if victim.is_running():victim.kill()
        elapsed=time.perf_counter()-start
        assert control('studioRecord').window_text()=='Stop recording'
        result=ctypes.c_size_t();assert ctypes.windll.user32.SendMessageTimeoutW(window.handle,0,0,0,2,1500,ctypes.byref(result))
        # Preview remains operable during the monitor failure; Program stays live.
        scene('Break');scene('Main');window.capture_as_image().save(str(report/(mode+'.png')))
        enabled(control('monitorEnable'),True);time.sleep(1.5)
        assert levels(speakers)[speakers[-1].name]>.005
        faults.append({'fault':mode,'disabledSeconds':elapsed,'programRecordingStayedActive':True})
    results['monitorWorkerFaults']=faults
    for n in range(12):choose_dialog(window,'monitorDevice',output_items[1+n%2]);time.sleep(.2)
    enabled(control('monitorEnable'),False);time.sleep(1);results['afterSwitchesOff']=levels(speakers)
    assert max(results['afterSwitchesOff'].values())<.001
    capture.communicate(b'q\n',timeout=20);assert capture.returncode==0;capture=None
    press('studioStartOutput');wait_output(False);press('studioRecord');wait_recording(False)
    for path in (recording,received):
        decode=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,timeout=90)
        assert not decode.returncode and not decode.stderr,decode.stderr
        raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-vn','-ac','1','-ar','48000','-f','f32le','-'])
        pcm=np.frombuffer(raw,np.float32);blocks=pcm[:len(pcm)//48000*48000].reshape(-1,48000)
        rms=np.sqrt(np.mean(blocks**2,axis=1))[2:-2];assert min(rms)>.02,rms.tolist()
        results[path.name]={'fullDecode':True,'minimumOneSecondAudioRms':float(min(rms))}
    results['passed']=True
finally:
    if capture and capture.poll() is None:capture.communicate(b'q\n',timeout=20)
    if receiver and receiver.poll() is None:receiver.terminate();receiver.wait(10)
    for log in logs:log.close()
    server.shutdown();(report/'monitor-isolation.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
