"""SRT unsupported-mode errors and cancellation while recording continues."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    password=uuid.uuid4().hex[:24]
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    clip=report/'tone.wav';made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','8',clip],timeout=30);assert made.returncode==0
    command('source.add',kind='media',name='Listener audio',locator=str(clip))
    recording=report/'independent.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['active'],'record')
    results['unsupportedModes']={}
    for mode in ('listener','rendezvous'):
        result=api('/studio/command',{'command':'output.configure','protocol':'srt','url':f'srt://127.0.0.1:{port}?mode={mode}','encoder':'nvenc'},400)
        assert 'Only SRT caller mode' in result['error'],result
        assert state()['recording']['active'];results['unsupportedModes'][mode]=result['error']
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
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'srt-modes.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
