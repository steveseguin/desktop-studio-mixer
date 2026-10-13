"""Four source videos, privacy routing, source removal and a failed Program writer."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    ids=[]
    for n,color in enumerate(('red','green','blue','yellow')):
        clip=report/(color+'.mp4')
        r=run([ffmpeg,'-v','error','-y','-f','lavfi','-i',f'color={color}:s=640x360:r=30','-f','lavfi','-i',f'sine=frequency={440+n*220}:sample_rate=48000','-t','40','-c:v','libopenh264','-c:a','aac',clip],timeout=40);assert r.returncode==0,r.stderr
        ids.append(command('source.add',kind='media',name=color,locator=str(clip))['id'])
    until(lambda:all(s['frames']>20 for s in state()['sources']),'four media sources')
    recording=report/'four-sources.mkv';command('record.start',path=str(recording),audioSources=ids,isoSources=ids)
    until(lambda:len(state()['recording']['separate_videos'])==4 and all(s['status'].startswith('Recording:') for s in state()['recording']['separate_videos']),'four encoders')
    time.sleep(4)
    for sid in ids:command('layer.set',source=sid,visible=False)
    time.sleep(3)
    # Mute and On air must protect both the named audio and the source file.
    command('audio.set',source=ids[0],muted=True);command('audio.set',source=ids[1],programAudio=False)
    time.sleep(3)
    command('audio.set',source=ids[0],muted=False);command('audio.set',source=ids[1],programAudio=True)
    command('production.set',privateBreak=True);time.sleep(3);command('production.set',privateBreak=False)
    time.sleep(3)
    command('source.remove',id=ids[2]);time.sleep(3)
    before=state()['recording'];paths=[Path(s['path']) for s in before['separate_videos']]
    writer=next(p for p in root.children(recursive=True) if p.name().lower()=='ffmpeg.exe' and any(str(recording).replace('\\','/').casefold()==a.replace('\\','/').casefold() for a in p.cmdline()))
    writer.kill();writer.wait(timeout=10)
    until(lambda:not state()['recording']['program_active'],'failed Program writer',seconds=20)
    assert all(s['active'] for s in state()['recording']['separate_videos']),state()['recording']
    assert state()['recording']['active']
    sizes=[p.stat().st_size for p in paths];time.sleep(4)
    assert all(p.stat().st_size>size for p,size in zip(paths,sizes))
    api('/studio/command',{'command':'canvas.set','format':1},400)
    command('record.stop');until(lambda:not state()['recording']['busy'],'all finalizing',seconds=60)
    evidence=[]
    for n,path in enumerate(paths):
        r=run([ffmpeg,'-v','error','-i',path,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=90);assert r.returncode==0 and not r.stderr,r.stderr
        r=run([ffmpeg,'-v','error','-i',path,'-map','0:a:0','-ac','1','-ar','48000','-f','f32le','-'],timeout=30);assert r.returncode==0
        audio=np.frombuffer(r.stdout,np.float32)
        rms=[float(np.sqrt(np.mean(audio[i:i+24000]**2))) for i in range(48000,len(audio)-48000,24000)]
        assert sum(v<.0001 for v in rms)>=(8 if n<2 else 3),rms
        tail=float(np.sqrt(np.mean(audio[-96000:-48000]**2)))
        assert (tail<.0001 if n==2 else tail>.03),(n,tail)
        r=run([ffmpeg,'-v','error','-sseof','-2','-i',path,'-frames:v','1','-f','image2pipe','-vcodec','png','-'],timeout=30)
        pixel=Image.open(io.BytesIO(r.stdout)).convert('RGB').getpixel((320,180))
        assert (max(pixel)<5 if n==2 else max(pixel)>100),(n,pixel)
        # Off-screen picture and sound remain available in an ISO before removal.
        r=run([ffmpeg,'-v','error','-ss','5','-i',path,'-frames:v','1','-f','image2pipe','-vcodec','png','-'],timeout=30)
        assert max(Image.open(io.BytesIO(r.stdout)).convert('RGB').getpixel((320,180)))>100
        evidence.append({'file':path.name,'cleanDecode':True,'silentHalfSeconds':sum(v<.0001 for v in rms),'tailRms':tail,'tailPixel':pixel})
    results={'passed':True,'fourSourceRecordings':evidence,'programWriterFailedIndependently':True,'offscreenSourcesRetained':True,'muteOnAirAndPrivateBreak':True,'removedSourceSilentAndBlack':True}
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
    (report/'recording-isolation.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
