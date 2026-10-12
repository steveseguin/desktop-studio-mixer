"""Abrupt failures in the packaged app, with private recordings/receivers.

Kills/suspends only descendants of the supplied Desktop Studio process. Camera
footage and local receiver files stay in the ignored report directory.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import ctypes, os, socket, sys, psutil, numpy as np

root = psutil.Process(args.pid)
main_hwnd = window.handle  # Cache before suspending a separate UI Automation provider.
ffmpeg = Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
results = {}; children = []; server = None

def save(): (report/'isolation.json').write_text(json.dumps(results, indent=2))
def workers(kind):
    found = []
    for p in root.children(recursive=True):
        try:
            command = p.cmdline()
            if '--studio-worker' in command and command[command.index('--studio-worker')+1] == kind: found.append(p)
        except psutil.NoSuchProcess: pass
    return found

def responsive():
    response = ctypes.c_size_t(); started = time.perf_counter()
    assert ctypes.windll.user32.SendMessageTimeoutW(main_hwnd, 0, 0, 0, 2, 1500, ctypes.byref(response)), 'UI stopped responding'
    assert root.is_running()
    return (time.perf_counter()-started)*1000

def until(predicate, label, timeout=35):
    deadline = time.monotonic()+timeout
    while time.monotonic()<deadline:
        responsive()
        result = predicate()
        if result: return result
        time.sleep(.25)
    raise AssertionError(label)

def record(name):
    path = report/(name+'.mkv')
    press('studioRecord'); dialog = window.child_window(title='Record Program', control_type='Window'); dialog.wait('visible')
    dialog.child_window(title='File name:', control_type='Edit').set_edit_text(str(path))
    dialog.child_window(title='Save', control_type='Button').invoke()
    until(lambda: control('recordingStatus').window_text().startswith('Recording:'), 'recording did not start')
    return path

def stop_record():
    if control('studioRecord').window_text() == 'Stop recording': press('studioRecord')
    until(lambda: 'saved' in control('recordingStatus').window_text().lower(), control('recordingStatus').window_text())

def decode(path, tone=True):
    video = subprocess.run([str(ffmpeg), '-v', 'error', '-i', str(path), '-an', '-vf', 'scale=64:36', '-fps_mode','passthrough','-enc_time_base','1:1000000','-f', 'framemd5', '-'], capture_output=True, text=True, timeout=90)
    assert video.returncode == 0 and not video.stderr, video.stderr
    frames = [v for v in video.stdout.splitlines() if v and not v.startswith('#')]
    assert len(frames) > 20, (path, len(frames))
    pcm = np.frombuffer(subprocess.check_output([str(ffmpeg), '-v', 'error', '-i', str(path), '-vn', '-f', 'f32le', '-ac', '1', '-ar', '48000', 'pipe:1']), np.float32)
    rms = float(np.sqrt(np.mean(pcm*pcm))) if len(pcm) else 0
    if tone: assert rms > .005, rms
    return {'frames': len(frames), 'distinct': len({v.rsplit(',',1)[-1] for v in frames}), 'audioRms': rms, 'decoderWarnings': video.stderr[-300:]}

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'<!doctype html><body style="margin:0;background:#24b46a"><div id=t style="font:36px sans-serif;color:white"></div><script>setInterval(()=>t.textContent=Date.now(),33)</script>'
        self.send_response(200); self.send_header('Content-Type','text/html'); self.end_headers(); self.wfile.write(body)
    def log_message(self,*_): pass

def port():
    with socket.socket() as s: s.bind(('127.0.0.1',0)); return s.getsockname()[1]

window.set_focus(); window.child_window(title='Studio',control_type='TabItem').click_input(); studio_mode(False); scene('Main'); remove_sources(('QA ',))
enabled(control('guestProgramReturn'),False); choose_dialog(window,'canvasFormat','1280x720 / 30 fps'); choose_dialog(window,'compositorBackend','CPU composition')
try:
    server = http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture); threading.Thread(target=server.serve_forever,daemon=True).start()
    add_source('Video / audio file','QA isolated media',str(media_fixture()))
    add_source('Camera / webcam','QA isolated camera',device='HD Pro Webcam C920')
    add_source('Microphone / audio input','QA isolated mic',device='HD Pro Webcam C920')
    fixture = subprocess.Popen([sys.executable,'-B',str(Path(__file__).with_name('mixer-sources-e2e.py')),'--fixture'],creationflags=subprocess.CREATE_NO_WINDOW); children.append(fixture); time.sleep(2)
    add_source('Game / window','QA isolated window',device='Desktop Studio QA Window')
    add_source('Browser /','QA isolated browser',f'http://127.0.0.1:{server.server_port}/')
    path = record('capture-crashes'); time.sleep(3)
    record_encoders = {p.pid for p in workers('encoder')}; record_outputs = {p.pid for p in workers('output')}
    failures = []
    for kind in ['camera','audio','window','media','browser']:
        victims = workers(kind); assert victims, kind
        victim = victims[0]; old = {p.pid for p in victims}; victim.kill(); victim.wait(timeout=8)
        started = time.monotonic()
        until(lambda: any(p.pid not in old for p in workers(kind)), kind+' worker did not restart')
        time.sleep(2)
        assert control('studioRecord').window_text() == 'Stop recording'
        assert {p.pid for p in workers('encoder')} == record_encoders
        assert {p.pid for p in workers('output')} == record_outputs
        failures.append({'worker':kind,'recoveredSeconds':round(time.monotonic()-started,2),'uiRoundTripMs':round(responsive(),2)})
        results['captureFailures'] = failures; save()
    victim = workers('camera')[0]; previous = victim.pid; victim.suspend()
    until(lambda: any(p.pid != previous for p in workers('camera')), 'hung camera worker was not replaced', 30)
    results['suspendedCamera'] = 'watchdog replaced worker; recording continued'; save()
    victim = workers('browser')[0]; previous = victim.pid; victim.suspend()
    until(lambda: any(p.pid != previous for p in workers('browser')), 'hung browser host was not replaced', 30)
    results['suspendedBrowser'] = 'watchdog replaced worker; recording continued'; save()
    time.sleep(2); stop_record(); results['continuousRecording'] = decode(path); save()

    # Remove physical sources from the protocol checks, preserving the tone clip.
    for name in ['QA isolated browser','QA isolated window','QA isolated mic','QA isolated camera']: remove(name)
    rtsp,rtmp,whip,udp,srt = [port() for _ in range(5)]
    config = report/'mediamtx.yml'; config.write_text(f'''logLevel: info
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
    log = (report/'server.log').open('w')
    mtx = subprocess.Popen([os.environ['MEDIAMTX_PATH'],str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW); children.append(mtx)
    protocols = [('RTMP / RTMPS',f'rtmp://127.0.0.1:{rtmp}/qa-rtmp','qa-rtmp'),
                 ('SRT (caller)',f'srt://127.0.0.1:{srt}?streamid=publish:qa-srt','qa-srt'),
                 ('WHIP',f'http://127.0.0.1:{whip}/qa-whip/whip','qa-whip')]
    results['protocolFailures'] = {}
    def receive(name,label):
        path = report/(name+'-'+label+'.mkv')
        deadline = time.monotonic()+50
        while True:
            result = subprocess.run([str(ffmpeg),'-hide_banner','-y','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/{name}','-t','4','-map','0','-c','copy',str(path)],capture_output=True,text=True,timeout=30)
            if result.returncode == 0:
                try: return decode(path)
                except (AssertionError,subprocess.CalledProcessError) as error:
                    # A receiver can attach to the retiring session between a
                    # muxer crash and reconnect and finish with no video packets.
                    if time.monotonic() >= deadline: raise
            elif time.monotonic() >= deadline: raise AssertionError(result.stderr)
            responsive(); time.sleep(.5)
    for label,url,name in protocols:
        path = record(name+'-independent'); time.sleep(2)
        recording_outputs = {p.pid for p in workers('output')}; recording_encoders = {p.pid for p in workers('encoder')}
        configure(label,url); press('studioStartOutput'); wait_output(True)
        until(lambda: 'Publishing to' in control('studioOutputStatus').window_text(), 'publisher did not become live')
        evidence = {'initial':receive(name,'initial')}
        victim = next(p for p in workers('encoder') if p.pid not in recording_encoders); old_encoder = victim.pid
        victim.kill(); victim.wait(timeout=8)
        until(lambda: any(p.pid not in recording_encoders and p.pid != old_encoder for p in workers('encoder')), 'publisher encoder did not restart')
        evidence['afterEncoderCrash'] = receive(name,'encoder-crash')
        publisher_encoders = {p.pid for p in workers('encoder')} - recording_encoders
        victim = next(p for p in workers('encoder') if p.pid in recording_encoders); old_encoder = victim.pid
        victim.kill(); victim.wait(timeout=8)
        until(lambda: len(workers('encoder')) == 2 and all(p.pid != old_encoder for p in workers('encoder')), 'recording encoder did not restart')
        time.sleep(3); assert control('studioRecord').window_text() == 'Stop recording'
        recording_encoders = {p.pid for p in workers('encoder')} - publisher_encoders
        evidence['recordingEncoderCrash'] = 'recording remained active; publisher continued'
        if label != 'WHIP':
            muxer = next(p for p in workers('output') if p.pid not in recording_outputs).children()[0]
            assert muxer.name().lower() == 'ffmpeg.exe'; muxer.kill(); muxer.wait(timeout=8)
            evidence['afterMuxerCrash'] = receive(name,'muxer-crash')
        mtx.terminate(); mtx.wait(timeout=8); time.sleep(2)
        mtx = subprocess.Popen([os.environ['MEDIAMTX_PATH'],str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW); children.append(mtx)
        evidence['afterReceiverRestart'] = receive(name,'receiver-restart')
        victim = next(p for p in workers('output') if p.pid not in recording_outputs); old = victim.pid
        victim.kill(); victim.wait(timeout=8)
        until(lambda: any(p.pid not in recording_outputs and p.pid != old for p in workers('output')), 'publisher was not replaced')
        until(lambda: 'Publishing to' in control('studioOutputStatus').window_text(), 'publisher did not recover')
        evidence['afterPublisherCrash'] = receive(name,'recovered')
        if label == 'RTMP / RTMPS':
            victim = next(p for p in workers('output') if p.pid not in recording_outputs); old = victim.pid; victim.suspend()
            until(lambda:any(p.pid not in recording_outputs and p.pid != old for p in workers('output')), 'hung publisher was not replaced',30)
            evidence['afterPublisherHang'] = receive(name,'publisher-hang')
        assert control('studioRecord').window_text() == 'Stop recording'
        assert recording_encoders.issubset({p.pid for p in workers('encoder')})
        # Abrupt recording failure must not interrupt this publishing session.
        victim = next(p for p in workers('output') if p.pid in recording_outputs); victim.kill(); victim.wait(timeout=8)
        until(lambda: control('studioRecord').window_text() == 'Record...', 'recording did not report failure')
        assert 'saved' not in control('recordingStatus').window_text().lower()
        evidence['afterRecordingCrash'] = receive(name,'recording-crash')
        evidence['recordingFailure'] = control('recordingStatus').window_text()
        old_size = path.stat().st_size; recovery = record(name+'-new-recording'); time.sleep(3); stop_record()
        assert path.stat().st_size == old_size, 'old recording was modified'
        evidence['newRecording'] = decode(recovery)
        failed_file = record(name+'-muxer-failure'); time.sleep(3)
        # The newly created output is the newest worker; its only child is its private muxer.
        recorder_output = max(workers('output'), key=lambda p:p.create_time())
        muxer = next(p for p in recorder_output.children() if p.name().lower()=='ffmpeg.exe'); muxer.kill(); muxer.wait(timeout=8)
        until(lambda: control('studioRecord').window_text() == 'Record...', 'muxer failure did not stop recording')
        assert 'saved' not in control('recordingStatus').window_text().lower()
        evidence['recordingMuxerFailure'] = control('recordingStatus').window_text()
        evidence['afterRecordingMuxerCrash'] = receive(name,'recording-muxer-crash')
        press('studioStartOutput'); wait_output(False)
        results['protocolFailures'][label] = evidence; save()
    log.close()
finally:
    save()
    try:
        if control('studioRecord').window_text() == 'Stop recording': press('studioRecord')
        if control('studioStartOutput').window_text() == 'STOP': press('studioStartOutput'); wait_output(False)
    except Exception: pass
    if server: server.shutdown()
    for child in children:
        if child.poll() is None: child.terminate(); child.wait(timeout=10)
