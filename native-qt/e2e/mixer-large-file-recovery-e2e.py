"""Exercise real >4 GiB MKV/MP4 export and app seeking without filling a drive."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import math,shutil
results={};root=None

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

try:
    original=Path(os.environ['STUDIO_RECOVERY_INPUT']).resolve().with_suffix('.mp4')
    before=sha(original)
    assert shutil.disk_usage(report).free>16*2**30,'Keep at least 16 GiB free for this bounded fixture'
    large=report/'over-four-gib.mkv';exported=report/'over-four-gib.mp4'
    copies=math.ceil((4*2**30+512*2**20)/original.stat().st_size)+2
    made=run([ffmpeg,'-v','error','-n','-stream_loop',str(copies-1),'-i',original,'-map','0','-c','copy',large],timeout=240)
    assert made.returncode==0,made.stderr
    assert large.stat().st_size>4*2**30,large.stat().st_size
    large_hash=sha(large);results['inputBytes']=large.stat().st_size
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    start=time.monotonic();command('record.export',input=str(large),path=str(exported))
    until(lambda:not state()['recording']['exporting'],'large MP4 export',240)
    assert exported.exists() and exported.stat().st_size>4*2**30,state()['recording']['export_status']
    results['exportSeconds']=time.monotonic()-start;results['outputBytes']=exported.stat().st_size
    assert sha(large)==large_hash and sha(original)==before
    positions=[]
    for path in (large,exported):
        sid=command('source.add',kind='media',name=path.suffix+' large media',locator=str(path))['id']
        def track():return next(s for s in state()['sources'] if s['id']==sid)
        until(lambda:track()['frames']>10 and track()['durationMs']>3600000,'large file playback',45)
        total=track()['durationMs'];command('media.set',id=sid,paused=True,seekMs=total-3000)
        until(lambda:track()['positionMs']>=total-3500,'seek beyond 4 GiB',45)
        picture=image('program',path.suffix[1:]+'-near-end.png')
        assert float(np.std(np.asarray(picture)))>30
        frames=track()['frames'];command('media.set',id=sid,paused=False)
        until(lambda:track()['frames']>frames+15 and track()['peak']>.01,'picture and audio after large seek',15)
        command('source.remove',id=sid)
        for offset in (0,total/2000,total/1000-3):
            decoded=run([ffmpeg,'-v','error','-ss',str(offset),'-i',path,'-t','2','-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=60)
            assert decoded.returncode==0 and not decoded.stderr,(path,offset,decoded.stderr)
        positions.append({'container':path.suffix,'durationMs':total,'seekMs':total-3000,'beginningMiddleEndAllTracksDecoded':True,'appPlaybackAfterSeek':True})
    results.update({'files':positions,'originalsUnchanged':True,'passed':True,'largeFixtureRetention':'removed after successful verification; hashes and near-end screenshots retained','inputSha256':large_hash,'outputSha256':sha(exported)})
finally:
    if target:cli_call('quit');root.wait(timeout=30)
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'large-file-recovery.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2),flush=True)
if results.get('passed'):
    # Only these two generated files, in this workflow's report directory.
    for path in (large,exported):
        assert path.resolve().parent==report and path.name in ('over-four-gib.mkv','over-four-gib.mp4')
        path.unlink()
