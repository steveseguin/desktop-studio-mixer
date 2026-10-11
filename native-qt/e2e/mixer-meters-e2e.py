"""Check visible audio levels and scroll boundaries in the packaged Studio."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np
import win32gui, win32process

def restore(hwnd, _):
    if win32process.GetWindowThreadProcessId(hwnd)[1] == args.pid and win32gui.GetWindowText(hwnd).startswith('Video Mixer - Powered'):
        win32gui.ShowWindow(hwnd, 9)
    return True
win32gui.EnumWindows(restore,None)
window.set_focus(); window.child_window(title='Studio',control_type='TabItem').click_input()
studio_mode()

level = .02
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/level': body = str(level).encode(); mime = 'text/plain'
        else:
            body = b'''<!doctype html><style>body{background:#183657;color:white;font:48px sans-serif}</style>Audio meter fixture
<script>const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();o.frequency.value=440;g.gain.value=.02;o.connect(g);g.connect(a.destination);o.start();a.resume();setInterval(async()=>g.gain.value=Number(await(await fetch('/level')).text()),100);</script>'''
            mime = 'text/html'
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*_): pass

def meter(name):
    return window.child_window(title=name+' audio level (dBFS)',control_type='ProgressBar').wrapper_object()

def reading(name): return meter(name).iface_range_value.CurrentValue

def pixels(name):
    window.set_focus()
    return np.asarray(meter(name).capture_as_image().convert('RGB'))

def has_color(image,color):
    return int(np.sum(np.all(np.abs(image.astype(int)-np.asarray(color))<4,axis=2)))

def surface(name, root=window):
    root.set_focus(); time.sleep(.15)
    target=control(name,root)
    image=np.asarray(target.capture_as_image().convert('RGB'))
    parent=np.asarray(target.parent().capture_as_image().convert('RGB'))
    outlined=has_color(image,(80,102,123))>50
    contrasted=has_color(image,(16,27,39))>500 and np.max(np.abs(image[2,2].astype(int)-parent[2,2].astype(int)))>8
    assert outlined or contrasted, 'Missing scroll boundary: '+name

results={}
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    scene('Opening');press('takeScene');remove_sources(('QA meter',));scene('Main')
    add_source('Browser /','QA meter tone',f'http://127.0.0.1:{server.server_port}/')
    time.sleep(3);press('takeScene');time.sleep(1)
    # Exercise the relocated audio panel in the default single-canvas layout.
    studio_mode(False)
    low=reading('QA meter tone');assert -40<low<-25,low
    meter_rect=meter('QA meter tone').rectangle();scroll_rect=control('audioTracksScroll').rectangle()
    assert scroll_rect.top < meter_rect.top and meter_rect.bottom < scroll_rect.bottom, 'Track meter is clipped'
    assert scroll_rect.left < meter_rect.left and meter_rect.right < scroll_rect.right-15, 'Track meter is clipped horizontally'
    assert has_color(pixels('QA meter tone'),(57,217,154))>30
    results['quiet_signal_visible_dbfs']=low
    level=.4;time.sleep(1.5)
    medium=reading('QA meter tone');assert -12<medium<-5,medium
    assert has_color(pixels('QA meter tone'),(239,189,80))>20
    level=.8;time.sleep(1.5)
    high=reading('QA meter tone');assert -5<high<=0,high
    assert has_color(pixels('QA meter tone'),(255,89,107))>10
    results['amber_and_red_levels']=[medium,high]
    window.capture_as_image().save(str(report/'audio-meter-live.png'))
    active_meter=meter('QA meter tone'); window.set_focus()
    level=.02;time.sleep(.35)
    # Resolve UI Automation before lowering the signal: a fresh tree walk can
    # outlast the one-second hold and measure the later decay instead.
    held_image=active_meter.capture_as_image().convert('RGB')
    held_image.save(str(report/'peak-held.png'))
    held=np.asarray(held_image)
    locations=np.where(np.all(np.abs(held.astype(int)-np.asarray((242,247,252)))<4,axis=2))
    assert len(locations[1]) and locations[1].max()>held.shape[1]*.85
    results['peak_hold']='passed'
    level=.8;time.sleep(.5)
    gain=window.child_window(title='QA meter tone volume',control_type='Slider')
    gain.iface_range_value.SetValue(200);time.sleep(.5)
    assert reading('QA meter tone')==0 and has_color(pixels('QA meter tone'),(255,101,116))>5
    results['clip_indicator']='passed'
    gain.iface_range_value.SetValue(100)
    enabled(channel('QA meter tone','Mute'),True);time.sleep(2.5)
    assert reading('QA meter tone')==-60
    assert control('masterAudioMeter').iface_range_value.CurrentValue==-60
    results['mute_silences_track_and_master']='passed'
    enabled(channel('QA meter tone','Mute'),False);level=.02;time.sleep(1)
    surface('audioTracksScroll')
    add_source('Microphone / audio input','QA meter microphone',device='HD Pro Webcam C920')
    assert -60 <= reading('QA meter microphone') <= 0
    control('audioTracksScroll').wheel_mouse_input(wheel_dist=-12);time.sleep(.5)
    surface('audioTracksScroll')
    window.capture_as_image().save(str(report/'audio-tracks-scrolled.png'))
    results['track_scroll_boundary']='passed'
    add_source('Camera / webcam','QA meter camera',device='HD Pro Webcam C920')
    press('cameraSettings')
    dialog=window.child_window(title='Camera settings - QA meter camera',control_type='Window')
    surface('cameraControlsScroll',dialog)
    dialog.capture_as_image().save(str(report/'camera-scroll-border.png'))
    next(c for c in dialog.descendants(control_type='Button') if c.window_text()=='Close' and c.element_info.automation_id).invoke()
    results['camera_scroll_boundary']='passed'
    press('mixerOutput');surface('mainScrollArea')
    window.capture_as_image().save(str(report/'output-scroll-border.png'))
    results['output_scroll_boundary']='passed'
    window.child_window(title='Studio',control_type='TabItem').click_input()
    studio_mode(True)
    scene('Opening');press('takeScene');remove_sources(('QA meter',))
    results['cleanup']='passed'
finally:
    server.shutdown();(report/'meters-workflow.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
