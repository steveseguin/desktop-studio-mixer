"""Decode a real recording after repeated short scheduling stalls in Studio."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    clip=report/'tone.wav';made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','20',clip],timeout=30);assert made.returncode==0
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    command('source.add',kind='media',name='Clock tone',locator=str(clip))
    recording=report/'stalled-program.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['active'],'record');time.sleep(2)
    stalls=int(os.environ.get('STUDIO_CLOCK_STALLS','45'))
    for n in range(stalls):
        root.suspend()
        try:time.sleep(.065)
        finally:root.resume()
        time.sleep(.135)
        assert state()['recording']['active']
    time.sleep(2);command('record.stop');until(lambda:not state()['recording']['busy'],'finalize')
    decoded=run([ffmpeg,'-v','error','-i',recording,'-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=60)
    results['decode']={'exitCode':decoded.returncode,'errors':decoded.stderr.decode(errors='replace')[-5000:]}
    results['schedulingStalls']=stalls;results['stallMilliseconds']=65
    assert decoded.returncode==0 and not decoded.stderr,results['decode']
    results['recording']=decode(recording);results['passed']=True
finally:
    if target:
        try:root.resume();cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'audio-clock.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
