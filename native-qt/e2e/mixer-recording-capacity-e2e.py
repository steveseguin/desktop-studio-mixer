"""Record the advertised maximum of 16 separate audio tracks and decode all."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    ids=[];with_video=os.environ.get('STUDIO_CAPACITY_ISO')=='1'
    for n in range(16):
        clip=report/f'track-{n}{".mp4" if with_video and n<4 else ".wav"}'
        video=['-f','lavfi','-i','testsrc2=s=640x360:r=30'] if with_video and n<4 else []
        codecs=['-c:v','libopenh264','-c:a','aac'] if video else []
        r=run([ffmpeg,'-v','error','-y',*video,'-f','lavfi','-i',f'sine=frequency={400+50*n}:sample_rate=48000','-t','40',*codecs,clip],timeout=30);assert r.returncode==0
        ids.append(command('source.add',kind='media',name=f'Track {n+1}',locator=str(clip))['id'])
    until(lambda:all(s['peak']>.02 for s in state()['sources']),'all source audio')
    output=report/'sixteen-tracks.mkv';command('record.start',path=str(output),audioSources=ids,isoSources=ids[:4] if with_video else [])
    until(lambda:state()['recording']['status'].startswith('Recording:'),'recording')
    for _ in range(60):
        time.sleep(.5);current=state()['recording'];assert current['active'],current
    command('record.stop');until(lambda:not state()['recording']['busy'],'finalize',seconds=60)
    for separate in state()['recording']['separate_videos']:
        assert 'saved' in separate['status'].lower(),separate
        r=run([ffmpeg,'-v','error','-i',separate['path'],'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=90);assert r.returncode==0 and not r.stderr,r.stderr
    r=run([ffmpeg,'-v','error','-i',output,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=120);assert r.returncode==0 and not r.stderr,r.stderr
    measured=[]
    for n in range(16):
        r=run([ffmpeg,'-v','error','-ss','5','-i',output,'-map',f'0:a:{n+1}','-t','1','-ac','1','-ar','48000','-f','f32le','-'],timeout=30);assert r.returncode==0
        samples=np.frombuffer(r.stdout,np.float32);assert len(samples)==48000
        amplitude=float(abs(samples@np.exp(-2j*np.pi*(400+50*n)*np.arange(48000)/48000))*2/48000);assert amplitude>.10,(n,amplitude);measured.append(amplitude)
    results={'passed':True,'namedSeparateTracks':16,'programPlusSeparateTracks':17,'separateVideos':4 if with_video else 0,'seconds':30,'amplitudes':measured,'allStreamsDecoded':True}
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
    (report/'recording-capacity.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
