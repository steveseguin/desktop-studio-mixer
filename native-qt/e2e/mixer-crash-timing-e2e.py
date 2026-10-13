"""Measure playable media after real abrupt exits at different recording ages."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import re
results={'executableSha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),'cases':[]}
root=None

def launch():
    global target,root
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)

def crash():
    global target
    children=root.children(recursive=True);root.kill();root.wait(timeout=10);target=None
    _,alive=psutil.wait_procs(children,timeout=15)
    assert not alive,[(p.pid,p.name()) for p in alive]

def inspect(path):
    entry={'file':path.name,'bytes':path.stat().st_size if path.exists() else 0}
    if not entry['bytes']:return entry
    decoded=run([ffmpeg,'-v','error','-i',path,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=120)
    entry['decodeExit']=decoded.returncode;entry['decodeWarnings']=decoded.stderr.decode(errors='replace').strip()
    packets=run([ffmpeg,'-v','error','-i',path,'-map','0:v:0','-c:v','copy','-f','framecrc','-'],text=True,encoding='utf-8',timeout=60)
    tb=re.search(r'#tb 0: (\d+)/(\d+)',packets.stdout)
    rows=[line.split(',') for line in packets.stdout.splitlines() if line and not line.startswith('#')]
    entry['videoPackets']=len(rows)
    if tb and rows:
        scale=int(tb[1])/int(tb[2]);entry['lastVideoSeconds']=float(rows[-1][2])*scale
    return entry

try:
    clips=[]
    for n in range(4):
        clip=report/f'tone-{n}.mp4'
        made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=640x360:r=30','-f','lavfi','-i',f'sine=frequency={400+n*100}:sample_rate=48000','-t','15','-c:v','libopenh264','-c:a','aac',clip],timeout=40)
        assert made.returncode==0,made.stderr;clips.append(clip)
    launch()
    ids=[command('source.add',kind='media',name=f'Crash track {n+1}',locator=str(clip))['id'] for n,clip in enumerate(clips)]
    until(lambda:all(s['frames']>15 for s in state()['sources']),'all sources playing')
    ages=[float(value) for value in os.environ.get('STUDIO_CRASH_AGES','.1,.7,2.3,5.5,60').split(',')]
    during_stop=os.environ.get('STUDIO_CRASH_DURING_STOP')=='1'
    for age in ages:
        output=report/f'crash-{age:g}.mkv'
        command('record.start',path=str(output),audioSources=ids,isoSources=ids)
        until(lambda:state()['recording']['status'].startswith('Recording:'),'recording started')
        begin=time.monotonic();time.sleep(age)
        snapshot=state();paths=[output]+[Path(s['path']) for s in snapshot['recording']['separate_videos']]
        actual=time.monotonic()-begin
        if during_stop:
            writer=next(p for p in root.children(recursive=True) if p.name().lower()=='ffmpeg.exe' and str(output) in p.cmdline())
            writer.suspend();command('record.stop')
            assert state()['recording']['busy'],'Did not catch finalization'
        crash()
        evidence=[inspect(p) for p in paths]
        case={'requestedAgeSeconds':age,'secondsAfterRecordingStatus':actual,'duringFinalization':during_stop,'files':evidence,'allDescendantsExited':True}
        results['cases'].append(case)
        (report/'crash-timing.json').write_text(json.dumps(results,indent=2))
        print(json.dumps(case),flush=True)
        if age>=5:
            assert all(e.get('videoPackets',0)>0 and e.get('decodeExit')==0 for e in evidence),evidence
            assert all(e['lastVideoSeconds']>=age-3 for e in evidence),evidence
        launch()
        assert not state()['recording']['active'] and not state()['output']['active']
        until(lambda:all(s['frames']>15 for s in state()['sources']),'sources after relaunch')
        if age==60:
            recovered=[]
            for path in paths:
                exported=path.with_suffix('.mp4');command('record.export',input=str(path),path=str(exported))
                until(lambda:not state()['recording']['exporting'],'MP4 recovery',60)
                entry=inspect(exported);assert entry.get('videoPackets',0)>1600 and entry['decodeExit']==0 and not entry['decodeWarnings'],entry
                recovered.append(entry)
            metadata=run([ffmpeg,'-hide_banner','-i',output.with_suffix('.mp4')],text=True,encoding='utf-8',timeout=20).stderr
            assert metadata.count('Audio:')==5,metadata
            tones=[]
            for n in range(4):
                r=run([ffmpeg,'-v','error','-ss','30','-i',output.with_suffix('.mp4'),'-map',f'0:a:{n+1}','-t','1','-ac','1','-ar','48000','-f','f32le','-'],timeout=30)
                assert r.returncode==0 and len(r.stdout)==48000*4,r.stderr
                samples=np.frombuffer(r.stdout,np.float32)
                amplitude=float(abs(samples@np.exp(-2j*np.pi*(400+n*100)*np.arange(48000)/48000))*2/48000)
                assert amplitude>.1,(n,amplitude);tones.append(amplitude)
            results['minuteRecovery']={'files':recovered,'namedAudioTones':tones,'audioTrackCount':5,'allTracksDecoded':True}
    results['passed']=True
finally:
    if target:
        cli_call('quit');root.wait(timeout=30)
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'crash-timing.json').write_text(json.dumps(results,indent=2))
