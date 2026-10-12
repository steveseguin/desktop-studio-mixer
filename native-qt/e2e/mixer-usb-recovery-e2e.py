"""Keep recording/publishing through a manual Logitech USB unplug/reconnect.

Wait for READY in the log before unplugging. Private media remains in --report.
Requires --mediamtx. Completes only after camera frames and microphone samples
return while both outputs are still active; timeout is not a successful result.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-endurance-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])

results={'samples':[]};receiver_capture=None
def source(data,sid):return next(s for s in data['sources'] if s['id']==sid)
def save_usb():
    (report/'usb-recovery.json').write_text(json.dumps(results,indent=2),encoding='utf-8')

try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    (report/'session.json').write_text(json.dumps(target),encoding='utf-8')
    command('canvas.set',format=0,renderer='cpu');command('workspace.set',mode='single')
    camera=next(c for c in api('/sources/cameras')['sources'] if 'C920' in c['name'])
    micro=next(c for c in api('/sources/audio-inputs')['sources'] if 'C920' in c['name'])
    cam=command('source.add',kind='camera',name='USB recovery camera',locator=camera['id'])['id']
    mic=command('source.add',kind='microphone',name='USB recovery microphone',locator=micro['id'])['id']
    command('audio.set',source=mic,monitored=False)
    fixture=report/'clock.mp4'
    made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=30',
              '-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','4',
              '-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',fixture],timeout=30)
    assert made.returncode==0,made.stderr
    clock=command('source.add',kind='media',name='Continuity clock',locator=str(fixture))['id']
    command('audio.set',source=clock,monitored=False)
    command('layer.set',source=clock,x=.75,y=.75,w=.25,h=.25)
    wait_for(lambda:source(state(),cam)['frames']>30,'Camera did not start')
    wait_for(lambda:source(state(),mic)['peak']>1e-5,'Microphone did not start')
    rtsp,rtmp,whip,udp,srt=[port() for _ in range(5)]
    (report/'receiver.yml').write_text(f'''logLevel: warn
rtspAddress: 127.0.0.1:{rtsp}
rtspTransports: [tcp]
rtmpAddress: 127.0.0.1:{rtmp}
webrtc: false
srt: false
hls: false
moq: false
paths:
  all_others:
''')
    mtx=start_receiver_server();time.sleep(1)
    recording=report/'usb-program.mkv';command('record.start',path=str(recording))
    wait_for(lambda:state()['recording']['status'].startswith('Recording:'),'Recording did not start')
    command('output.configure',protocol='rtmp',url=f'rtmp://127.0.0.1:{rtmp}/endurance',encoder='nvenc',bitrate_kbps=5000)
    command('output.start');wait_for(lambda:state()['output']['live'],'Publishing did not start')
    received=report/'usb-received.mkv';rxlog=(report/'received.log').open('wb');handles.append(rxlog)
    for attempt in range(10):
        receiver_capture=subprocess.Popen([str(ffmpeg),'-v','error','-y','-rtsp_transport','tcp',
            '-i',f'rtsp://127.0.0.1:{rtsp}/endurance','-map','0','-c','copy',str(received)],
            stdin=subprocess.PIPE,stdout=rxlog,stderr=rxlog,creationflags=subprocess.CREATE_NO_WINDOW)
        time.sleep(3)
        if receiver_capture.poll() is None:break
        receiver_capture.communicate()
    assert receiver_capture.poll() is None,'Receiver never opened the published stream'
    results['initialAudioWorkerPids']=[p.pid for p in workers('audio')]
    results['startedUtc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
    started=time.monotonic();absent=False;returned=None;stable=None;previous=source(state(),cam)['frames']
    last_scan=0;present=True;mic_present=True
    print('READY: unplug Logitech for about ten seconds, then reconnect.',flush=True)
    while time.monotonic()-started<1800:
        elapsed=time.monotonic()-started;data=state();camera_state=source(data,cam);mic_state=source(data,mic)
        assert data['recording']['active'] and data['output']['live'],'An output stopped during USB recovery'
        assert receiver_capture.poll() is None,'Receiver stopped during USB recovery'
        if elapsed-last_scan>=2 and (absent or camera_state['starting'] or camera_state['error'] or camera_state['frames']<=previous):
            last_scan=elapsed
            present=any(c['id']==camera['id'] for c in api('/sources/cameras')['sources'])
            mic_present=any(c['id']==micro['id'] for c in api('/sources/audio-inputs')['sources'])
            if not present and not mic_present and not absent:
                absent=True;results['absentAtSeconds']=elapsed;print('USB absence observed; both outputs continue.',flush=True)
            if absent and present and mic_present and returned is None:
                returned=elapsed;results['returnedAtSeconds']=elapsed;print('USB devices returned; checking media.',flush=True)
        sample={'seconds':round(elapsed,3),'cameraPresent':present,'microphonePresent':mic_present,
                'cameraFrames':camera_state['frames'],'cameraStarting':camera_state['starting'],
                'cameraError':camera_state['error'],'microphonePeak':mic_state['peak'],
                'microphoneStarting':mic_state['starting'],'microphoneError':mic_state['error'],
                'recording':data['recording'],'output':data['output']}
        results['samples'].append(sample);save_usb()
        if returned is not None and camera_state['frames']>previous and mic_state['peak']>1e-5:
            if stable is None:stable=elapsed
            if elapsed-stable>=5:break
        else:stable=None
        previous=camera_state['frames'];time.sleep(.3)
    assert stable is not None and returned is not None,'Physical recovery was not verified before timeout'
    results['cameraAndMicrophoneRecoveredSecondsAfterReturn']=stable-returned
    results['finalAudioWorkerPids']=[p.pid for p in workers('audio')]
    assert set(results['initialAudioWorkerPids']).isdisjoint(results['finalAudioWorkerPids'])
    results['outputsActiveThroughout']=True
    cli_call('screenshot','program','--out',str(report/'reconnected.png'))
    receiver_capture.communicate(b'q\n',timeout=15);assert receiver_capture.returncode==0
    command('output.stop');wait_for(lambda:not state()['output']['active'] and not state()['output']['stopping'],'Output did not stop')
    command('record.stop');wait_for(lambda:not state()['recording']['busy'],'Recording did not finalize')
    results['media']=[decode(recording),decode(received)];results['passed']=True
finally:
    if receiver_capture and receiver_capture.poll() is None:
        try:receiver_capture.communicate(b'q\n',timeout=10)
        except subprocess.TimeoutExpired:receiver_capture.terminate();receiver_capture.wait(timeout=10)
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    if mtx and mtx.poll() is None:mtx.terminate();mtx.wait(timeout=15)
    for h in handles:h.close()
    winreg.CloseKey(settings)
    results['originalShowUnchanged']=(hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    save_usb()
print(json.dumps({k:v for k,v in results.items() if k!='samples'},indent=2),flush=True)
