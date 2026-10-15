"""Publish to a private Chromium WHIP receiver, with negotiated RED on/off.

STUDIO_RED_ON_ONLY / STUDIO_RED_OFF_ONLY narrow the case matrix.
STUDIO_AUDIO_BITRATE exercises persisted Opus settings with a private active
project; STUDIO_WHIP_PLAYOUT_MS overrides the default playback hint for diagnosis.
STUDIO_BROWSER_COMBINED=1 uses one video element for synchronized A/V playback;
otherwise the fixture exercises separate audio/video elements.
STUDIO_BROWSER_IMPAIRED_SECONDS extends each impaired phase (default 20).
STUDIO_WHIP_HISTORY_MS holds repair history constant across the RED comparison.
STUDIO_BROWSER_SEED sets the repeatable relay RNG seed for each case.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-udp-fault-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    assert args.mediamtx')[0])
import sys
os.environ['STUDIO_WHIP_REFERENCE']='browser'
browser_children=[]
private_preferences=[]
try:
    if os.environ.get('STUDIO_AUDIO_BITRATE'):
        for key,name,value,kind in (
            ('mixer','activeProject',str(show),winreg.REG_SZ),
            ('audio','bitrateKbps',int(os.environ['STUDIO_AUDIO_BITRATE']),winreg.REG_DWORD)):
            handle=winreg.CreateKey(winreg.HKEY_CURRENT_USER,'Software\\VDO.Ninja\\Video Mixer\\'+key)
            try:previous=winreg.QueryValueEx(handle,name)
            except FileNotFoundError:previous=None
            private_preferences.append((handle,name,previous))
            winreg.SetValueEx(handle,name,0,kind,value)
    whip_port=port(); log=(report/'receiver.log').open('wb')
    mtx=subprocess.Popen([sys.executable,'-B',str(Path(__file__).with_name('whip-browser-receiver.py')),
        '--port',str(whip_port),'--report',str(report/'receiver')],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(4); assert mtx.poll() is None
    relay=UdpRelay(1,True,socket.gethostbyname(socket.gethostname())); signal_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Signaling)
    threading.Thread(target=signal_server.serve_forever,daemon=True).start()
    target=cli_call('launch','--headless',*(() if private_preferences else ('--show',str(show))))
    root=psutil.Process(target['pid']); owned.append(root)
    command('monitor.set',enabled=False)
    command('source.add',kind='media',name='Browser receiver clock',locator=os.environ['STUDIO_CLOCK_FIXTURE'])
    until(lambda:state()['sources'][0]['frames']>30,'clock')
    output=report/'independent-recording.mkv'; command('record.start',path=str(output))
    until(lambda:state()['recording']['program_active'],'recording')
    initial_workers={p.pid for p in root.children()}; started=time.monotonic()
    results['cases']=[]
    for red in ([False] if os.environ.get('STUDIO_RED_OFF_ONLY') else [True] if os.environ.get('STUDIO_RED_ON_ONLY') else [True,False]):
        relay.configure('clean')
        if os.environ.get('STUDIO_BROWSER_SEED'):
            with relay.lock: relay.random.seed(int(os.environ['STUDIO_BROWSER_SEED']))
        history_ms=int(os.environ.get('STUDIO_WHIP_HISTORY_MS',str(1500 if red else 1000)))
        impaired_seconds=int(os.environ.get('STUDIO_BROWSER_IMPAIRED_SECONDS','20'))
        assert 1 <= impaired_seconds <= 600
        command('output.configure',protocol='whip',url=f'http://127.0.0.1:{signal_server.server_port}/whip/whip',encoder='auto',bitrate_kbps=3500,whip_audio_red=red,whip_history_ms=history_ms,whip_playout_ms=int(os.environ.get('STUDIO_WHIP_PLAYOUT_MS','500')))
        command('output.start'); until(lambda:state()['output']['live'],'browser live',45); dwell(3)
        selected=state()['output']; assert selected['audio_red']==red,selected
        assert selected['video_rtx'] and selected['video_nack'] and selected['video_pli'],selected
        phases=[]
        for label,loss,jitter,seconds in [('baseline',0,0,10),('moderate',.01,25,impaired_seconds),('restored',0,0,10)]:
            print(('RED on ' if red else 'RED off ')+label,flush=True); relay.configure(label,loss,jitter)
            began=time.time()*1000; dwell(seconds); phases.append({'name':label,'start':began,'end':time.time()*1000})
        before_stop=state()['output']; command('output.stop')
        until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'stop')
        with urllib.request.urlopen(f'http://127.0.0.1:{whip_port}/status') as response: received=json.load(response)
        for phase in phases:
            frames=[f for f in received['frames'] if phase['start']<=f['wall']<phase['end']]
            levels=[f['rms'] for f in received['audio'] if phase['start']<=f['wall']<phase['end']]
            phase.update(frames=len(frames),fps=len(frames)*1000/(phase['end']-phase['start']),
                maxPtsGapMs=max(((b['pts']-a['pts'])*1000 for a,b in zip(frames,frames[1:])),default=0),
                nonIncreasingPts=sum(b['pts']<=a['pts'] for a,b in zip(frames,frames[1:])),
                audibleWindows=sum(x>.02 for x in levels))
            if frames and 'presentedFrames' in frames[0]:
                phase['compositorPresentedFrames']=frames[-1]['presentedFrames']-frames[0]['presentedFrames']+1
        case={'red':red,'id':received['id'],'phases':phases,'output':before_stop,'stats':received['stats'],'contextState':received.get('contextState'),'playError':received.get('playError'),'audioTime':received.get('audioTime'),'playbackQuality':received.get('playbackQuality')}
        case['combinedPlayback']=received['combinedPlayback']
        case.update(historyMs=history_ms,relaySeed=os.environ.get('STUDIO_BROWSER_SEED'),
                    statsTimeline=received.get('statsTimeline',[]),playbackEvents=received.get('playbackEvents',[]))
        case['passed']=before_stop['retransmitted_packets']>0 and all(p['fps']>28 and not p['nonIncreasingPts'] and p['maxPtsGapMs']<250 and p['audibleWindows']>15 for p in phases)
        results['cases'].append(case); save()
    command('record.stop'); until(lambda:not state()['recording']['program_active'],'record stop')
    time.sleep(1); results['recordingTiming']=analyze(output);results['relay']=relay.snapshot()
    results['passed']=all(c['passed'] for c in results['cases'])
finally:
    if target:
        children=root.children(recursive=True)
        try:cli_call('quit');root.wait(timeout=35)
        except Exception:
            if root.is_running():root.kill();root.wait(timeout=10)
        _,alive=psutil.wait_procs(children,timeout=10)
        for process in alive:
            try:process.kill()
            except psutil.NoSuchProcess:pass
    if relay:results['relay']=relay.snapshot();relay.close()
    if signal_server:signal_server.shutdown();signal_server.server_close()
    if mtx and mtx.poll() is None:
        browser_children=psutil.Process(mtx.pid).children(recursive=True);mtx.terminate();mtx.wait(timeout=10)
    for process in browser_children:
        try:process.kill()
        except psutil.NoSuchProcess:pass
    psutil.wait_procs(browser_children,timeout=5)
    if log:log.close()
    for handle,name,previous in private_preferences:
        if previous is None:
            try:winreg.DeleteValue(handle,name)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(handle,name,0,previous[1],previous[0])
        winreg.CloseKey(handle)
    winreg.CloseKey(settings)
    assert hashlib.sha256(user_show.read_bytes()).hexdigest()==original_hash
    save()
assert results.get('passed'),'Browser recovery workflow failed'
