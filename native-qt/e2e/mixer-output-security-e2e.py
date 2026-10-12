"""Exercise destination security failures and encrypted SRT in the packaged app."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import psutil,socket,numpy as np
root=psutil.Process(args.pid);results={};servers=[];receiver=None;requests=[]
secret='desktop-studio-qa-fixture-token';ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_OPTIONS(self):self.respond()
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length','0')));self.respond()
    def do_DELETE(self):self.send_response(200);self.end_headers()
    def respond(self):
        requests.append((self.server.server_port,self.path,self.command,self.headers.get('Authorization','')))
        code=200;body=b'';location=None
        if self.path.startswith('/redirect'):code=307;location=f'http://127.0.0.1:{other.server_port}/denied'
        elif self.command=='POST':
            if self.path.startswith('/oversize'):code=201;body=b'X'*(2*1024*1024);location='/session'
            else:code=403
        self.send_response(code);self.send_header('Content-Type','application/sdp');self.send_header('Content-Length',str(len(body)))
        if location:self.send_header('Location',location)
        self.end_headers()
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
    def log_message(self,*_):pass
for _ in range(2):
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=server.serve_forever,daemon=True).start();servers.append(server)
server,other=servers

def until(fn,label,timeout=40):
    end=time.time()+timeout
    while time.time()<end:
        if fn():return
        time.sleep(.25)
    raise AssertionError(label+': '+control('studioOutputStatus').window_text())
def decode(path):
    decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,text=True,timeout=60)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    pcm=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1']),np.float32)
    rms=float(np.sqrt(np.mean(pcm*pcm)));assert rms>.005,rms
    return {'fullDecode':'passed','audioRms':rms}
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',));enabled(control('guestProgramReturn'),False)
choose_dialog(window,'canvasFormat','1280x720 / 30 fps');add_source('Video / audio file','QA security media',str(media_fixture()))
try:
    path=report/'independent.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').invoke();until(lambda:control('recordingStatus').window_text().startswith('Recording:'),'recording did not start')
    for route,expected in [('denied','authentication failed'),('redirect','authentication failed'),('oversize','size limit')]:
        configure('WHIP',f'http://127.0.0.1:{server.server_port}/{route}');press('mixerOutput');control('outputSecretInput').set_edit_text(secret);window.child_window(title='Studio',control_type='TabItem').click_input()
        press('studioStartOutput');wait_output(True);until(lambda:expected in control('studioOutputStatus').window_text().lower(),'expected WHIP rejection')
        assert control('studioRecord').window_text()=='Stop recording'
        for child in root.children():assert secret not in ' '.join(child.cmdline()),'worker command line leaked token'
        results[route]=control('studioOutputStatus').window_text();press('studioStartOutput');wait_output(False)
    sent=[r for r in requests if r[0]==server.server_port];redirected=[r for r in requests if r[0]==other.server_port]
    assert sent and any(r[3]=='Bearer '+secret for r in sent)
    assert redirected and all(not r[3] for r in redirected), 'cross-origin bearer credential leaked'
    results['crossOriginCredential']='not forwarded'
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    received=report/'encrypted-srt.mkv';log=(report/'srt.log').open('w')
    receiver=subprocess.Popen([str(ffmpeg),'-hide_banner','-y','-i',f'srt://127.0.0.1:{port}?mode=listener&latency=120000&passphrase={secret}&pbkeylen=16','-t','6','-map','0','-c','copy',str(received)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    configure('SRT (caller)',f'srt://127.0.0.1:{port}');press('mixerOutput');control('outputSecretInput').set_edit_text(secret);control('outputStreamIdInput').set_edit_text('');window.child_window(title='Studio',control_type='TabItem').click_input()
    press('studioStartOutput');wait_output(True);assert receiver.wait(timeout=40)==0
    press('studioStartOutput');wait_output(False);log.close();results['encryptedSrt']=decode(received)
    assert control('studioRecord').window_text()=='Stop recording';press('studioRecord');until(lambda:'saved' in control('recordingStatus').window_text().lower(),'recording finalization');results['independentRecording']=decode(path)
finally:
    (report/'output-security.json').write_text(json.dumps(results,indent=2))
    if receiver and receiver.poll() is None:receiver.terminate();receiver.wait(timeout=5)
    for server in servers:server.shutdown()
    try:
        if control('studioRecord').window_text()=='Stop recording':press('studioRecord')
        if control('studioStartOutput').window_text()=='STOP':press('studioStartOutput');wait_output(False)
    except Exception:pass
