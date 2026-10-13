"""Packaged SRT/WHIP publishing through private impaired UDP paths.

No firewall or machine network changes. WHIP offer/answer candidates are
rewritten in the fixture so ICE cannot bypass the relay. Recording is separate.

STUDIO_CLOCK_FIXTURE can reuse the 60-second clock from the quality workflow.
STUDIO_UDP_PROTOCOLS defaults to srt,whip. STUDIO_UDP_DIAGNOSTIC=1 isolates
packet loss from jitter. A receiver decode failure keeps the run red and saves
the remaining clip analyses; transport survival alone is not a quality pass.
STUDIO_UDP_RECOVERY=1 also checks settled SRT reception and a clean restart.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import ast, base64, collections, heapq, random, re, select, shutil, http.client, urllib.parse

# Reuse the actual frame-ID/audio-identity decoder without launching its test.
tree = ast.parse(Path(__file__).with_name('mixer-fault-quality-e2e.py').read_text(encoding='utf-8'))
for node in tree.body:
    if isinstance(node, ast.FunctionDef) and node.name in ('ff', 'analyze'):
        exec(compile(ast.Module(body=[node], type_ignores=[]), '<shared media analysis>', 'exec'))

results = {'executableSha256': hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),
           'protocols': {}, 'samples': [], 'http': []}
root = mtx = log = relay = signal_server = None
receivers = []; initial_workers = set(); started = time.monotonic(); cpu_previous = {}

def port():
    with socket.socket() as sock: sock.bind(('127.0.0.1', 0)); return sock.getsockname()[1]

class UdpRelay:
    def __init__(self, upstream, inspect_rtp=False):
        self.front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.front.bind(('127.0.0.1', 0))
        self.back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.back.bind(('127.0.0.1', 0))
        self.upstream = ('127.0.0.1', upstream); self.client = None
        self.lock = threading.Lock(); self.stop = threading.Event(); self.queue = []; self.serial = 0
        self.random = random.Random(72941); self.mode = 'clean'; self.loss = 0; self.jitter = 0
        self.counts = collections.defaultdict(collections.Counter); self.last_sent = {}; self.errors = []
        self.inspect_rtp = inspect_rtp; self.rtp_seen = {}; self.high_sequence = {}
        self.rtp_dropped = {}; self.retransmissions = []
        self.thread = threading.Thread(target=self.loop, daemon=True); self.thread.start()

    def configure(self, mode, loss=0, jitter_ms=0):
        with self.lock:
            self.mode, self.loss, self.jitter = mode, loss, jitter_ms/1000
            if mode == 'blackout':
                self.counts[mode]['queuedDiscarded'] += len(self.queue); self.queue.clear()

    def snapshot(self):
        with self.lock:
            return {'profiles': {k: dict(v) for k,v in self.counts.items()},
                    'queuePackets': len(self.queue), 'errors': list(self.errors),
                    'retransmissions': list(self.retransmissions)}

    def loop(self):
        while not self.stop.is_set():
            try: ready, _, _ = select.select([self.front, self.back], [], [], .002)
            except (OSError, ValueError): break
            with self.lock:
                for source in ready:
                    try: data, address = source.recvfrom(65536)
                    except OSError: continue
                    if source is self.front:
                        self.client = address; dest = self.upstream; sender = self.back; direction = 'up'
                    else:
                        if address != self.upstream: self.errors.append('Unexpected upstream peer'); continue
                        dest = self.client; sender = self.front; direction = 'down'
                    count = self.counts[self.mode]; count[direction+'Packets'] += 1; count[direction+'Bytes'] += len(data)
                    # SRTP keeps the RTP header clear. Repeated source sequence
                    # numbers reveal retransmission without decrypting payloads.
                    rtp_key = None; retransmission = None
                    if self.inspect_rtp and direction == 'up' and len(data) >= 12 and data[0] >> 6 == 2 and not 192 <= data[1] <= 223:
                        key = (int.from_bytes(data[8:12], 'big'), int.from_bytes(data[2:4], 'big'))
                        rtp_key = key
                        now = time.monotonic()
                        if now-self.rtp_seen.get(key, 0) < 2: count['rtpRepeatedSequences'] += 1
                        self.rtp_seen[key] = now
                        high = self.high_sequence.get(key[0], key[1])
                        if (key[1]-high) % 65536 < 32768: high = key[1]
                        self.high_sequence[key[0]] = high
                        dropped = self.rtp_dropped.get(key)
                        if dropped and now-dropped < 2:
                            retransmission = {'mode': self.mode, 'ssrc': key[0], 'sequence': key[1],
                                              'delayMs': (now-dropped)*1000,
                                              'sequenceLag': (high-key[1]) % 65536}
                        if len(self.rtp_seen) > 8192: self.rtp_seen = {k:v for k,v in self.rtp_seen.items() if now-v < 2}
                        if len(self.rtp_dropped) > 8192: self.rtp_dropped = {k:v for k,v in self.rtp_dropped.items() if now-v < 2}
                    if dest is None: count['noClient'] += 1; continue
                    if self.random.random() < self.loss:
                        if rtp_key: self.rtp_dropped.setdefault(rtp_key, time.monotonic())
                        count[direction+'Dropped'] += 1; continue
                    if retransmission:
                        # Observation at relay ingress, not proof of receiver recovery.
                        count['droppedRtpRetransmitted'] += 1
                        if len(self.retransmissions) < 1000: self.retransmissions.append(retransmission)
                        del self.rtp_dropped[rtp_key]
                    if len(self.queue) >= 4096: count['queueOverflow'] += 1; continue
                    self.serial += 1
                    delay = self.random.random()*self.jitter
                    heapq.heappush(self.queue, (time.monotonic()+delay, self.serial, sender, dest, data, direction, self.mode))
                now = time.monotonic()
                while self.queue and self.queue[0][0] <= now:
                    _, serial, sender, dest, data, direction, mode = heapq.heappop(self.queue)
                    try:
                        sender.sendto(data, dest)
                        key = (mode, direction)
                        if serial < self.last_sent.get(key, 0): self.counts[mode][direction+'Reordered'] += 1
                        self.last_sent[key] = max(serial, self.last_sent.get(key, 0))
                        self.counts[mode][direction+'Sent'] += 1
                    except OSError: self.counts[mode]['sendErrors'] += 1

    def close(self):
        self.stop.set(); self.thread.join(timeout=3); self.front.close(); self.back.close()

def rewrite_candidates(body, destination):
    lines = body.decode().splitlines(); found = 0; out = []
    for line in lines:
        if line.startswith('a=candidate:'):
            fields = line.split()
            if fields[2].lower() != 'udp': continue
            fields[4] = '127.0.0.1'; fields[5] = str(destination)
            # Remove related/TCP candidate attributes after the host type.
            fields[7] = 'host'; line = ' '.join(fields[:8]); found += 1
        elif line.startswith('c=IN '): line = 'c=IN IP4 127.0.0.1'
        out.append(line)
    assert found, 'WHIP fixture received no UDP ICE candidates'
    return ('\r\n'.join(out)+'\r\n').encode(), found

class Signaling(http.server.BaseHTTPRequestHandler):
    def forward(self):
        connection = http.client.HTTPConnection('127.0.0.1', whip_port, timeout=12)
        try:
            body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
            entry = {'method': self.command, 'path': self.path, 'seconds': time.monotonic()-started}
            if self.command == 'POST':
                (report/f'offer-{len(results["http"])}.sdp').write_bytes(body)
                body, entry['offerCandidates'] = rewrite_candidates(body, relay.back.getsockname()[1])
            headers = {k:v for k,v in self.headers.items() if k.lower() not in ('host', 'connection', 'content-length')}
            headers['Content-Length'] = str(len(body))
            connection.request(self.command, self.path, body=body, headers=headers)
            response = connection.getresponse(); data = response.read(); entry['status'] = response.status
            if response.status == 201:
                (report/f'answer-{len(results["http"])}.sdp').write_bytes(data)
                data, entry['answerCandidates'] = rewrite_candidates(data, relay.front.getsockname()[1])
            results['http'].append(entry); self.send_response(response.status)
            for name,value in response.getheaders():
                if name.lower() in ('content-length', 'connection', 'transfer-encoding'): continue
                if name.lower() == 'location':
                    location = urllib.parse.urlsplit(value)
                    value = f'http://127.0.0.1:{self.server.server_port}{location.path}' + ('?'+location.query if location.query else '')
                self.send_header(name, value)
            self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
        except Exception as error:
            results['http'].append({'fixtureError': str(error)})
            self.send_error(502)
        finally: connection.close()
    do_OPTIONS = do_POST = do_DELETE = do_PATCH = forward
    def log_message(self, *_): pass

def sample():
    began = time.perf_counter(); s = state(); latency = (time.perf_counter()-began)*1000
    assert s['recording']['program_active'], s['recording']
    pids = {p.pid for p in root.children()}; assert initial_workers.issubset(pids), 'Independent recorder/source worker replaced'
    private = 0; handles = 0; cpu_delta = {}
    for process in [root, *root.children(recursive=True)]:
        try:
            args = process.cmdline(); role = 'main' if process.pid == root.pid else process.name()
            if '--studio-worker' in args: role = args[args.index('--studio-worker')+1]
            cpu = process.cpu_times(); total = cpu.user+cpu.system; key = (process.pid, process.create_time())
            if key in cpu_previous: cpu_delta[role] = cpu_delta.get(role, 0)+max(0, total-cpu_previous[key])
            cpu_previous[key] = total
            private += process.memory_info().private/2**20; handles += process.num_handles()
        except psutil.NoSuchProcess: pass  # Publisher replacement can race sampling.
    results['samples'].append({'seconds': time.monotonic()-started, 'apiMs': latency, 'privateMiB': private,
                              'handles': handles, 'cpuDeltaByRole': cpu_delta,
                              'recordingDrops': s['recording']['dropped_frames'], 'output': s['output']})
    return s

def save(): (report/'udp-fault.json').write_text(json.dumps(results, indent=2), encoding='utf-8')

def dwell(seconds):
    end = time.monotonic()+seconds
    while time.monotonic() < end: sample(); time.sleep(.5)
    save()

def receive(protocol, label, seconds=14):
    path = report/f'{protocol}-{label}.mkv'; began = time.monotonic(); deadline = began+60
    attempts = 0
    while time.monotonic() < deadline:
        attempts += 1; error_path = report/f'{protocol}-{label}-{attempts}.log'
        with error_path.open('wb') as errors:
            process = subprocess.Popen([str(ffmpeg), '-v', 'warning', '-y', '-timeout', '5000000', '-rtsp_transport', 'tcp',
                '-i', f'rtsp://127.0.0.1:{rtsp_port}/{protocol}', '-t', str(seconds), '-map', '0', '-c', 'copy', str(path)],
                stdout=errors, stderr=errors, creationflags=subprocess.CREATE_NO_WINDOW)
            receivers.append(process)
            while process.poll() is None and time.monotonic() < deadline:
                sample(); time.sleep(.5)
            if process.poll() is None: process.kill(); process.wait(timeout=5)
        if process.returncode == 0 and path.exists() and path.stat().st_size > 50000:
            picture = api('/studio/screenshot', {'target':'program', 'max_width':640})
            raw = base64.b64decode(picture['data']); (report/f'{protocol}-{label}-program.png').write_bytes(raw)
            frame = np.asarray(Image.open(io.BytesIO(raw)).convert('L'))
            live = sum(int(frame[8:16, n*20+6:n*20+14].mean()>128)*(1<<n) for n in range(16))
            observation = {'file':path.name, 'captureSeconds':time.monotonic()-began, 'liveFrameAtCaptureEnd':live,
                           'attempts':attempts, 'receiverLog':error_path.name}
            results['protocols'][protocol]['clips'].append(observation); save(); return
        if b'404' not in error_path.read_bytes() and process.returncode != 0:
            raise AssertionError(error_path.read_text(errors='replace')[-2000:])
        time.sleep(.5)
    raise AssertionError('Receiver did not recover: '+protocol+'/'+label)

try:
    assert args.mediamtx
    clip = report/'frame-clock.mp4'
    fixture = os.environ.get('STUDIO_CLOCK_FIXTURE')
    if fixture: shutil.copyfile(fixture, clip)
    else:
        boxes = ['drawbox=x=0:y=0:w=320:h=24:color=black:t=fill']
        boxes += [f"drawbox=x={n*20}:y=0:w=20:h=24:color=white:t=fill:enable='mod(floor(n/{1<<n}),2)'" for n in range(16)]
        ff('-v','error','-n','-f','lavfi','-i','testsrc2=s=640x360:r=30','-f','lavfi','-i',
           r'aevalsrc=if(lt(mod(t\,1)\,0.12)\,0.25*sin(2*PI*(600+20*floor(t))*t)\,0):s=48000',
           '-vf',','.join(boxes),'-t','60','-c:v','libopenh264','-b:v','4000k','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k',clip)
    rtsp_port, srt_port, whip_port, udp_port = [port() for _ in range(4)]
    config = report/'mediamtx.yml'
    config.write_text(f'logLevel: warn\nrtspAddress: 127.0.0.1:{rtsp_port}\nrtspTransports: [tcp]\nrtmp: false\nwebrtcAddress: 127.0.0.1:{whip_port}\nwebrtcLocalUDPAddress: 127.0.0.1:{udp_port}\nwebrtcLocalTCPAddress: ""\nwebrtcIPsFromInterfaces: false\nwebrtcAdditionalHosts: [127.0.0.1]\nwebrtcICEServers2: []\nsrtAddress: 127.0.0.1:{srt_port}\nhls: false\nmoq: false\npaths:\n  all_others:\n')
    log = (report/'mediamtx.log').open('wb'); mtx = subprocess.Popen([args.mediamtx,str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(.8); assert mtx.poll() is None
    target = cli_call('launch','--headless','--show',str(show)); root = psutil.Process(target['pid']); owned.append(root)
    command('monitor.set',enabled=False)
    sid = command('source.add',kind='media',name='Network sync clock',locator=str(clip))['id']
    until(lambda: state()['sources'][0]['frames']>30,'media frames')
    output = report/'independent-recording.mkv'; command('record.start',path=str(output))
    until(lambda: state()['recording']['status'].startswith('Recording:'),'recording')
    initial_workers = {p.pid for p in root.children()}; started = time.monotonic()
    protocols = os.environ.get('STUDIO_UDP_PROTOCOLS','srt,whip').split(',')
    for protocol in protocols:
        assert protocol in ('srt','whip'); relay = UdpRelay(srt_port if protocol=='srt' else udp_port, protocol=='whip')
        details = {'clips':[], 'phases':[]}; results['protocols'][protocol] = details
        if protocol == 'whip':
            signal_server = http.server.ThreadingHTTPServer(('127.0.0.1',0), Signaling)
            threading.Thread(target=signal_server.serve_forever,daemon=True).start()
            url = f'http://127.0.0.1:{signal_server.server_port}/whip/whip'
        else: url = f'srt://127.0.0.1:{relay.front.getsockname()[1]}?streamid=publish:srt'
        command('output.configure',protocol=protocol,url=url,encoder='auto',bitrate_kbps=3500,latency_ms=300)
        command('output.start'); until(lambda: state()['output']['live'], protocol+' live',50)
        details['encoder'] = state()['output']['encoder']; print(protocol+' baseline',flush=True)
        receive(protocol,'baseline')
        profiles = [('moderate',.01,25,8,True),('severe',.10,100,20,False),
                    ('blackout',1,0,20,False),('restored',0,0,15,True)]
        if os.environ.get('STUDIO_UDP_DIAGNOSTIC'):
            profiles = [('loss-only',.01,0,5,True),('restored-between',0,0,5,False),
                        ('jitter-only',0,25,5,True),('restored',0,0,5,True)]
        for name,loss,jitter,length,capture in profiles:
            print(protocol+' '+name,flush=True); entry={'name':name,'loss':loss,'jitterMs':jitter,'startSeconds':time.monotonic()-started}
            details['phases'].append(entry); relay.configure(name,loss,jitter); dwell(length)
            if capture: receive(protocol,name)
            entry['endSeconds'] = time.monotonic()-started; details['relay'] = relay.snapshot(); save()
        if protocol == 'srt' and os.environ.get('STUDIO_UDP_RECOVERY'):
            print('srt settled', flush=True); dwell(30); receive(protocol, 'settled')
            command('output.stop'); until(lambda: not state()['output']['active'] and not state()['output']['stopping'], 'stop for clean restart')
            command('output.start'); until(lambda: state()['output']['live'], 'clean restart live', 50)
            print('srt clean-restart', flush=True); dwell(5); receive(protocol, 'clean-restart')
        command('output.stop'); until(lambda: not state()['output']['active'] and not state()['output']['stopping'],'stop '+protocol)
        details['relay'] = relay.snapshot(); assert not details['relay']['errors'], details['relay']
        assert not any(p.get('queueOverflow', 0) or p.get('sendErrors', 0) for p in details['relay']['profiles'].values()), details['relay']
        if not os.environ.get('STUDIO_UDP_DIAGNOSTIC'):
            assert details['relay']['profiles']['blackout'].get('upSent',0) == 0 and details['relay']['profiles']['blackout'].get('downSent',0) == 0
            assert details['relay']['profiles']['moderate'].get('upDropped',0)>0 and details['relay']['profiles']['moderate'].get('upReordered',0)>0
        relay.close(); relay = None
        if signal_server: signal_server.shutdown(); signal_server.server_close(); signal_server=None
    command('record.stop'); until(lambda: not state()['recording']['busy'],'record saved',45)
    children = root.children(recursive=True); cli_call('quit'); root.wait(timeout=30); target=None
    _, alive = psutil.wait_procs(children,timeout=15); assert not alive
    results['recordingTiming'] = analyze(output)
    for protocol, details in results['protocols'].items():
        for observation in details['clips']:
            try:
                timing = analyze(report/observation['file'])
                observation['timing'] = timing
                observation['lastPictureAgeSeconds'] = ((observation['liveFrameAtCaptureEnd']-timing['lastSourceFrameId'])%1800)/30
                assert observation['lastPictureAgeSeconds'] < 3, f"Last picture is {observation['lastPictureAgeSeconds']:.3f}s behind Program"
            except AssertionError as error:
                observation['qualityFailure'] = str(error)
    results['summary'] = {'p95ApiMs':float(np.percentile([s['apiMs'] for s in results['samples']],95)),
                          'maxApiMs':max(s['apiMs'] for s in results['samples']),
                          'memoryRangeMiB':[min(s['privateMiB'] for s in results['samples']),max(s['privateMiB'] for s in results['samples'])],
                          'recordingDropsStartEnd':[results['samples'][0]['recordingDrops'],results['samples'][-1]['recordingDrops']]}
    elapsed = results['samples'][-1]['seconds']-results['samples'][0]['seconds']
    totals = collections.Counter()
    for sample_data in results['samples'][1:]: totals.update(sample_data['cpuDeltaByRole'])
    results['summary']['averageLogicalCoresByRole'] = {role: value/elapsed for role,value in totals.items()}
    results['summary']['handlesStartEnd'] = [results['samples'][0]['handles'], results['samples'][-1]['handles']]
    results['passed'] = not any('qualityFailure' in clip for details in results['protocols'].values() for clip in details['clips'])
finally:
    if target:
        children = root.children(recursive=True) if root and root.is_running() else []
        try: cli_call('quit'); root.wait(timeout=35)
        except Exception:
            if root and root.is_running(): root.kill(); root.wait(timeout=10)
        _, alive = psutil.wait_procs(children, timeout=10)
        for process in alive:
            try: process.kill()
            except psutil.NoSuchProcess: pass
        psutil.wait_procs(alive, timeout=5)
    for process in receivers:
        if process.poll() is None: process.kill(); process.wait(timeout=5)
    if relay: relay.close()
    if signal_server: signal_server.shutdown(); signal_server.server_close()
    if mtx and mtx.poll() is None: mtx.terminate(); mtx.wait(timeout=10)
    if log: log.close()
    for key,value in saved.items():
        if value is None:
            try: winreg.DeleteValue(settings,key)
            except FileNotFoundError: pass
        else: winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    save()
print(json.dumps(results.get('summary',{}),indent=2),flush=True)
assert results.get('passed'), 'Media quality failed; see udp-fault.json and retained clips'
