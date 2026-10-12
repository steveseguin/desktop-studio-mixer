"""Packaged desktop production workflows, using real devices and a local SRT receiver.

Requires the desktop UI requirements plus numpy and SoundCard. Recordings and
screenshots may contain the local camera/microphone; keep the report folder private.
"""
from pathlib import Path
import soundcard as sc
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
import numpy as np
import re
import socket
import sys

ffmpeg = Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
results = {}
children = []
window.child_window(title='Studio', control_type='TabItem').click_input()
studio_mode()

def source_row(name):
    return next(c for c in control('mixerSources').descendants(control_type='ListItem') if c.window_text().startswith(name+'  ['))

def choose_dialog(dialog, name, text):
    # Window-capture fixtures can take foreground focus. Focus the actual
    # dialog before using pointer input on its popup.
    dialog.set_focus()
    combo = control(name, dialog); combo.expand(); time.sleep(.2)
    next(c for top in app.windows() for c in top.descendants(control_type='ListItem')
         if c.is_visible() and text in c.window_text()).click_input()
    time.sleep(.15)

def add_source(kind, name, value='', device=None):
    press('addSource'); dialog = window.child_window(title='Add source', control_type='Window'); dialog.wait('visible')
    choose_dialog(dialog, 'sourceKind', kind)
    control('sourceName', dialog).set_edit_text(name)
    if value: control('sourceValue', dialog).set_edit_text(value)
    if device: choose_dialog(dialog, 'sourceDevice', device)
    dialog.child_window(title='OK', control_type='Button').invoke(); time.sleep(2)

def remove(name):
    source_row(name).select(); press('removeSource')

def channel(name, label):
    for text in window.descendants(control_type='Text'):
        if text.window_text() == name:
            parent = text.parent()
            return next(c for c in parent.descendants(control_type='CheckBox') if c.window_text() == label)
    raise AssertionError((name,label))

def enabled(box, value):
    if bool(box.get_toggle_state()) != value: box.toggle()

def wait_output(active):
    deadline = time.time()+25
    while time.time()<deadline:
        label = control('studioStartOutput').window_text()
        if active and label in ('STOP', 'STOP RECORDING'): return
        if not active and label in ('GO LIVE', 'START RECORDING'): return
        time.sleep(.3)
    raise AssertionError(control('studioOutputStatus').window_text())

def configure(protocol, destination):
    press('mixerOutput'); select('outputSelect',protocol)
    control('outputUrlInput').set_edit_text(str(destination))
    select('resolutionSelect','1280x720'); select('fpsSelect','30')
    window.child_window(title='Studio',control_type='TabItem').click_input()

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        # Beeps and flashes use the same audio clock, allowing measured A/V alignment.
        body = b'''<!doctype html><style>body{margin:0;background:#24b46a}canvas{width:100vw;height:100vh}</style><canvas id=c width=1280 height=720></canvas>
<script>const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();o.frequency.value=731;o.connect(g);g.connect(a.destination);g.gain.value=.08;o.start();a.resume();
const x=c.getContext('2d');function draw(){let n=Math.floor(a.currentTime*30);x.fillStyle='#24b46a';x.fillRect(0,0,1280,720);x.fillStyle='white';x.font='48px sans-serif';x.fillText('Mixer production / '+n,60,360);x.fillStyle='rgb('+n%255+',50,200)';x.fillRect(50,50,100,100);requestAnimationFrame(draw)}draw();</script>'''
        if self.path.startswith('/transparent'):
            body = b'<!doctype html><style>body{margin:0;background:transparent}.chat{position:absolute;bottom:10%;left:10%;background:#ee2255;color:white;font:40px sans-serif;padding:20px}</style><div class=chat>QA chat overlay</div>'
        elif self.path.startswith('/second'):
            body = body.replace(b'frequency.value=731', b'frequency.value=997')
        self.send_response(200); self.send_header('Content-Type','text/html'); self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self,*_): pass

server = http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    scene('Break'); press('takeScene')
    remove_sources(('QA ', 'Loopback browser', 'Social chat'))
    scene('Main')
    add_source('Browser /','QA smooth browser',f'http://127.0.0.1:{server.server_port}/')
    press('takeScene'); time.sleep(5)
    near(color('programCanvas'),(36,180,106))
    # Health is a one-second sample; verify consecutive samples to avoid
    # deciding the result from an initial scheduling or UI Automation pause.
    health_control = control('sourceHealth').wrapper_object()
    browser_fps = []
    for _ in range(5):
        time.sleep(1.1)
        health = health_control.window_text()
        assert 'Windows capture' in health, health
        browser_fps.append(float(re.search(r': ([\d.]+) fps', health)[1]))
    assert float(np.median(browser_fps)) >= 20, browser_fps
    results['smooth_browser'] = {'health': health, 'fpsSamples': browser_fps}
    press('reconnectSource'); time.sleep(4); near(color('programCanvas'),(36,180,106))
    results['live_browser_reconnect']='passed'

    # Reconfigure the same source, retaining its scene placement and identity.
    press('editSource'); dialog=window.child_window(title='Edit source',control_type='Window')
    control('sourceName',dialog).set_edit_text('QA browser')
    enabled(control('browserTransparency',dialog),True)
    dialog.child_window(title='OK',control_type='Button').invoke(); time.sleep(5)
    assert 'snapshots' in control('sourceHealth').window_text()
    near(color('programCanvas'),(36,180,106)); results['transparent_capture_mode']='passed'
    press('editSource'); dialog=window.child_window(title='Edit source',control_type='Window')
    enabled(control('browserTransparency',dialog),False)
    dialog.child_window(title='OK',control_type='Button').invoke(); time.sleep(3)

    # Speaker loopback confirms off/on/muted routing rather than just meter movement.
    speaker=sc.default_speaker(); loop=sc.get_microphone(id=speaker.id,include_loopback=True)
    def tone_level(frequency=731):
        audio=loop.record(samplerate=48000,numframes=48000)
        mono=audio.mean(axis=1)
        # Device and browser clocks can shift phase across a long window.
        # RMS of short-window amplitudes detects the tone and intermittent leaks
        # without cancellation from unrelated phases.
        chunks=mono[:len(mono)//4800*4800].reshape(-1,4800)
        amplitudes=abs(np.sum(chunks*np.exp(-2j*np.pi*frequency*np.arange(4800)/48000),axis=1))*2/4800
        return float(np.sqrt(np.mean(amplitudes*amplitudes)))
    enabled(control('monitorEnable'),False); time.sleep(1); off=tone_level()
    enabled(control('monitorEnable'),True); time.sleep(1); on=tone_level()
    enabled(channel('QA browser','Mute'),True); time.sleep(1); muted=tone_level()
    enabled(channel('QA browser','Mute'),False); enabled(control('monitorEnable'),False)
    assert on>.008 and off<on*.15 and muted<on*.15,(off,on,muted)
    results['headphone_monitor']={'off731Hz':off,'on731Hz':on,'muted731Hz':muted}
    # Distinct audio in two browser processes exercises the shared session scan,
    # including one root reconnecting and another being removed.
    add_source('Browser /','QA second browser',f'http://127.0.0.1:{server.server_port}/second')
    press('takeScene'); time.sleep(2)
    both_off = [tone_level(), tone_level(997)]
    enabled(control('monitorEnable'),True); time.sleep(1)
    both_on = [tone_level(), tone_level(997)]
    enabled(control('monitorEnable'),False)
    source_row('QA browser').select(); press('reconnectSource'); time.sleep(4)
    after_reconnect = [tone_level(), tone_level(997)]
    scene('Break'); press('takeScene'); remove('QA second browser')
    scene('Main'); press('takeScene'); time.sleep(1)
    after_remove = tone_level()
    enabled(control('monitorEnable'),True); time.sleep(1); remaining_on = tone_level()
    enabled(control('monitorEnable'),False)
    assert max(both_off + after_reconnect + [after_remove]) < .001, (both_off, after_reconnect, after_remove)
    assert min(both_on + [remaining_on]) > .008, (both_on, remaining_on)
    results['shared_browser_muting'] = {'off':both_off, 'on':both_on, 'afterReconnect':after_reconnect,
                                       'afterRemovalOff':after_remove, 'remainingMonitorOn':remaining_on}
    add_source('Desktop audio','QA desktop')
    enabled(control('monitorEnable'),True); time.sleep(2); with_desktop=tone_level()
    enabled(channel('QA browser','Mute'),True);time.sleep(1.5);desktop_muted=tone_level()
    enabled(channel('QA browser','Mute'),False)
    scene('Break'); press('takeScene'); time.sleep(1); off_air=tone_level()
    enabled(control('monitorEnable'),False)
    assert with_desktop>.008 and desktop_muted<.001,(with_desktop,desktop_muted)
    assert off_air>.008,off_air
    results['desktop_excludes_monitor_and_off_air_listen']={'withDesktop':with_desktop,'mutedWithDesktop':desktop_muted,'offAir':off_air}
    remove('QA desktop'); scene('Main'); press('takeScene')

    # A transparent chat source must preserve the video beneath it.
    press('socialChat'); prompt=window.child_window(title='Social Stream Ninja',control_type='Window')
    prompt.child_window(control_type='Edit').set_edit_text(f'http://127.0.0.1:{server.server_port}/transparent')
    prompt.child_window(title='OK',control_type='Button').invoke(); time.sleep(3)
    near(color('previewCanvas'),(36,180,106)); press('takeScene')
    image=control('programCanvas').capture_as_image()
    pixels=np.asarray(image.convert('RGB'))
    assert np.any((pixels[:,:,0]>180) & (pixels[:,:,1]<80) & (pixels[:,:,2]<140)), 'Chat was not rendered'
    results['transparent_chat_overlay']='passed'

    fixture=subprocess.Popen([sys.executable,'-B',str(Path(__file__).with_name('mixer-sources-e2e.py')),'--fixture'],creationflags=subprocess.CREATE_NO_WINDOW)
    children.append(fixture); time.sleep(2)
    add_source('Game / window','QA game',device='Desktop Studio QA Window')
    press('takeScene'); near(color('programCanvas'),(196,58,70)); results['native_game_capture']='passed'
    press('reconnectSource')
    dialog=window.child_window(title='Edit source',control_type='Window')
    dialog.child_window(title='OK',control_type='Button').invoke(); time.sleep(3)
    near(color('programCanvas'),(196,58,70)); results['explicit_window_reconnect']='passed'
    add_source('Camera / webcam','QA webcam',device='HD Pro Webcam C920')
    time.sleep(5); camera=np.asarray(control('previewCanvas').capture_as_image().convert('RGB'))
    assert camera.std()>12, 'Camera has no useful picture'
    assert 'fps received' in control('sourceHealth').window_text(),control('sourceHealth').window_text()
    results['physical_webcam']={'device':'HD Pro Webcam C920','pixelStdDev':float(camera.std()),'status':control('sourceHealth').window_text()}
    add_source('Microphone / audio input','QA microphone',device='HD Pro Webcam C920')
    assert not channel('QA microphone','Listen').get_toggle_state()
    results['physical_microphone']=control('sourceHealth').window_text()
    # Use a real mix of webcam, window, browser and mic, then switch scenes while recording.
    press('layout-Picture in picture'); press('takeScene')
    recording=report/f'production-{int(time.time())}.mkv'
    configure('Record to file (MKV)',recording); press('studioStartOutput'); wait_output(True)
    time.sleep(7); scene('Break'); press('fadeScene'); time.sleep(3)
    press('studioStartOutput'); wait_output(False); time.sleep(2)
    assert recording.exists() and recording.stat().st_size>10000
    results['production_recording']=str(recording)
    # Remove physical sources before the network-output check.
    for name in ['QA webcam','QA microphone','QA game','Social chat']: remove(name)
    scene('Main'); press('takeScene')

    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reservation:
        reservation.bind(('127.0.0.1',0)); port=reservation.getsockname()[1]
    received=report/f'srt-received-{int(time.time())}.mkv'
    log=(report/'srt-receiver.log').open('w')
    receiver=subprocess.Popen([str(ffmpeg),'-hide_banner','-y','-i',f'srt://127.0.0.1:{port}?mode=listener&latency=120000',
        '-t','10','-map','0','-c','copy',str(received)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    children.append(receiver); time.sleep(1)
    configure('SRT (caller)',f'srt://127.0.0.1:{port}'); press('studioStartOutput'); wait_output(True)
    receiver.wait(timeout=35); press('studioStartOutput'); wait_output(False); log.close()
    assert receiver.returncode==0 and received.stat().st_size>10000,(receiver.returncode,received)
    meta=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(received)],capture_output=True,text=True).stderr
    assert 'Video: h264' in meta and 'Audio: aac' in meta,meta
    raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(received),'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1'])
    samples=np.frombuffer(raw,dtype=np.float32)[48000:96000]; t=np.arange(len(samples))/48000
    tone=float(abs(np.sum(samples*np.exp(-2j*np.pi*731*t)))*2/len(samples))
    assert tone>.03,tone
    results['private_srt_receiver']={'path':str(received),'video':'H.264','audio':'AAC','tone731Hz':tone}
    scene('Break'); press('takeScene'); remove('QA browser'); scene('Opening'); press('takeScene')
    window.capture_as_image().save(str(report/'studio-phase2.png'))
finally:
    server.shutdown()
    for child in children:
        if child.poll() is None: child.terminate(); child.wait(timeout=5)
    (report/'production-workflow.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
