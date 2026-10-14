"""Packaged Studio -> private RTSP/RTMP server -> decoded A/V, with faults.

Uses an isolated show. Nothing is published to the internet. Also exercises the
real output GUI, concurrent recording and the authenticated automation endpoint.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import base64
from pywinauto import Application
from pywinauto.keyboard import send_keys

server = root = None
logs = []
gui = os.environ.get('STUDIO_RTSP_HEADLESS') != '1'
ctypes.windll.kernel32.SetThreadExecutionState(0x80000003)
results = {'cases': [], 'sha256': hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest()}
results['gui'] = gui

def launch(argv, name):
    log = (report/(name+'.log')).open('wb'); logs.append(log)
    child = subprocess.Popen([str(a) for a in argv], stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    owned.append(psutil.Process(child.pid))
    return child

def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0)); return s.getsockname()[1]

def server_api(route):
    with urllib.request.urlopen(f'http://127.0.0.1:{http_port}/v3/'+route, timeout=3) as r: return json.load(r)

def ready_server():
    try: return server_api('paths/list') is not None
    except (OSError, urllib.error.URLError): return False

def capture(name, stream):
    until(lambda:any(p['name']==stream and p['ready'] for p in server_api('paths/list')['items']), 'receiver tracks '+stream, 30)
    path = report/(name+'.mkv')
    child = launch([ffmpeg, '-hide_banner', '-rtsp_transport', 'tcp', '-i', f'rtsp://127.0.0.1:{rtsp_port}/{stream}',
                    '-t', '8', '-map', '0', '-c', 'copy', path], name)
    child.wait(timeout=30); assert child.returncode == 0, name
    decoded = decode(path)
    stats = run([ffmpeg, '-v', 'info', '-i', path, '-an', '-vf', 'showinfo', '-fps_mode', 'passthrough', '-f', 'null', '-'], timeout=30)
    import re
    times = [float(x) for x in re.findall(rb'pts_time:([-\d.]+)', stats.stderr)]
    assert len(times)>180 and min(np.diff(times))>0, times[:10]
    decoded['fps']=(len(times)-1)/(times[-1]-times[0])
    decoded['maxFrameGapMs']=float(max(np.diff(times))*1000)
    # Joining an already-running RTSP stream can skip initial packets before
    # the reader acquires its first complete GOP. Report that separately from
    # sustained cadence instead of hiding it in an average frame rate.
    steady=[t for t in times if t>times[0]+1]
    decoded['steadyMaxFrameGapMs']=float(max(np.diff(steady))*1000)
    path.with_suffix('.decoded.json').write_text(json.dumps(decoded,indent=2),encoding='utf-8')
    assert 29 < decoded['fps'] < 31 and decoded['maxFrameGapMs'] < 250 and decoded['steadyMaxFrameGapMs'] < 100
    return decoded

def ui(name):
    return next(c for c in window.descendants() if (c.element_info.automation_id or '').endswith('.'+name))

def screenshot(name):
    data=api('/studio/screenshot', {'target':'window' if gui else 'program', 'max_width':1600})
    (report/name).write_bytes(base64.b64decode(data['data']))

try:
    rtsp_port, rtmp_port, http_port = [free_port() for _ in range(3)]
    for port in range(18200, 19000, 2):
        try:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as a, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as b:
                a.bind(('127.0.0.1',port));b.bind(('127.0.0.1',port+1))
                rtp_port,rtcp_port=port,port+1;break
        except OSError:continue
    else:raise AssertionError('No private RTP/RTCP port pair available')
    config=report/'mediamtx.yml'
    config.write_text(f'''logLevel: info
api: true
apiAddress: 127.0.0.1:{http_port}
rtspAddress: 127.0.0.1:{rtsp_port}
rtspTransports: [tcp, udp]
rtpAddress: 127.0.0.1:{rtp_port}
rtcpAddress: 127.0.0.1:{rtcp_port}
rtmpAddress: 127.0.0.1:{rtmp_port}
webrtc: false
srt: false
hls: false
moq: false
authInternalUsers:
  - user: any
    permissions:
      - action: api
      - action: read
      - action: publish
        path: '~^(rtsp|live)'
  - user: publisher
    pass: rtsp-private-fixture
    permissions:
      - action: publish
        path: auth
paths:
  all_others:
''', encoding='utf-8')
    mtx=Path(args.mediamtx or 'C:/Users/Steve/code/game-capture/native-qt/.cache/mediamtx-1.21.2/mediamtx.exe')
    server=launch([mtx, config], 'server'); until(ready_server, 'private server')
    target=cli_call('launch', '--gui' if gui else '--headless', '--show', str(show));root=psutil.Process(target['pid']);owned.append(root)
    if gui:
        window=Application(backend='uia').connect(process=root.pid).window(title='Desktop Studio - Powered by VDO.Ninja')
        window.wait('visible', timeout=15);window.maximize()
    command('monitor.set', enabled=False)
    fixture=report/'source.mp4'
    made=run([ffmpeg,'-v','error','-f','lavfi','-i','testsrc2=s=640x360:r=30','-f','lavfi','-i',
              'sine=frequency=731:sample_rate=48000','-t','4','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',fixture],timeout=30)
    assert made.returncode==0,made.stderr
    source=command('source.add',kind='media',name='RTSP moving picture and tone',locator=str(fixture))['id']
    until(lambda:state()['sources'][0]['frames']>30,'source playback')
    command('layer.set',source=source,x=0,y=0,w=1,h=1)
    recording=report/'concurrent-recording.mkv'
    command('record.start',path=str(recording));until(lambda:state()['recording']['status'].startswith('Recording:'),'recording')
    # Select and type the first destination through the actual interface.
    if gui:
        window.child_window(title='Studio',control_type='TabItem').click_input();ui('mixerOutput').click_input();time.sleep(.4)
        ui('outputSelect').click_input();send_keys('{HOME}{DOWN 5}{ENTER}',pause=.15)
        assert ui('outputSelect').selected_text()=='RTSP / VRChat (TCP)'
        ui('outputUrlInput').set_edit_text(f'rtspt://127.0.0.1:{rtsp_port}/rtsp-gui')
        assert not any(c.is_visible() for c in window.descendants() if (c.element_info.automation_id or '').endswith('.outputSecretInput'))
    else:
        command('output.configure',protocol='rtsp',url=f'rtspt://127.0.0.1:{rtsp_port}/rtsp-gui',encoder='auto',bitrate_kbps=3500)
    screenshot('rtsp-settings.png')
    if gui:
        window.child_window(title='Studio',control_type='TabItem').click_input();ui('studioStartOutput').click_input()
    else:command('output.start')
    until(lambda:state()['output']['live'],'GUI RTSP publishing',45)
    row={'protocol':'RTSP / rtspt URL','media':capture('gui-tcp','rtsp-gui')};results['cases'].append(row)
    sessions=server_api('rtspsessions/list'); row['sessions']=sessions
    assert any(s.get('state')=='publish' and s.get('transport')=='TCP' for s in sessions['items']),sessions
    screenshot('rtsp-live.png')
    # Kill only the owned muxer. Composition and the independent recording must survive.
    diag=api('/diagnostics');mux_pid=diag['output']['muxer_pid']
    assert mux_pid in [p.pid for p in root.children(recursive=True)]
    psutil.Process(mux_pid).kill()
    until(lambda:api('/diagnostics')['output']['reconnects']>0 and state()['output']['live'],'muxer recovery',45)
    row['afterMuxerCrash']=capture('muxer-recovered','rtsp-gui')
    assert state()['recording']['active'] and root.is_running()
    # Restart the receiving server; the same GUI session must reconnect.
    server.terminate();server.wait(timeout=10);time.sleep(3)
    start=time.monotonic();server=launch([mtx,config],'server-restarted');until(ready_server,'server restart')
    until(lambda:any(p['name']=='rtsp-gui' and p['ready'] for p in server_api('paths/list')['items']),'RTSP reconnect',45)
    until(lambda:state()['output']['live'],'publishing restored')
    row['serverRecoverySeconds']=time.monotonic()-start
    row['afterServerRestart']=capture('server-recovered','rtsp-gui')
    if gui:ui('studioStartOutput').invoke()
    else:command('output.stop')
    until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'stop')
    for protocol,stream,url in [
        ('rtsp-udp','rtsp-udp',f'rtsp://127.0.0.1:{rtsp_port}/rtsp-udp'),
        ('rtspt','rtsp-alias',f'rtsp://127.0.0.1:{rtsp_port}/rtsp-alias'),
        ('rtmp','live/rtmp',f'rtmp://127.0.0.1:{rtmp_port}/live/rtmp'),
        ('rtsp','auth',f'rtsp://publisher:rtsp-private-fixture@127.0.0.1:{rtsp_port}/auth')]:
        if stream=='auth':
            command('output.configure',protocol='rtsp',url=url.replace('rtsp-private-fixture','wrong-private-fixture'))
            command('output.start')
            until(lambda:api('/diagnostics').get('output',{}).get('reconnects',0)>0,'authentication rejected',30)
            assert not any(p['name']=='auth' and p['ready'] for p in server_api('paths/list')['items'])
            assert 'wrong-private-fixture' not in json.dumps(api('/diagnostics'))+json.dumps(state())
            assert state()['recording']['active']
            command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'rejected output stopped')
            results['rejectedAuthenticationIsolated']=True
        command('output.configure',protocol=protocol,url=url,encoder='auto',bitrate_kbps=3500)
        command('output.start');until(lambda:state()['output']['live'],protocol+' live',45)
        row={'protocol':protocol,'media':capture(stream.replace('/','-'),stream)};results['cases'].append(row)
        row['sessions']=server_api('rtspsessions/list')
        if protocol.startswith('rtsp'):
            expected='UDP' if protocol=='rtsp-udp' else 'TCP'
            assert any(s.get('state')=='publish' and s.get('transport')==expected for s in row['sessions']['items']),row['sessions']
        assert 'rtsp-private-fixture' not in json.dumps(api('/diagnostics'))+json.dumps(state())
        command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],protocol+' stop')
        assert state()['recording']['active']
    for fields in [dict(protocol='rtsp',url='https://example.invalid/live'),dict(protocol='rtsp',url='rtsp://host/'),
                   dict(protocol='rtsp-udp',url='rtspt://host/live'),dict(protocol='rtsp',url='rtsp://host/live',credential='unsupported')]:
        api('/studio/command',{'command':'output.configure',**fields},expected=400)
    command('record.stop');until(lambda:not state()['recording']['busy'],'recording finalized')
    results['recording']=decode(recording)
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(children,timeout=10);assert not alive
    results['passed']=True
finally:
    ctypes.windll.kernel32.SetThreadExecutionState(0x80000000)
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    for proc in reversed(owned):
        try:
            for child in proc.children(recursive=True):child.kill()
            if proc.is_running():proc.kill();proc.wait(timeout=10)
        except psutil.NoSuchProcess:pass
    for log in logs:log.close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'rtsp-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
assert results.get('passed')
