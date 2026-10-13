"""Real encrypted SRT caller/listener media and cancellation during recording."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    password=uuid.uuid4().hex[:24]
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    clip=report/'tone.wav';made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','8',clip],timeout=30);assert made.returncode==0
    command('source.add',kind='media',name='Listener audio',locator=str(clip))
    recording=report/'independent.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['active'],'record')
    results['modes']={}
    def receiver(name,mode):
        output=report/(name+'.mkv');log=(report/(name+'.log')).open('wb')
        child=subprocess.Popen([str(ffmpeg),'-v','error','-y','-i',f'srt://127.0.0.1:{port}?mode={mode}&passphrase={password}&pbkeylen=16&connect_timeout=15000','-t','6','-map','0','-c','copy',str(output)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        owned.append(psutil.Process(child.pid));return child,log,output
    for mode in ('caller','listener'):
        if mode=='caller':child,log,output=receiver('caller-received','listener');time.sleep(.5)
        command('output.configure',protocol='srt',url=f'srt://127.0.0.1:{port}?mode={mode}',credential=password,encoder='nvenc',bitrate_kbps=2500)
        command('output.start')
        if mode=='listener':
            # A receiver can arrive after startup watchdog/retry intervals.
            time.sleep(18);assert state()['recording']['active'];child,log,output=receiver('listener-late-received','caller')
        child.wait(timeout=55);log.close();assert child.returncode==0,(report/(output.stem+'.log')).read_text()
        results['modes'][mode]=decode(output)
        if mode=='listener':
            time.sleep(3);child,log,output=receiver('listener-rejoined','caller');child.wait(timeout=55);log.close();assert child.returncode==0
            results['listenerReceiverRejoined']=decode(output)
        command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'stop')
    result=api('/studio/command',{'command':'output.configure','protocol':'srt','url':f'srt://127.0.0.1:{port}?mode=rendezvous','encoder':'nvenc'},400)
    assert 'rendezvous' in result['error'];results['rendezvousUnavailable']=True
    command('output.configure',protocol='srt',url=f'srt://127.0.0.1:{port}?mode=caller&latency=200000&passphrase={password}&pbkeylen=16',encoder='nvenc',bitrate_kbps=2500)
    command('output.start');time.sleep(3)
    assert state()['recording']['active'];start=time.monotonic();command('output.stop')
    until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'cancel unconnected caller')
    results['cancelCallerWithoutReceiverSeconds']=round(time.monotonic()-start,3)
    assert state()['recording']['active'];time.sleep(3)
    command('record.stop');until(lambda:not state()['recording']['busy'],'finalize')
    results['independentRecording']=decode(recording);results['errorsAndCancellationHandled']=True
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    for child in owned[1:]:
        try:
            if child.is_running():child.terminate();child.wait(timeout=10)
        except psutil.Error:pass
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'srt-modes.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
