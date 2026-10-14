"""Measure actual frame identity and A/V timing while owned components fail.

Private RTMP proxy restores bandwidth without closing sockets. A healthy media
clock keeps running through browser CPU stress/crash/hang and publisher failure.
Video carries frame IDs; audio pulses carry the corresponding second in their
frequency, so nearest identical periodic pulses cannot hide a large sync error.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import re, sys, base64
sys.path.insert(0, str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright

os.environ['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = '--remote-debugging-port=0 --remote-debugging-address=127.0.0.1'
results = {'executableSha256': hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(), 'phases': [], 'samples': []}
root = receiver = proxy = log = None
stop = threading.Event(); throttle = threading.Event(); connections = []; cpu_previous = {}

def port():
    with socket.socket() as sock: sock.bind(('127.0.0.1', 0)); return sock.getsockname()[1]

def ff(*options):
    r = run([ffmpeg, *options], timeout=180)
    assert r.returncode == 0, r.stderr.decode(errors='replace')[-2000:]
    return r

def workers(kind):
    found = []
    for process in root.children():
        try:
            cmd = process.cmdline()
            if '--studio-worker' in cmd and cmd[cmd.index('--studio-worker')+1] == kind: found.append(process)
        except psutil.Error: pass
    return found

def sample():
    began = time.perf_counter(); s = state(); latency = (time.perf_counter()-began)*1000
    assert s['recording']['program_active'], s['recording']
    assert {p.pid for p in workers('media')} == media_pids, 'Unaffected media worker restarted'
    values = {'seconds': time.monotonic()-started, 'apiMs': latency,
              'dropped': s['recording']['dropped_frames'], 'output': s['output'],
              'sources': s['sources'], 'privateMiB': 0, 'handles': 0, 'cpuDeltaByRole': {}, 'workerPids': {}}
    for p in [root, *root.children(recursive=True)]:
        try:
            cmd = p.cmdline(); role = 'main' if p.pid == root.pid else p.name()
            if '--studio-worker' in cmd:
                role = cmd[cmd.index('--studio-worker')+1]
                values['workerPids'].setdefault(role, []).append(p.pid)
            cpu = p.cpu_times(); total = cpu.user+cpu.system; key = (p.pid, p.create_time())
            if key in cpu_previous:
                values['cpuDeltaByRole'][role] = values['cpuDeltaByRole'].get(role, 0)+max(0, total-cpu_previous[key])
            cpu_previous[key] = total
            values['privateMiB'] += p.memory_info().private/2**20; values['handles'] += p.num_handles()
        except psutil.Error: pass
    results['samples'].append(values)
    (report/'fault-quality.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return s

def dwell(seconds):
    end = time.monotonic()+seconds
    while time.monotonic() < end: sample(); time.sleep(.5)

def phase(name, action, settle=15):
    entry = {'name': name, 'startSeconds': time.monotonic()-started}
    results['phases'].append(entry); print('PHASE '+name, flush=True)
    action(); entry['actionDoneSeconds'] = time.monotonic()-started
    dwell(settle); entry['endSeconds'] = time.monotonic()-started
    image('program', name+'.png')

def relay(source, dest, limited):
    try:
        while not stop.is_set():
            data = source.recv(4096)
            if not data: break
            if limited and throttle.is_set(): time.sleep(.125)
            dest.sendall(data)
    except OSError: pass
    finally:
        for sock in (source, dest):
            try: sock.shutdown(socket.SHUT_RDWR); sock.close()
            except OSError: pass

def accept_proxy():
    while not stop.is_set():
        try:
            client, _ = proxy.accept(); upstream = socket.create_connection(('127.0.0.1', rtmp))
            connections.extend([client, upstream])
            for source, dest, limited in [(client, upstream, True), (upstream, client, False)]:
                threading.Thread(target=relay, args=(source, dest, limited), daemon=True).start()
        except OSError: break

def receive(name):
    path = report/(name+'.mkv'); began = time.monotonic()
    for attempt in range(12):
        r = run([ffmpeg, '-v', 'error', '-y', '-timeout', '15000000', '-rtsp_transport', 'tcp',
                 '-i', f'rtsp://127.0.0.1:{rtsp}/quality', '-t', '14', '-map', '0', '-c', 'copy', path], timeout=40)
        if r.returncode == 0 and path.stat().st_size > 50000:
            picture = api('/studio/screenshot', {'target': 'program', 'max_width': 640})
            rgb = Image.open(io.BytesIO(base64.b64decode(picture['data']))).convert('L')
            rgb.save(report/(name+'-live-at-capture-end.png'))
            frame = np.asarray(rgb)
            live_id = sum(int(frame[8:16, n*20+6:n*20+14].mean() > 128)*(1 << n) for n in range(16))
            results.setdefault('receiverObservations', {})[path.name] = {'liveFrameAtCaptureEnd': live_id, 'captureSeconds': time.monotonic()-began}
            return path, time.monotonic()-began
        if b'404' not in r.stderr: raise AssertionError(r.stderr)
        sample(); time.sleep(.5)
    raise AssertionError('Receiver did not recover: '+r.stderr.decode(errors='replace'))

def analyze(path, audio_index=0):
    decoded = ff('-v', 'error', '-i', path, '-map', '0', '-fps_mode:v', 'passthrough',
                 '-enc_time_base:v', '1:1000000', '-f', 'null', '-')
    assert not decoded.stderr, decoded.stderr
    video = ff('-v', 'info', '-copyts', '-i', path, '-an', '-vf',
               'scale=640:360,crop=320:24:0:0,format=gray,showinfo', '-fps_mode', 'passthrough', '-f', 'rawvideo', '-')
    vpts = np.array([float(s) for s in re.findall(r'\bn:\s*\d+\s+pts:\s*\S+\s+pts_time:([-\d.e+]+)', video.stderr.decode(errors='replace'))])
    frames = np.frombuffer(video.stdout, np.uint8).reshape(-1, 24, 320)
    assert len(vpts) == len(frames)
    ids = sum((frames[:, 8:16, n*20+6:n*20+14].mean(axis=(1, 2)) > 128).astype(np.int64)*(1 << n) for n in range(16))
    valid = ids < 1800; assert valid.mean() > .999, float(valid.mean())
    sound = ff('-v', 'info', '-copyts', '-i', path, '-map', f'0:a:{audio_index}', '-vn', '-af',
               'aformat=sample_fmts=flt:sample_rates=48000:channel_layouts=mono,asetnsamples=n=480:p=1,ashowinfo', '-f', 'f32le', '-')
    apts = np.array([float(s) for s in re.findall(r'\bn:\d+\s+pts:\S+\s+pts_time:([-\d.e+]+)', sound.stderr.decode(errors='replace'))])
    pcm = np.frombuffer(sound.stdout, np.float32).reshape(-1, 480); assert len(apts) == len(pcm)
    loud = np.sqrt(np.mean(pcm**2, axis=1)) > .055
    starts = np.flatnonzero(loud[1:] & ~loud[:-1])+1
    matched = []; unmatched = []
    for index in starts:
        if index+9 >= len(pcm) or apts[index] < vpts[0]+1 or apts[index] > vpts[-1]-1: continue
        wave = pcm[index+1:index+9].reshape(-1)
        spectrum = abs(np.fft.rfft(wave*np.hanning(len(wave)), n=32768))
        freq = np.argmax(spectrum)*48000/32768; second = int(round((freq-600)/20))
        if not 0 <= second < 60 or abs(freq-(600+20*second)) > 7: unmatched.append(float(apts[index])); continue
        goal = second*30
        mask = (ids >= goal) & (ids < goal+8) & valid
        candidates = np.flatnonzero(mask & np.r_[True, ~mask[:-1]])
        if not len(candidates): unmatched.append(float(apts[index])); continue
        best = min(candidates, key=lambda j: abs(vpts[j]-apts[index]))
        # Match the pulse identity first. A delay near a whole second remains
        # a large measured error, rather than matching a neighboring pulse.
        offset = apts[index]-(vpts[best]-(ids[best]-goal)/30)
        matched.append({'pts': float(apts[index]), 'second': second, 'offsetMs': float(offset*1000)})
    assert len(matched) >= 8, (matched, unmatched)
    changes = (np.diff(ids)+1800) % 1800
    gaps = np.diff(vpts); stable = (vpts[1:] > vpts[0]+1) & (vpts[1:] < vpts[-1]-1)
    run_start = 0; longest = 0
    for n in range(1, len(ids)):
        if ids[n] != ids[n-1]: run_start = n
        longest = max(longest, vpts[n]-vpts[run_start])
    offsets = np.array([p['offsetMs'] for p in matched])
    out = {'file': path.name, 'fullDecode': 'clean', 'frames': len(ids), 'averageFps': float(1/gaps[stable].mean()),
           'p95FrameIntervalMs': float(np.percentile(gaps[stable], 95)*1000), 'maxFrameIntervalMs': float(gaps[stable].max()*1000),
           'duplicatePictures': int(np.sum((changes == 0) & stable)),
           'skippedSourcePictures': int(np.sum(np.maximum(0, changes[stable]-1))),
           'longestFrozenPictureMs': float(longest*1000), 'p95AbsoluteSyncMs': float(np.percentile(abs(offsets), 95)),
           'maxAbsoluteSyncMs': float(max(abs(offsets))), 'unmatchedPulses': unmatched, 'pulses': matched,
           'lastSourceFrameId': int(ids[-1])}
    assert out['averageFps'] > 28.5 and out['p95AbsoluteSyncMs'] < 120 and out['maxAbsoluteSyncMs'] < 200, out
    assert out['longestFrozenPictureMs'] < 250 and out['maxFrameIntervalMs'] < 250, out
    assert len(unmatched) <= 1, out
    return out

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'''<!doctype html><body><div style="background:#305544;color:white">Browser audio + failure fixture <span id=t></span></div><script>setInterval(()=>t.textContent=Date.now(),33);const c=new AudioContext(),o=c.createOscillator(),g=c.createGain();o.frequency.value=1731;g.gain.value=.12;o.connect(g);g.connect(c.destination);o.start();c.resume()</script>'''
        self.send_response(200); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self, *_): pass

try:
    assert args.mediamtx
    clip = report/'frame-clock.mp4'
    boxes = ['drawbox=x=0:y=0:w=320:h=24:color=black:t=fill']
    boxes += [f"drawbox=x={n*20}:y=0:w=20:h=24:color=white:t=fill:enable='mod(floor(n/{1<<n}),2)'" for n in range(16)]
    ff('-v', 'error', '-n', '-f', 'lavfi', '-i', 'testsrc2=s=640x360:r=30', '-f', 'lavfi', '-i',
       r'aevalsrc=if(lt(mod(t\,1)\,0.12)\,0.25*sin(2*PI*(600+20*floor(t))*t)\,0):s=48000',
       '-vf', ','.join(boxes), '-t', '60', '-c:v', 'libopenh264', '-b:v', '4000k', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '192k', clip)
    rtsp, rtmp = port(), port()
    config = report/'receiver.yml'; config.write_text(f'logLevel: warn\nrtspAddress: 127.0.0.1:{rtsp}\nrtspTransports: [tcp]\nrtmpAddress: 127.0.0.1:{rtmp}\nwebrtc: false\nsrt: false\nhls: false\nmoq: false\npaths:\n  all_others:\n')
    log = (report/'receiver.log').open('wb'); receiver = subprocess.Popen([args.mediamtx, str(config)], stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(.7)
    proxy = socket.socket(); proxy.bind(('127.0.0.1', 0)); proxy.listen(); threading.Thread(target=accept_proxy, daemon=True).start()
    browser_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture); threading.Thread(target=browser_server.serve_forever, daemon=True).start()
    target = cli_call('launch', '--headless', '--show', str(show)); root = psutil.Process(target['pid']); owned.append(root)
    command('monitor.set', enabled=False)
    healthy = command('source.add', kind='media', name='Frame and audio clock', locator=str(clip))['id']
    browser_id = command('source.add', kind='browser', name='Faulty browser', locator=f'http://127.0.0.1:{browser_server.server_port}/')['id']
    command('layer.set', source=browser_id, x=.6, y=.75, w=.4, h=.25)
    command('audio.set', source=browser_id, gain=.15, monitored=False)
    until(lambda: all(s['frames'] > 20 for s in state()['sources']), 'sources ready', 45)
    media_pids = {p.pid for p in workers('media')}
    output = report/'fault-quality.mkv'; command('record.start', path=str(output), audioSources=[healthy, browser_id])
    until(lambda: state()['recording']['status'].startswith('Recording:'), 'recording')
    recording_workers = {p.pid for p in workers('output')}
    command('output.configure', protocol='rtmp', url=f'rtmp://127.0.0.1:{proxy.getsockname()[1]}/quality', encoder='auto', bitrate_kbps=4000)
    command('output.start'); until(lambda: state()['output']['live'], 'publishing', 45)
    results['encoder'] = state()['output']['encoder']; started = time.monotonic()
    phase('baseline', lambda: None, 15)
    before, _ = receive('receiver-before')
    with sync_playwright() as pw:
        debug = Path(os.environ['LOCALAPPDATA'])/'VideoMixer/browser'/browser_id/'EBWebView/DevToolsActivePort'
        browser = pw.chromium.connect_over_cdp(f'http://127.0.0.1:{int(debug.read_text().splitlines()[0])}')
        page = next(p for context in browser.contexts for p in context.pages if str(browser_server.server_port) in p.url)
        def load(): page.evaluate("() => {const b=URL.createObjectURL(new Blob(['while(true){Math.sqrt(Math.random())}']));window.busy=Array.from({length:24},()=>new Worker(b));}")
        phase('browser-cpu-pressure', load, 20)
        phase('browser-crash', lambda: workers('browser')[0].kill(), 15)
        phase('browser-hang', lambda: workers('browser')[0].suspend(), 20)
    phase('network-starvation', throttle.set, 35)
    # Deliberately no socket reset: only bandwidth is restored.
    phase('bandwidth-restored', throttle.clear, 15)
    after, elapsed = receive('receiver-bandwidth-restored'); results['restoredReceiverCaptureSeconds'] = elapsed
    phase('publisher-crash', lambda: next(p for p in workers('output') if p.pid not in recording_workers).kill(), 15)
    recovered, _ = receive('receiver-publisher-recovered')
    phase('browser-and-publisher-fail-together', lambda: [workers('browser')[0].kill(), next(p for p in workers('output') if p.pid not in recording_workers).suspend()], 25)
    final, _ = receive('receiver-final')
    results['finalState'] = sample()
    logs = api('/logs/recent?lines=2000')['lines']
    (report/'worker-failure-log.txt').write_text('\n'.join(logs), encoding='utf-8')
    failures = [line for line in logs if '[Worker]' in line and 'reason=' in line]
    assert len([line for line in failures if 'kind=browser ' in line]) >= 3, failures
    assert len([line for line in failures if 'kind=output ' in line]) >= 2, failures
    assert len(failures) <= 8, 'Failure logging flooded the log'
    results['workerFailureLogs'] = failures
    command('output.stop'); until(lambda: not state()['output']['active'], 'publishing stopped')
    command('record.stop'); until(lambda: not state()['recording']['busy'], 'recording finalized', 45)
    children = root.children(recursive=True); cli_call('quit'); root.wait(timeout=30); target = None
    _, alive = psutil.wait_procs(children, timeout=15); assert not alive
    results['localTiming'] = analyze(output)
    results['namedClockTiming'] = analyze(output, 1)
    results['receiverTiming'] = [analyze(path) for path in (before, after, recovered, final)]
    for timing in results['receiverTiming']:
        observation = results['receiverObservations'][timing['file']]
        lag = ((observation['liveFrameAtCaptureEnd']-timing['lastSourceFrameId']) % 1800)/30
        observation['lastReceivedPictureAgeSeconds'] = lag
        assert lag < 3, observation
    # Verify the browser's actual recorded track, including audio after recovery.
    browser_pcm = np.frombuffer(ff('-v', 'error', '-i', output, '-map', '0:a:2', '-f', 'f32le', '-ac', '1', '-ar', '48000', '-').stdout, np.float32)
    amplitudes = []
    for samples in (browser_pcm[48000:96000], browser_pcm[-96000:-48000]):
        amplitudes.append(float(np.median([abs(samples[n:n+4800] @ np.exp(-2j*np.pi*1731*np.arange(4800)/48000))*2/4800 for n in range(0, 48000, 4800)])))
    assert min(amplitudes) > .008, amplitudes
    results['browserAudioToneBeforeAfter'] = amplitudes
    results['summary'] = {'p95ApiMs': float(np.percentile([s['apiMs'] for s in results['samples']], 95)),
                          'maxApiMs': max(s['apiMs'] for s in results['samples']),
                          'maximumPrivateMiB': max(s['privateMiB'] for s in results['samples']),
                          'healthyWorkerRestarts': 0}
    for phase_result in results['phases']:
        samples = results['samples']
        indices = [n for n, s in enumerate(samples) if n and phase_result['startSeconds']+2 < s['seconds'] < phase_result['endSeconds']]
        elapsed = sum(samples[n]['seconds']-samples[n-1]['seconds'] for n in indices)
        roles = {role for n in indices for role in samples[n]['cpuDeltaByRole']}
        phase_result['meanLogicalCoresByRole'] = {role: sum(samples[n]['cpuDeltaByRole'].get(role, 0) for n in indices)/elapsed for role in roles}
        phase_result['p95ApiMs'] = float(np.percentile([samples[n]['apiMs'] for n in indices], 95))
    results['passed'] = True
finally:
    stop.set()
    for sock in [*connections, proxy]:
        if sock:
            try: sock.close()
            except OSError: pass
    if target:
        try: cli_call('quit'); root.wait(timeout=35)
        except Exception:
            if root and root.is_running(): root.kill(); root.wait(timeout=10)
    if receiver and receiver.poll() is None: receiver.terminate(); receiver.wait(timeout=10)
    if log: log.close()
    if browser_server: browser_server.shutdown()
    for key, value in saved.items():
        if value is None:
            try: winreg.DeleteValue(settings, key)
            except FileNotFoundError: pass
        else: winreg.SetValueEx(settings, key, 0, value[1], value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None) == original_hash
    (report/'fault-quality.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
print(json.dumps({k:v for k,v in results.items() if k not in ('samples', 'localTiming', 'namedClockTiming', 'receiverTiming')}, indent=2), flush=True)
