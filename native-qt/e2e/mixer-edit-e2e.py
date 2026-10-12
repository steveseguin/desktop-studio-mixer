"""Edit a live source in the packaged app and verify recording continuity.

Uses a private loopback page; the server counts actual document loads so a
rename/no-op/audio-delay edit cannot silently reconnect the source.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import hashlib
import psutil
import numpy as np
import win32gui
from urllib.parse import urlparse

loads = []
results = {'exeSha256': hashlib.sha256(Path(psutil.Process(args.pid).exe()).read_bytes()).hexdigest()}

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        route = urlparse(self.path).path
        if route not in ('/source', '/replacement'):
            self.send_error(404)
            return
        loads.append(route)
        body = b'''<!doctype html><style>body{margin:0;background:#24b46a}#t{position:absolute;bottom:0;color:white}</style><div id=t></div>
<script>const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();
o.frequency.value=731;g.gain.value=.15;o.connect(g);g.connect(a.destination);o.start();a.resume();setInterval(()=>t.textContent=a.currentTime.toFixed(2),33);</script>'''
        if route == '/replacement':
            body = body.replace(b'#24b46a', b'#c43a46').replace(b'value=731', b'value=997')
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *_): pass

def edit(name=None, delay=None, url=None):
    press('editSource')
    dialog = window.child_window(title='Edit source', control_type='Window')
    dialog.wait('visible')
    if name is not None: control('sourceName', dialog).set_edit_text(name)
    if delay is not None: control('sourceAudioDelay', dialog).iface_range_value.SetValue(delay)
    if url is not None: control('sourceValue', dialog).set_edit_text(url)
    dialog.child_window(title='OK', control_type='Button').invoke()
    time.sleep(2)

def wait_color(expected):
    deadline = time.time()+12
    while time.time() < deadline:
        actual = color('previewCanvas')
        if max(abs(a-b) for a,b in zip(actual, expected)) <= 8: return
        time.sleep(.3)
    near(actual, expected)

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    win32gui.ShowWindow(window.handle, 9)
    window.set_focus()
    window.child_window(title='Studio', control_type='TabItem').click_input()
    studio_mode(False)
    scene('Opening'); remove_sources(('QA edit',)); scene('Main')
    base = f'http://127.0.0.1:{server.server_port}'
    add_source('Browser /', 'QA edit browser', base+'/source')
    time.sleep(3)
    near(color('previewCanvas'), (36, 180, 106))
    initial = len(loads)
    assert initial == 1, loads
    recording = report/f'source-edits-{int(time.time())}.mkv'
    configure('Record to file (MKV)', recording)
    press('studioStartOutput'); wait_output(True); time.sleep(2)
    edit()
    results['loadsAfterNoOp'] = len(loads)
    edit(name='QA edit renamed')
    results['loadsAfterRename'] = len(loads)
    assert source_row('QA edit renamed').is_visible()
    edit(delay=80)
    results['loadsAfterDelay'] = len(loads)
    near(color('previewCanvas'), (36, 180, 106))
    time.sleep(2)
    press('studioStartOutput'); wait_output(False); time.sleep(1)
    ffmpeg = Path(psutil.Process(args.pid).exe()).parent/'ffmpeg/bin/ffmpeg.exe'
    raw = subprocess.check_output([str(ffmpeg), '-v', 'error', '-i', str(recording), '-vn', '-ac', '1', '-ar', '48000', '-f', 'f32le', 'pipe:1'])
    audio = np.frombuffer(raw, dtype=np.float32)[48000:-48000]
    blocks = audio[:len(audio)//480*480].reshape(-1, 480)
    quiet = np.sqrt(np.mean(blocks*blocks, axis=1)) < .02
    longest = current = 0
    for value in quiet:
        current = current+1 if value else 0
        longest = max(longest, current)
    results['recording'] = {'path': str(recording), 'longestQuietMs': longest*10,
                            'audibleFraction': float(1-np.mean(quiet))}
    # A URL edit really must reconnect and deliver the replacement pixels.
    before = len(loads)
    edit(url=base+'/replacement')
    wait_color((196, 58, 70))
    assert len(loads) == before+1, loads
    before = len(loads); press('reconnectSource')
    deadline=time.monotonic()+15
    while len(loads)<before+1 and time.monotonic()<deadline:time.sleep(.2)
    assert len(loads) == before+1, loads
    wait_color((196, 58, 70))
    results['replacementAndExplicitReconnect'] = 'passed'
    assert all(results[k] == initial for k in ('loadsAfterNoOp', 'loadsAfterRename', 'loadsAfterDelay')), results
    # Changing delay deliberately inserts up to 80 ms; no navigation silence.
    assert longest <= 12 and np.mean(quiet) < .02, results['recording']
    results['liveEditContinuity'] = 'passed'
finally:
    if control('studioStartOutput').window_text() in ('STOP', 'STOP RECORDING'):
        press('studioStartOutput'); wait_output(False)
    scene('Opening'); remove_sources(('QA edit',))
    server.shutdown()
    (report/'edit-workflow.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2), flush=True)
