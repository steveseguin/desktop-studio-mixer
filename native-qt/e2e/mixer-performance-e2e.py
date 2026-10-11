"""Profile the packaged app through real Studio workflows (private camera media).

Run from native-qt with --pid and --report. Requires the desktop requirements,
psutil and numpy. CPU percentages use one logical core = 100%, including children
in tree totals. Each scenario warms up for 5 s then samples for 30 s. Start with
the standard Opening/Main/Break show; QA sources are removed on successful exit.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import ctypes
import hashlib
import numpy as np
import psutil
import os
import win32gui
import win32process

process = psutil.Process(args.pid)
def restore_window(hwnd, _):
    if win32process.GetWindowThreadProcessId(hwnd)[1] == args.pid and win32gui.GetWindowText(hwnd) == 'Desktop Studio - Powered by VDO.Ninja':
        win32gui.ShowWindow(hwnd, 9)
    return True
win32gui.EnumWindows(restore_window, None)
window.set_focus()
window.child_window(title='Studio', control_type='TabItem').click_input()
single_canvas = os.environ.get('VIDEO_MIXER_E2E_LAYOUT') == 'single'
studio_mode(not single_canvas)
backend = os.environ.get('VIDEO_MIXER_E2E_BACKEND', 'GPU composition')
choose_dialog(window, 'compositorBackend', backend)
choose_dialog(window, 'canvasFormat', '1280x720 / 30 fps')
original_press = press
def press(name, root=window):
    if single_canvas and name in ('takeScene', 'fadeScene'): return
    original_press(name, root)
results = {'cpuConvention': 'one logical core = 100%', 'logicalCores': psutil.cpu_count(),
           'backend': backend,
           'layout': 'single' if single_canvas else 'studio',
           'exeSha256': hashlib.sha256(Path(process.exe()).read_bytes()).hexdigest(), 'scenarios': {}}

def save_results():
    (report / 'performance.json').write_text(json.dumps(results, indent=2), encoding='utf-8')

def snapshot():
    found = {}
    for p in [process] + process.children(recursive=True):
        try:
            cpu = p.cpu_times()
            found[p.pid] = (cpu.user + cpu.system, p.memory_info().rss, p.name())
        except psutil.NoSuchProcess:
            pass
    return found

def measure(name):
    time.sleep(5)
    before = snapshot()
    threads = {t.id: t.user_time+t.system_time for t in process.threads()}
    start = time.perf_counter()
    sample_start = time.time()
    samples, latency = [], []
    while time.perf_counter()-start < 30:
        time.sleep(.5)
        begin = time.perf_counter()
        response = ctypes.c_size_t()
        ok = ctypes.windll.user32.SendMessageTimeoutW(window.handle, 0, 0, 0, 2, 2000, ctypes.byref(response))
        assert ok, 'Studio UI did not respond within 2 seconds'
        latency.append((time.perf_counter()-begin)*1000)
        samples.append(snapshot())
    duration = time.perf_counter()-start
    after = samples[-1]
    cpu = {pid: max(0, entry[0]-before.get(pid, (0,))[0])/duration*100 for pid, entry in after.items()}
    result = {'sampleStartEpoch': sample_start, 'seconds': round(duration, 2), 'parentCpuPercent': round(cpu[process.pid], 2),
              'treeCpuPercent': round(sum(cpu.values()), 2),
              'treeRssMiB': round(sum(v[1] for v in after.values())/2**20, 1),
              'treeRssGrowthMiB': round((sum(v[1] for v in after.values())-sum(v[1] for v in before.values()))/2**20, 1),
              'uiRoundTripP95Ms': round(float(np.percentile(latency, 95)), 2),
              'uiRoundTripMaxMs': round(max(latency), 2),
              'processes': [{'name': after[pid][2], 'pid': pid, 'cpuPercent': round(value, 2)} for pid,value in cpu.items()],
              'threads': sorted([{'id': t.id, 'cpuPercent': round((t.user_time+t.system_time-threads.get(t.id, 0))/duration*100, 2)}
                                 for t in process.threads()], key=lambda t: -t['cpuPercent'])[:8],
              'selectedSourceHealth': control('sourceHealth').window_text()}
    results['scenarios'][name] = result
    save_results()
    print(name, json.dumps({k: v for k,v in result.items() if k not in ('processes', 'threads')}), flush=True)

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'''<!doctype html><style>body{margin:0;background:#24b46a}canvas{width:100vw;height:100vh}</style><canvas id=c width=1280 height=720></canvas>
<script>const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();o.frequency.value=731;o.connect(g);g.connect(a.destination);g.gain.value=.08;o.start();a.resume();
const x=c.getContext('2d');function draw(){let n=Math.floor(a.currentTime*30);x.fillStyle='#24b46a';x.fillRect(0,0,1280,720);x.fillStyle='white';x.font='48px sans-serif';x.fillText('Performance fixture '+n,60,360);x.fillStyle='rgb('+n%255+',50,200)';x.fillRect(50,50,100,100);requestAnimationFrame(draw)}draw();</script>'''
        if self.path.startswith('/overlay'):
            body = b'<!doctype html><style>body{margin:0;background:transparent}div{position:absolute;bottom:5%;left:5%;background:#ee2255;color:white;font:32px sans-serif;padding:15px}</style><div>Performance overlay</div>'
        self.send_response(200); self.send_header('Content-Type','text/html'); self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self,*_): pass

server = http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    scene('Opening'); press('takeScene')
    remove_sources(('QA ', 'Social chat'))
    add_source('Title', 'QA profile slate', 'Starting soon')
    scene('Break'); add_source('Color', 'QA profile break', '#183657')
    scene('Opening'); press('takeScene')
    measure('static_title')
    scene('Main')
    add_source('Camera / webcam', 'QA profile camera', device='HD Pro Webcam C920')
    press('takeScene')
    measure('camera')
    assert np.asarray(control('previewCanvas' if single_canvas else 'programCanvas').capture_as_image()).std() > 12
    add_source('Browser /', 'QA profile browser', f'http://127.0.0.1:{server.server_port}/')
    press('layout-Picture in picture'); press('takeScene')
    measure('camera_and_browser')
    press('socialChat')
    prompt = window.child_window(title='Social Stream Ninja', control_type='Window')
    prompt.child_window(control_type='Edit').set_edit_text(f'http://127.0.0.1:{server.server_port}/overlay')
    prompt.child_window(title='OK',control_type='Button').invoke()
    press('takeScene')
    measure('camera_browser_overlay')
    # An audio-only layer must not intercept a click on the visible camera.
    add_source('Microphone / audio input', 'QA profile mic', device='HD Pro Webcam C920')
    canvas = control('previewCanvas')
    canvas.click_input(coords=(canvas.rectangle().width()//3, canvas.rectangle().height()//3))
    time.sleep(.5)
    selected = control('mixerSources').get_selection()
    results['canvasSelectionWithMicOnTop'] = [row.name for row in selected]
    # Restore selection for comparable overlay FPS reporting.
    source_row('Social chat').select()
    press('takeScene')
    recording = report / f'profile-{int(time.time())}.mkv'
    press('studioRecord')
    dialog = window.child_window(title='Record Program',control_type='Window'); dialog.wait('visible')
    edit = next(c for c in dialog.descendants(control_type='Edit') if c.is_visible() and c.element_info.automation_id=='1001')
    edit.set_edit_text(str(recording)); dialog.child_window(title='Save',control_type='Button').invoke()
    deadline=time.time()+30
    while time.time()<deadline and not control('recordingStatus').window_text().startswith('Recording:'): time.sleep(.3)
    assert control('recordingStatus').window_text().startswith('Recording:')
    measure('mixed_recording')
    scene('Break'); press('fadeScene'); time.sleep(2)
    press('studioRecord'); time.sleep(4)
    assert recording.stat().st_size > 10000
    results['recording'] = str(recording)
    measure('off_air_sources')
    ffmpeg = Path(__file__).resolve().parents[1] / 'dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe'
    metadata = subprocess.run([str(ffmpeg),'-hide_banner','-i',str(recording)],capture_output=True,text=True).stderr
    assert 'Video: h264' in metadata and '1280x720' in metadata and '30 fps' in metadata, metadata
    assert 'Audio: aac' in metadata and '48000 Hz, stereo' in metadata, metadata
    raw = subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-an','-vf','scale=160:90','-pix_fmt','rgb24','-f','rawvideo','pipe:1'])
    frames = np.frombuffer(raw,dtype=np.uint8).reshape(-1,90,160,3)
    raw = subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1'])
    audio = np.frombuffer(raw,dtype=np.float32)
    sample = audio[5*48000:6*48000]; clock = np.arange(len(sample))/48000
    tone = float(abs(np.sum(sample*np.exp(-2j*np.pi*731*clock)))*2/len(sample))
    changed = float(np.mean(np.any(frames[1:900]!=frames[:899],axis=(1,2,3))))
    assert len(frames) > 900 and tone > .03 and changed > .5
    near(frames[-1,22,80].tolist(),(24,54,87))
    results['decodedOutput'] = {'frames':len(frames),'audioSeconds':len(audio)/48000,
                                'tone731Hz':tone,'changedFrameFraction':changed,'finalBreakFrame':'passed'}
    remove_sources(('QA profile', 'Social chat'))
    scene('Opening'); press('takeScene')
    results['cleanup'] = 'passed'
finally:
    server.shutdown(); save_results()
