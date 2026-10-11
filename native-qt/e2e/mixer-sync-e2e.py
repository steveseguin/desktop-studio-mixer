"""Measure rendered A/V alignment using a browser's shared beep/flash clock."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
import numpy as np
window.child_window(title='Studio',control_type='TabItem').click_input()
studio_mode()
scene('Break'); press('takeScene')
remove_sources(('QA ', 'Loopback browser', 'Social chat'))
scene('Main')
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'''<!doctype html><style>body{margin:0;background:black}</style><body><script>
const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();o.frequency.value=1000;o.connect(g);g.connect(a.destination);g.gain.value=0;
for(let n=1;n<120;n++){g.gain.setValueAtTime(.15,n);g.gain.setValueAtTime(0,n+.12)}o.start();a.resume();
function frame(){document.body.style.background=a.currentTime%1<.12?'white':'black';requestAnimationFrame(frame)}frame();</script>'''
        self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*_):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
threading.Thread(target=server.serve_forever,daemon=True).start()
results={}
try:
    add('Browser /','QA sync',f'http://127.0.0.1:{server.server_port}/');time.sleep(3);press('takeScene')
    recording=report/f'sync-{int(time.time())}.mkv'
    press('studioRecord')
    dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    edit=next(c for c in dialog.descendants(control_type='Edit') if c.is_visible() and c.element_info.automation_id=='1001')
    edit.set_edit_text(str(recording));dialog.child_window(title='Save',control_type='Button').click_input()
    time.sleep(13);assert control('studioStartOutput').window_text()=='STOP RECORDING'
    press('studioStartOutput');time.sleep(4)
    ffmpeg=Path('dist/video-mixer-portable/ffmpeg/bin/ffmpeg.exe').resolve()
    audio=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1']),dtype=np.float32)
    video=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-an','-vf','crop=32:32:640:180,scale=1:1','-pix_fmt','gray','-f','rawvideo','pipe:1']),dtype=np.uint8)
    audio=audio[:len(audio)//480*480].reshape(-1,480)
    loud=np.sqrt(np.mean(audio*audio,axis=1))>.02;bright=video>128
    ae=np.flatnonzero(loud[1:]&~loud[:-1])/100
    ve=np.flatnonzero(bright[1:]&~bright[:-1])/30
    offsets=[float(ae[np.argmin(abs(ae-v))]-v) for v in ve if 1<v<10]
    assert len(offsets)>=7,(ae.tolist(),ve.tolist())
    assert max(abs(o) for o in offsets)<.2,offsets
    results={'recording':str(recording),'beepFlashPairs':len(offsets),'audioMinusVideoMs':[round(o*1000,1) for o in offsets],
        'medianAudioMinusVideoMs':round(float(np.median(offsets))*1000,1),'studioRecordDialog':'passed'}
    scene('Break');press('takeScene')
    next(c for c in control('mixerSources').descendants(control_type='ListItem') if c.window_text().startswith('QA sync')).select();press('removeSource')
    scene('Opening');press('takeScene')
finally:
    server.shutdown();(report/'sync-workflow.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print(json.dumps(results,indent=2))
