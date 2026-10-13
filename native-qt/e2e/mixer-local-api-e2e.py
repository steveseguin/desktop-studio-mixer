"""Windowless CLI/API control of portrait Spout and the registered virtual camera."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    command('canvas.set',format=5)
    clip=report/'portrait.mp4';r=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=360x640:r=30','-t','15','-c:v','libopenh264',clip],timeout=30);assert r.returncode==0,r.stderr
    sid=command('source.add',kind='media',name='Portrait program',locator=str(clip))['id']
    until(lambda:next(s for s in state()['sources'] if s['id']==sid)['frames']>20,'source')
    api('/studio/command',{'command':'local.set','spout':True,'virtualCamera':True,'unregisterCamera':True},400)
    assert not state()['local_outputs']['spout']
    command('local.set',spout=True,virtualCamera=True)
    until(lambda:not state()['local_outputs']['configuring'],'registration')
    assert state()['local_outputs']['virtual_camera'];time.sleep(2)
    r=run([Path('build-mixer/qa/studio_spout_output_probe.exe').resolve()],text=True,timeout=30);assert r.returncode==0,r.stdout+r.stderr
    spout=json.loads(next(s for s in r.stdout.splitlines() if s.startswith('{')));assert spout['width']==720 and spout['height']==1280 and spout['distinctFrames']>20,spout
    output=report/'camera-consumer.mkv'
    r=run([ffmpeg,'-v','error','-y','-f','dshow','-video_size','720x1280','-framerate','30','-i','video=Desktop Studio Virtual Camera','-t','4','-c:v','libopenh264',output],timeout=30);assert r.returncode==0,r.stderr
    r=run([ffmpeg,'-v','error','-i',output,'-vf','scale=36:64','-f','framemd5','-'],timeout=30);assert r.returncode==0 and not r.stderr,r.stderr
    rows=[line for line in r.stdout.decode().splitlines() if not line.startswith('#')];assert len(rows)>90 and len(set(row.split(',')[-1] for row in rows))>40
    assert not visible_windows(root.pid)
    command('local.set',spout=False,virtualCamera=False,unregisterCamera=True);until(lambda:not state()['local_outputs']['configuring'],'unregister')
    results={'passed':True,'spout':spout,'virtualCameraDecodedFrames':len(rows),'headless':True,'conflictingRequestRejectedBeforeChanges':True,'localOutputsState':state()['local_outputs']}
finally:
    if target:
        try:
            until(lambda:not state()['local_outputs']['configuring'],'registration cleanup');command('local.set',spout=False,virtualCamera=False,unregisterCamera=True);until(lambda:not state()['local_outputs']['configuring'],'unregister cleanup')
            cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'local-api.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
