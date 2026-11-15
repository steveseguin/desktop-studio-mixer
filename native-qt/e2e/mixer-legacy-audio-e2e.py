"""Open a real pre-six-input show, record its saved choices, export and reopen.

Uses the preserved seven-source GUI fixture from the older portable package.
Copies the show and its local media; never upgrades the original or user show.
Enumerates Windows input formats without starting microphone capture.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-multitrack-roundtrip-e2e.py').read_text(encoding='utf-8').split('\ntry:\n')[0])
import shutil

results={'passed':False,'executableSha256':file_hash(package/'desktop-studio.exe')}
legacy=Path(os.environ.get('STUDIO_LEGACY_SHOW',str(Path(__file__).resolve().parents[1]/'qa/reports/multitrack-gui-20261114-final/multitrack-ui/show.mixer.json'))).resolve()
legacy_hash=None


def close_owned():
    global target
    if not target:return
    descendants=root.children(recursive=True)
    cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(descendants,timeout=15)
    assert not alive,[p.pid for p in alive]


try:
    legacy_hash=file_hash(legacy);document=json.loads(legacy.read_text(encoding='utf-8'))
    assert not any('inputChannels' in source for source in document['sources'])
    selection=document['recordAudioSources'];assert len(selection)==7
    by_id={source['id']:source for source in document['sources']}
    for source in document['sources']:
        assert source['kind'] in ('media','title'),'Use a local-media-only legacy fixture'
        if source['kind']!='media':continue
        relative=Path(source['locator']);destination=(report/relative).resolve()
        assert not relative.is_absolute() and destination.is_relative_to(report)
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(legacy.parent/relative,destination)
    shutil.copyfile(legacy,show)
    assert file_hash(show)==legacy_hash
    results['legacyShowSha256']=legacy_hash

    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
    command('monitor.set',enabled=False)
    main=next(scene['id'] for scene in document['scenes'] if scene['name']=='Main')
    command('workspace.set',mode='single');command('scene.select',id=main)
    until(lambda:all(any(s['id']==sid and s['peak']>.02 for s in state()['sources']) for sid in selection),'Legacy audio sources play',45)
    s=state();assert not s['output']['active']
    assert all(source['inputChannels']==2 for source in s['sources'])
    assert all(choice['input']==0 for choice in s['recording']['audio_choices'])
    results['missingInputModeDefaultsToStereo']=True
    devices=cli_call('sources','audio-inputs')
    # Device IDs are unnecessary for the acceptance record.
    entries=devices.get('sources',[]) if isinstance(devices,dict) else devices
    results['inputDeviceFormats']=[{k:v for k,v in device.items() if k in ('name','channels','sampleRate','bitsPerSample')} for device in entries]

    recording=report/'legacy-selection.mkv';exported=report/'legacy-selection.mp4'
    # Omit selection arguments: the old show's seven saved choices must work.
    command('record.start',path=str(recording))
    until(lambda:state()['recording']['status'].startswith('Recording:'),'Legacy saved selection records')
    assert state()['recording']['audio_sources']==selection
    time.sleep(8);command('record.stop')
    until(lambda:not state()['recording']['busy'],'Legacy recording finalizes',60)
    assert state()['recording']['status'].startswith('Recording saved:')
    expected=['Program']+[by_id[sid]['name'] for sid in selection]
    frequencies=[400+50*(int(by_id[sid]['name'].split()[-1])-1) for sid in selection]
    reference=np.exp(-2j*np.pi*np.array(frequencies)[:,None]*np.arange(48000)[None,:]/48000)
    original=file_hash(recording)
    command('record.export',input=str(recording),path=str(exported))
    until(lambda:not state()['recording']['exporting'],'Legacy recording exports',60)
    assert state()['recording']['export_status'].startswith('MP4 saved:')
    results['files']=[]
    for path in (recording,exported):
        decode_all(path);assert metadata(path)==expected
        rows=[]
        for index in range(7):
            raw=run([ffmpeg,'-v','error','-i',path,'-ss','3','-map',f'0:a:{index+1}','-t','1','-ac','1','-ar','48000','-f','f32le','-'],timeout=30)
            assert raw.returncode==0 and len(raw.stdout)==48000*4
            samples=np.frombuffer(raw.stdout,np.float32)
            row=(abs(reference@samples)*2/48000).tolist();rows.append(row)
            assert row[index]>.1 and max(v for n,v in enumerate(row) if n!=index)<row[index]*.01,(index,row)
        results['files'].append({'file':path.name,'trackNames':expected,'sourceAmplitudes':rows,'allStreamsDecode':True,'sha256':file_hash(path)})
    assert file_hash(recording)==original
    assert audio_packet_hashes(recording)==audio_packet_hashes(exported)
    results['exportAudioUnchanged']=True
    close_owned()
    saved_show=json.loads(show.read_text(encoding='utf-8'))
    assert saved_show['recordAudioSources']==selection
    assert all(source['inputChannels']==2 for source in saved_show['sources'])
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
    until(lambda:all(any(s['id']==sid and s['inputChannels']==2 and s['peak']>.02 for s in state()['sources']) for sid in selection),'Resaved legacy show reopens',45)
    close_owned();results['resavedShowReopens']=True;results['cleanExit']=True;results['passed']=True
except BaseException as error:
    results['error']=str(error);raise
finally:
    try:close_owned()
    except Exception as error:results['cleanupError']=str(error);results['passed']=False
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    results['originalLegacyShowUnchanged']=legacy_hash is not None and file_hash(legacy)==legacy_hash
    results['userShowUnchanged']=(file_hash(user_show) if user_show.exists() else None)==original_hash
    results['passed']=results['passed'] and results['originalLegacyShowUnchanged'] and results['userShowUnchanged']
    (report/'legacy-audio.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    assert results['originalLegacyShowUnchanged'] and results['userShowUnchanged']
print(json.dumps({'passed':results['passed'],'audioTracks':8,'inputDeviceFormats':results.get('inputDeviceFormats',[])}))
