"""Real media, show reload, recording and MP4 export with Unicode/spaced paths."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
try:
    folder=report/'Émission_日本語';folder.mkdir()
    fixture=folder/'Clíp 你好.mp4'
    made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=size=640x360:rate=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','5','-c:v','libopenh264','-c:a','aac',fixture],timeout=30);assert made.returncode==0,made.stderr
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    sid=command('source.add',kind='media',name='Café 日本語',locator=str(fixture))['id']
    until(lambda:next(s for s in state()['sources'] if s['id']==sid)['frames']>30,'Unicode media')
    exported=folder/'Épisode 你好.mixer.json';command('show.save',path=str(exported))
    document=json.loads(exported.read_text(encoding='utf-8'));saved_source=next(s for s in document['sources'] if s['id']==sid)
    assert saved_source['locator']==fixture.name,saved_source['locator']
    command('scene.select',id=green);command('source.remove',id=sid);command('show.open',path=str(exported))
    until(lambda:next(s for s in state()['sources'] if s['id']==sid)['frames']>30,'Reload relative Unicode media')
    recording=folder/'Recording été №1.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['active'],'Unicode recording')
    time.sleep(5);command('record.stop');until(lambda:not state()['recording']['busy'],'Finalize')
    results['recording']=decode(recording)
    mp4=folder/'Export été №1.mp4';command('record.export',input=str(recording),path=str(mp4));until(lambda:not state()['recording']['exporting'],'Unicode export')
    assert state()['recording']['export_status'].startswith('MP4 saved:'),state()['recording'];results['mp4']=decode(mp4)
    results['relativeUnicodeShowReload']=True;results['passed']=True
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
    (report/'file-workflows.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
