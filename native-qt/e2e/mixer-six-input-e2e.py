"""Actual six-channel PCM file -> packaged capture worker -> named input tracks.

Exercises native media decode at 44.1/48/96 kHz, independent voice-range inputs,
source privacy, persisted input mode, MP4 export, and explicit invalid-input
behavior. Physical microphone/interface capture remains a separate workflow.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import re

results={'passed':False,'cases':[],'executableSha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest()}
root=None
frequencies=[1300,1700,2300,2900,3100,3700,440]
reference=np.exp(-2j*np.pi*np.array(frequencies)[:,None]*np.arange(48000)[None,:]/48000)


def decode(path):
    r=run([ffmpeg,'-v','error','-i',path,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=90)
    assert r.returncode==0 and not r.stderr,r.stderr


def names(path):
    r=run([ffmpeg,'-hide_banner','-i',path],timeout=30)
    info=r.stderr.decode(errors='replace');(report/(path.name+'.metadata.txt')).write_text(info)
    blocks=[b for b in re.split(r'\n\s*Stream #0:',info)[1:] if 'Audio:' in b.splitlines()[0]]
    return [re.search(r'^\s*(?:title|name)\s*:\s*(.*?)\s*$',b,re.M).group(1) for b in blocks]


def samples(path,track,seconds):
    r=run([ffmpeg,'-v','error','-i',path,'-ss',str(seconds),'-map',f'0:a:{track}','-t','1','-ac','1','-ar','48000','-f','f32le','-'],timeout=45)
    assert r.returncode==0 and len(r.stdout)==48000*4,(path,track,len(r.stdout),r.stderr)
    return np.frombuffer(r.stdout,np.float32)


def tone_row(path,track,seconds):
    return (abs(reference@samples(path,track,seconds))*2/48000).tolist()


def assert_isolated(row,index):
    assert row[index]>.025,(index,row)
    assert max(value for n,value in enumerate(row) if n!=index)<row[index]*.01,(index,row)


def quit_owned():
    global target
    descendants=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(descendants,timeout=15);assert not alive,[p.pid for p in alive]


try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    command('monitor.set',enabled=False)
    stereo=report/'reference.wav'
    made=run([ffmpeg,'-v','error','-n','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','5',stereo],timeout=30)
    assert made.returncode==0,made.stderr
    other=command('source.add',kind='media',name='Reference',locator=str(stereo))['id']
    api('/studio/command',{'command':'source.add','kind':'browser','name':'Invalid six browser','locator':'http://127.0.0.1:1/','inputChannels':6},400)
    api('/studio/command',{'command':'source.update','id':other,'inputChannels':4},400)
    for rate,channel_count in ((44100,6),(48000,6),(96000,6),(48000,8)):
        tag=f'{rate}-{channel_count}ch'
        clip=report/f'inputs-{tag}.wav'
        fixture_frequencies=frequencies[:6]+([7000,8000] if channel_count==8 else [])
        expression='|'.join(f'.08*sin(2*PI*{f}*t)' for f in fixture_frequencies)
        layout='7.1' if channel_count==8 else '5.1'
        made=run([ffmpeg,'-v','error','-n','-f','lavfi','-i',f'aevalsrc={expression}:s={rate}:c={layout}','-t','5','-c:a','pcm_s24le',clip],timeout=30)
        assert made.returncode==0,made.stderr
        source=command('source.add',kind='media',name=f'Inputs {tag}',locator=str(clip),inputChannels=6)['id']
        until(lambda:any(s['id']==source and s['receivedChannels']==channel_count and s['peak']>.03 for s in state()['sources']),'Native input channels',45)
        if rate==44100:
            quit_owned()
            target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
            until(lambda:any(s['id']==source and s['inputChannels']==6 and s['receivedChannels']==6 and s['peak']>.03 for s in state()['sources']),'Persisted six-channel mode',45)
            results['inputModeSurvivesRestart']=True
            command('audio.set',source=source,noiseGate=True,compression=True,audioDelayMs=100)
        choices=[choice for choice in state()['recording']['audio_choices'] if choice['source']==source and choice['input']>0]
        assert [choice['input'] for choice in choices]==list(range(1,7)),choices
        selected=[choice['id'] for choice in choices]+[other]
        output=report/f'inputs-{tag}.mkv'
        command('record.start',path=str(output),audioSources=selected,isoSources=[])
        until(lambda:state()['recording']['program_active'],'Recording inputs')
        start=time.monotonic()
        api('/studio/command',{'command':'source.update','id':source,'inputChannels':2},400)
        time.sleep(3)
        quiet=[]
        if rate==44100:
            for control_name,on,off in [('mute',{'muted':True},{'muted':False}),('on-air',{'programAudio':False},{'programAudio':True})]:
                command('audio.set',source=source,**on);quiet.append((control_name,time.monotonic()-start+1));time.sleep(3)
                command('audio.set',source=source,**off);time.sleep(2)
            command('production.set',privateBreak=True);quiet.append(('private-break',time.monotonic()-start+1));time.sleep(3)
            command('production.set',privateBreak=False);time.sleep(3)
        else:time.sleep(5)
        tail=time.monotonic()-start-2
        results['lastRecordingObservation']={'file':output.name,'quietWindows':quiet,'tailWindow':tail}
        command('record.stop');until(lambda:not state()['recording']['busy'],'Input recording finalizes',60)
        assert state()['recording']['status'].startswith('Recording saved:'),state()['recording']
        decode(output)
        expected=['Program']+[f'Inputs {tag} - Input {n}' for n in range(1,7)]+['Reference']
        assert names(output)==expected,names(output)
        rows=[tone_row(output,n+1,1) for n in range(7)]
        late=[tone_row(output,n+1,tail) for n in range(7)]
        minimum_windows=[]
        for when in (1,tail):
            for track in range(1,8):
                pcm=samples(output,track,when).reshape(-1,480)
                minimum=float(np.min(np.sqrt(np.mean(pcm**2,axis=1))));minimum_windows.append(minimum)
                assert minimum>.01,('Audio dropout',rate,track,when,minimum)
        for index,row in enumerate(rows):assert_isolated(row,index)
        for index,row in enumerate(late):assert_isolated(row,index)
        program=tone_row(output,0,1);assert min(program)>.01,program
        if channel_count==8:
            omitted=np.exp(-2j*np.pi*np.array([7000,8000])[:,None]*np.arange(48000)[None,:]/48000)
            for track in range(8):
                assert float(max(abs(omitted@samples(output,track,1))*2/48000))<.0001,'Inputs 7/8 leaked into first six'
        silence=[]
        for label,when in quiet:
            indices=range(8) if label=='private-break' else range(1,7)
            for index in indices:
                rms=float(np.sqrt(np.mean(samples(output,index,when)**2)));silence.append({'control':label,'track':index,'rms':rms})
                assert rms<.0001,(label,index,rms)
            if label!='private-break':assert_isolated(tone_row(output,7,when),6)
        exported=report/f'inputs-{tag}.mp4';original=hashlib.sha256(output.read_bytes()).hexdigest()
        command('record.export',input=str(output),path=str(exported));until(lambda:not state()['recording']['exporting'],'Export input tracks',60)
        assert state()['recording']['export_status'].startswith('MP4 saved:'),state()['recording']
        decode(exported);assert names(exported)==expected
        assert hashlib.sha256(output.read_bytes()).hexdigest()==original
        for index in range(7):assert_isolated(tone_row(exported,index+1,1),index)
        result={'inputSampleRate':rate,'nativeChannels':channel_count,'trackNames':expected,'initialSourceAmplitudes':rows,'tailSourceAmplitudes':late,
            'minimumTenMillisecondRms':min(minimum_windows),'programAmplitudes':program,'privacyMeasurements':silence,'decodedMkvAndMp4':True,'recordingSha256':original}
        results['cases'].append(result);print(json.dumps({'rate':rate,'audioStreams':8,'passed':True}),flush=True)
        command('source.remove',id=source)
    # A two/one-channel file must not be presented as six independent inputs.
    invalid=command('source.add',kind='media',name='Insufficient inputs',locator=str(stereo),inputChannels=6)['id']
    until(lambda:any(s['id']==invalid and s['receivedChannels']>0 for s in state()['sources']),'Actual input count')
    item=next(s for s in state()['sources'] if s['id']==invalid)
    assert item['receivedChannels']<6 and item['peak']==0 and 'Audio is silent' in item['inputStatus'],item
    results['insufficientInputsVisibleAndSilent']=True
    command('source.update',id=invalid,inputChannels=2)
    until(lambda:any(s['id']==invalid and s['inputChannels']==2 and s['peak']>.03 for s in state()['sources']),'Return to stereo')
    assert not any(c['source']==invalid and c['input']>0 for c in state()['recording']['audio_choices'])
    api('/studio/command',{'command':'record.start','path':str(report/'invalid-channel.mkv'),'audioSources':[invalid+':channel:1']},400)
    assert not (report/'invalid-channel.mkv').exists()
    results['stereoRestoredAndUnavailableRouteRejected']=True
    quit_owned();results['cleanExit']=True;results['passed']=True
except BaseException as error:
    results['error']=str(error);raise
finally:
    if target:
        try:quit_owned()
        except Exception as error:results['cleanupError']=str(error)
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    results['userShowUnchanged']=(hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    if not results['userShowUnchanged']:results['passed']=False
    (report/'six-input.json').write_text(json.dumps(results,indent=2))
    assert results['userShowUnchanged']
