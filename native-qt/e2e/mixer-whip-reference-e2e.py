"""Packaged WHIP publisher against an independent private aiortc receiver."""
from pathlib import Path
import sys
exec(Path(__file__).with_name('mixer-udp-fault-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    assert args.mediamtx')[0])
os.environ['STUDIO_WHIP_REFERENCE'] = '1'
try:
    whip_port = port()
    log = (report/'receiver.log').open('wb')
    mtx = subprocess.Popen([sys.executable,
        str(Path(__file__).with_name('whip-reference-receiver.py')), '--port', str(whip_port),
        '--report', str(report/'receiver')], stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(2); assert mtx.poll() is None
    relay = UdpRelay(1, True)
    signal_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Signaling)
    threading.Thread(target=signal_server.serve_forever, daemon=True).start()
    target = cli_call('launch','--headless','--show',str(show)); root = psutil.Process(target['pid']); owned.append(root)
    command('monitor.set', enabled=False)
    command('source.add', kind='media', name='Independent receiver clock', locator=os.environ['STUDIO_CLOCK_FIXTURE'])
    until(lambda: state()['sources'][0]['frames']>30, 'clock')
    output = report/'independent-recording.mkv'; command('record.start', path=str(output))
    until(lambda: state()['recording']['program_active'], 'recording')
    initial_workers = {p.pid for p in root.children()}; started = time.monotonic()
    command('output.configure', protocol='whip', url=f'http://127.0.0.1:{signal_server.server_port}/whip/whip', encoder='auto', bitrate_kbps=3500)
    command('output.start'); until(lambda: state()['output']['live'], 'WHIP live')
    phases=[]
    for name,loss,jitter,seconds in [('baseline',0,0,12),('loss-only',.01,0,25),('moderate',.01,25,25),('restored',0,0,12)]:
        print(name, flush=True); relay.configure(name,loss,jitter)
        start=time.monotonic(); dwell(seconds)
        phases.append({'name':name,'start':start,'end':time.monotonic()})
    command('output.stop'); until(lambda: not state()['output']['active'] and not state()['output']['stopping'], 'stop')
    with urllib.request.urlopen(f'http://127.0.0.1:{whip_port}/status') as response: received=json.load(response)
    results['receiver']=received; results['phases']=phases; results['relay']=relay.snapshot()
    raw=report/'receiver'/(received['id']+'.h264')
    decoded=run([ffmpeg,'-v','error','-i',raw,'-f','null','-'], timeout=40)
    results['rawDecode']={'returncode':decoded.returncode,'stderr':decoded.stderr.decode(errors='replace')}
    for phase in phases:
        frames=[f for f in received['frames'] if phase['start']<=f['wall']<phase['end']]
        phase['frames']=len(frames)
        phase['maxPtsGapMs']=max((b['pts']-a['pts'])*1000 for a,b in zip(frames,frames[1:]))
        phase['fps']=len(frames)/(phase['end']-phase['start'])
        phase['nonIncreasingPts']=sum(b['pts']<=a['pts'] for a,b in zip(frames,frames[1:]))
    command('record.stop'); until(lambda:not state()['recording']['program_active'], 'record stop')
    time.sleep(1); results['recordingTiming']=analyze(output)
    results['passed']=decoded.returncode==0 and not decoded.stderr and all(p['fps']>28 and not p['nonIncreasingPts'] and p['maxPtsGapMs']<250 for p in phases)
finally:
    if target:
        children=root.children(recursive=True)
        try: cli_call('quit'); root.wait(timeout=35)
        except Exception:
            if root.is_running(): root.kill(); root.wait(timeout=10)
        _,alive=psutil.wait_procs(children,timeout=10)
        for process in alive:
            try: process.kill()
            except psutil.NoSuchProcess: pass
    if relay: relay.close()
    if signal_server: signal_server.shutdown(); signal_server.server_close()
    if mtx and mtx.poll() is None: mtx.terminate(); mtx.wait(timeout=10)
    if log: log.close()
    winreg.CloseKey(settings)
    assert hashlib.sha256(user_show.read_bytes()).hexdigest()==original_hash
    save()
print(json.dumps(results.get('phases'),indent=2),flush=True)
assert results.get('passed'), 'Reference receiver media quality failed'
