"""Opt-in input mode, track selection, restart, recording and export in real GUI."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-multitrack-ui-e2e.py').read_text().split('\ntry:\n')[0])

try:
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input()
    studio_mode(False);scene('Main');enabled(control('monitorEnable'),False)
    clip=report/'six-voices.wav'
    frequencies=[1300,1700,2300,2900,3100,3700,440]
    expression='|'.join(f'.08*sin(2*PI*{f}*t)' for f in frequencies[:6])
    made=run('-v','error','-n','-f','lavfi','-i',f'aevalsrc={expression}:s=48000:c=5.1','-t','5','-c:a','pcm_s24le',clip)
    assert made.returncode==0,made.stderr
    press('addSource');dialog=window.child_window(title='Add source',control_type='Window');dialog.wait('visible')
    choose_dialog(dialog,'sourceKind','Video / audio file')
    control('sourceName',dialog).set_edit_text('Six voices');control('sourceValue',dialog).set_edit_text(str(clip))
    choose_dialog(dialog,'sourceInputChannels','Six separate inputs')
    dialog.capture_as_image().save(str(report/'six-channel-choice.png'))
    dialog.child_window(title='OK',control_type='Button').click_input()
    wait(lambda:any(s['name']=='Six voices' and s['receivedChannels']==6 and s['peak']>.03 for s in state()['sources']),'Six-channel file plays')
    sid=next(s['id'] for s in state()['sources'] if s['name']=='Six voices')
    reference_clip=report/'reference.wav'
    made=run('-v','error','-n','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','5',reference_clip)
    assert made.returncode==0,made.stderr
    add_source('Video / audio file','Reference',str(reference_clip))
    reference_id=next(s['id'] for s in state()['sources'] if s['name']=='Reference')
    selected=[f'{sid}:channel:{n}' for n in range(1,7)]+[reference_id]
    menu('Recording tracks...');dialog=window.child_window(title='Recording tracks',control_type='Window');dialog.wait('visible')
    for key in selected:
        box=control('record_audio_'+key,dialog);box.set_focus()
        if not box.get_toggle_state():box.type_keys('{SPACE}')
        assert box.get_toggle_state()
    dialog.capture_as_image().save(str(report/'individual-input-tracks.png'))
    dialog.child_window(title='Save',control_type='Button').click_input()
    wait(lambda:json.loads(show_path.read_text()).get('recordAudioSources')==selected,'Saved discrete recording choices')

    # Reopen the saved show in a new process, then inspect the actual controls.
    previous=psutil.Process(args.pid);descendants=previous.children(recursive=True)
    window.type_keys('^q');previous.wait(timeout=30)
    _,alive=psutil.wait_procs(descendants,timeout=15);assert not alive
    restarted_log=(report/'restarted-app.log').open('wb')
    restarted=subprocess.Popen([str(executable),'--studio-show='+str(show_path),'--local-control-discovery='+os.environ['STUDIO_E2E_DISCOVERY']],
        env=dict(os.environ),stdout=restarted_log,stderr=restarted_log,creationflags=subprocess.CREATE_NO_WINDOW)
    (report/'app.pid').write_text(str(restarted.pid))
    app=Application(backend='uia').connect(process=restarted.pid,timeout=30)
    window=app.window(title='Desktop Studio - Powered by VDO.Ninja');window.wait('visible',timeout=30);window.set_focus()
    control.__defaults__=(window,);press.__defaults__=(window,)
    wait(lambda:any(s['id']==sid and s['inputChannels']==6 and s['receivedChannels']==6 for s in state()['sources']),'Input mode survives restart')
    window.child_window(title='Studio',control_type='TabItem').click_input()
    scene('Main')
    menu('Recording tracks...');dialog=window.child_window(title='Recording tracks',control_type='Window');dialog.wait('visible')
    assert all(control('record_audio_'+key,dialog).get_toggle_state() for key in selected)
    assert not control('record_audio_'+sid,dialog).get_toggle_state()
    dialog.child_window(title='Cancel',control_type='Button').click_input()
    results['inputModeAndRecordingChoicesSurviveRestart']=True

    recording=report/'Six inputs GUI.mkv';press('studioRecord');filename('Record Program',recording);wait_recording(True);time.sleep(8)
    press('studioRecord');wait(lambda:not state()['recording']['busy'],'GUI recording finalizes')
    assert state()['recording']['status'].startswith('Recording saved:')
    original=hashlib.sha256(recording.read_bytes()).hexdigest()
    menu('Export recording as MP4...');filename('Choose recording to export',recording)
    exported=report/'Six inputs GUI.mp4';filename('Export MP4 (original recording preserved)',exported)
    wait(lambda:not state()['recording']['exporting'],'GUI export finalizes')
    assert state()['recording']['export_status'].startswith('MP4 saved:')
    assert hashlib.sha256(recording.read_bytes()).hexdigest()==original
    expected=['Program']+[f'Six voices - Input {n}' for n in range(1,7)]+['Reference']
    tone_reference=np.exp(-2j*np.pi*np.array(frequencies)[:,None]*np.arange(48000)[None,:]/48000)
    results['files']=[]
    for path in (recording,exported):
        assert names(path)==expected,names(path)
        decoded=run('-v','error','-i',path,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-')
        assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
        rows=[]
        for track in range(1,8):
            raw=run('-v','error','-i',path,'-ss','3','-map',f'0:a:{track}','-t','1','-ac','1','-ar','48000','-f','f32le','-')
            assert raw.returncode==0 and len(raw.stdout)==48000*4
            samples=np.frombuffer(raw.stdout,np.float32);row=(abs(tone_reference@samples)*2/48000).tolist();rows.append(row)
            assert row[track-1]>.025 and max(value for n,value in enumerate(row) if n!=track-1)<row[track-1]*.01,(track,row)
            assert float(np.min(np.sqrt(np.mean(samples.reshape(-1,480)**2,axis=1))))>.01
        results['files'].append({'file':path.name,'names':expected,'sourceAmplitudes':rows,'fullDecode':'clean','sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    window.capture_as_image().save(str(report/'six-input-export.png'))
    results['passed']=True;print(json.dumps({'passed':True,'guiSixInputs':True,'audioTracks':8}),flush=True)
except BaseException as error:
    results['error']=str(error);raise
finally:
    (report/'six-input-ui.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
