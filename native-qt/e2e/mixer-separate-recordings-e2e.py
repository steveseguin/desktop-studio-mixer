"""Decode named audio tracks and source video; fail one writer during Program."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    clips=[]
    for color,tone in [('red',440),('blue',880)]:
        clip=report/(color+'.mp4');r=run([ffmpeg,'-v','error','-y','-f','lavfi','-i',f'color={color}:s=640x360:r=30','-f','lavfi','-i',f'sine=frequency={tone}:sample_rate=48000','-t','20','-c:v','libopenh264','-c:a','aac',clip],timeout=35);assert r.returncode==0,r.stderr;clips.append(clip)
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    ids=[command('source.add',kind='media',name=name,locator=str(clip))['id'] for name,clip in zip(('Alice','Bob'),clips)]
    for sid in ids:command('audio.set',source=sid,followsScene=False)
    until(lambda:all(s['frames']>20 for s in state()['sources']),'media')
    path=report/'multitrack.mkv'
    api('/studio/command',{'command':'record.start','path':str(path),'audioSources':[ids[0],ids[0]]},400)
    api('/studio/command',{'command':'record.start','path':str(path),'audioSources':[False]},400)
    command('record.start',path=str(path),audioSources=ids,isoSources=[ids[0]])
    until(lambda:state()['recording']['status'].startswith('Recording:'),'Program')
    until(lambda:state()['recording']['separate_videos'][0]['status'].startswith('Recording:'),'ISO')
    time.sleep(4)
    # Separately recorded sources remain present even off-screen.
    command('layer.set',source=ids[0],visible=False)
    command('production.set',privateBreak=True);time.sleep(3);command('production.set',privateBreak=False);time.sleep(3)
    iso=Path(state()['recording']['separate_videos'][0]['path'])
    writer=next(p for p in root.children(recursive=True) if p.name().lower()=='ffmpeg.exe' and any(str(iso).replace('\\','/').casefold()==a.replace('\\','/').casefold() for a in p.cmdline()))
    writer.kill();writer.wait(timeout=10)
    until(lambda:not state()['recording']['separate_videos'][0]['active'],'failed separate writer',seconds=20)
    assert state()['recording']['program_active'];time.sleep(4)
    results['independentWriterFailure']=state()['recording']['separate_videos'][0]['status']
    command('record.stop');until(lambda:not state()['recording']['busy'],'finalize')
    decoded=run([ffmpeg,'-v','error','-i',path,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=40);assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    tracks=[]
    for track in range(3):
        raw=run([ffmpeg,'-v','error','-i',path,'-map',f'0:a:{track}','-ac','1','-ar','48000','-f','f32le','-'],timeout=30);assert raw.returncode==0,raw.stderr
        audio=np.frombuffer(raw.stdout,np.float32);sample=audio[48000:96000]
        tones=[float(abs(sample@np.exp(-2j*np.pi*f*np.arange(len(sample))/48000))*2/len(sample)) for f in (440,880)]
        assert tones[track-1 if track else 0]>.08,tones
        if track:assert tones[2-track]<.004,tones
        # The break is visible in all audio tracks, not just Program.
        halfseconds=[float(np.sqrt(np.mean(audio[n:n+24000]**2))) for n in range(48000,len(audio)-48000,24000)]
        assert sum(x<.0001 for x in halfseconds)>=3,halfseconds
        assert np.sqrt(np.mean(audio[-96000:-48000]**2))>.03
        tracks.append({'tone440':tones[0],'tone880':tones[1],'silentHalfSeconds':sum(x<.0001 for x in halfseconds)})
    meta=run([ffmpeg,'-hide_banner','-i',path],text=True,encoding='utf-8');assert 'Alice' in meta.stderr and 'Bob' in meta.stderr and 'Program' in meta.stderr,meta.stderr
    frame=run([ffmpeg,'-v','error','-ss','2','-i',iso,'-frames:v','1','-f','image2pipe','-vcodec','png','-'],timeout=25);assert frame.returncode==0,frame.stderr
    pixel=Image.open(io.BytesIO(frame.stdout)).convert('RGB').getpixel((320,180));assert pixel[0]>220 and pixel[2]<30,pixel
    results['audioTracks']=tracks;results['sourcePicture']=pixel;results['passed']=True
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
    (report/'separate-recordings.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
