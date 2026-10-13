"""Actual packaged media seeking and portable/active project workflows."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    source_dir=report/'original assets';source_dir.mkdir()
    clip=source_dir/'colour cue.mp4'
    made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','color=red:s=640x360:r=30:d=4[r];color=lime:s=640x360:r=30:d=4[g];color=blue:s=640x360:r=30:d=4[b];[r][g][b]concat=n=3:v=1:a=0',
              '-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','12','-c:v','libopenh264','-c:a','aac',clip],timeout=40);assert made.returncode==0,made.stderr
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    sid=command('source.add',kind='media',name='Cue clip',locator=str(clip))['id']
    def track():return next(s for s in state()['sources'] if s['id']==sid)
    until(lambda:track()['durationMs']>11000,'media duration')
    command('media.set',id=sid,paused=True,loop=False,seekMs=6000)
    until(lambda:6000<=track()['positionMs']<6500,'paused cue')
    time.sleep(.6);assert track()['paused'] and track()['positionMs']<6500
    pixel=image('program','paused-green.png').getpixel((320,180));assert pixel[1]>220 and pixel[0]<25 and pixel[2]<25,pixel
    command('media.set',id=sid,seekMs=track()['durationMs']+1000)
    until(lambda:track()['positionMs']>=11000,'cue clamped at clip end')
    pixel=image('program','paused-end.png').getpixel((320,180));assert pixel[2]>220 and pixel[0]<25,pixel
    command('media.set',id=sid,seekMs=9500);until(lambda:track()['positionMs']>=9500,'second paused cue')
    pixel=image('program','paused-blue.png').getpixel((320,180));assert pixel[2]>220 and pixel[0]<25,pixel
    recording=report/'seek-resume.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['active'],'recording')
    time.sleep(2);command('media.set',id=sid,seekMs=1000,paused=False);time.sleep(4)
    command('record.stop');until(lambda:not state()['recording']['busy'],'recording stopped')
    full=run([ffmpeg,'-v','error','-i',recording,'-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=40);assert full.returncode==0 and not full.stderr,full.stderr
    pcm=run([ffmpeg,'-v','error','-i',recording,'-vn','-f','f32le','-ac','1','-ar','48000','-'],timeout=30);audio=np.frombuffer(pcm.stdout,np.float32)
    assert np.max(abs(audio[24000:72000]))<.0001
    assert np.sqrt(np.mean(audio[3*48000:4*48000]**2))>.03
    results['seek']='Paused cue shows requested picture silently; resumed recording has clean video/audio timestamps'
    active=report/'new active.mixer.json';command('show.saveAs',path=str(active));time.sleep(.7)
    previous=show.read_bytes();command('scene.rename',id=red,name='Changed in active file')
    until(lambda:'Changed in active file' in active.read_text(),'active autosave');assert show.read_bytes()==previous
    assert Path(str(active)+'.backup.mixer.json').exists()
    collection=report/'Portable show';command('show.collect',path=str(collection));until(lambda:not state()['show_collection']['busy'],'collection')
    assert (collection/'show.mixer.json').is_file(),state()['show_collection']
    data=json.loads((collection/'show.mixer.json').read_text());locator=next(x['locator'] for x in data['sources'] if x['id']==sid)
    assert not Path(locator).is_absolute() and hashlib.sha256((collection/locator).read_bytes()).digest()==hashlib.sha256(clip.read_bytes()).digest()
    api('/studio/command',{'command':'show.collect','path':str(collection)},400)
    command('source.remove',id=sid);moved=source_dir/'moved.mp4';clip.rename(moved)
    command('show.open',path=str(collection/'show.mixer.json'),activate=True);until(lambda:track()['frames']>20,'collected media playback')
    assert state()['project_path']==str(collection/'show.mixer.json').replace('\\','/')
    broken=report/'broken.mixer.json';missing_show=json.loads((collection/'show.mixer.json').read_text())
    next(s for s in missing_show['sources'] if s['id']==sid)['locator']=str(source_dir/'missing.mp4')
    broken.write_text(json.dumps(missing_show));command('show.open',path=str(broken));time.sleep(.5)
    replacements=report/'relink';replacements.mkdir();__import__('shutil').copy2(moved,replacements/'missing.mp4')
    assert command('show.relink',path=str(replacements))['relinked']==1
    until(lambda:track()['frames']>20 and not track()['error'],'relinked video')
    missing=report/'missing collection';command('show.open',path=str(broken))
    command('show.collect',path=str(missing));until(lambda:not state()['show_collection']['busy'],'failed collection')
    assert not missing.exists() and 'Missing asset' in state()['show_collection']['status']
    results['projects']='Active Save As, backup, collection without originals, no overwrite, relinking and missing-asset rollback passed'
    results['passed']=True
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
    (report/'project-media.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
