"""Real packaged recordings from unusual WAV files and failed source devices.

These are media-input formats, not claims about a physical audio driver's format.
Fixtures are bounded; all decoding runs after the owned application exits.
"""
from pathlib import Path
source=Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0]
source=source.replace('args=parser.parse_args()',"parser.add_argument('--fixture',required=True)\nargs=parser.parse_args()")
exec(source)
import struct,random

root=None
results={'sha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),'cases':[]}
frequencies=[997,1231,1451,73,1663,1877,2089,2293]
specs=[('pcm8-8000-mono',8000,1,8,False),('pcm16-11025-mono',11025,1,16,False),
       ('pcm24-22050-stereo',22050,2,24,False),('pcm24-44100-three',44100,3,24,False),
       ('pcm32-96000-six',96000,6,32,False),('pcm24-192000-eight',192000,8,24,False),
       ('float32-44100-stereo',44100,2,32,True),('float32-96000-six',96000,6,32,True),
       ('float64-48000-stereo',48000,2,64,True),('float32-nonfinite',48000,2,32,True)]

def chunk(name,data):return name+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')

def wav(name,rate,channels,bits,floating):
    t=np.arange(rate*4,dtype=np.float64)/rate
    values=np.stack([.07*np.sin(2*np.pi*frequencies[c]*t) for c in range(channels)],axis=1)
    if name.endswith('nonfinite'):
        values[::511,0]=np.nan;values[::733,1]=np.inf;values[::947,1]=-np.inf
    if floating:data=values.astype('<f4' if bits==32 else '<f8').tobytes()
    elif bits==8:data=np.round(values*127+128).astype('u1').tobytes()
    elif bits==24:
        packed=np.round(values*(2**23-1)).astype('<i4').view('u1').reshape(-1,4)
        data=packed[:,:3].tobytes()
    else:data=np.round(values*(2**(bits-1)-1)).astype('<i2' if bits==16 else '<i4').tobytes()
    tag=3 if floating else 1;block=channels*bits//8
    if channels>2:
        mask={3:7,6:0x3f,8:0x63f}[channels]
        subtype=struct.pack('<IHH8s',tag,0,0x10,b'\x80\x00\x00\xaa\x00\x38\x9b\x71')
        fmt=struct.pack('<HHIIHHHHI',0xfffe,channels,rate,rate*block,block,bits,22,bits,mask)+subtype
    else:fmt=struct.pack('<HHIIHH',tag,channels,rate,rate*block,block,bits)
    body=b'WAVE'+chunk(b'fmt ',fmt)+(chunk(b'fact',struct.pack('<I',len(values))) if floating else b'')+chunk(b'data',data)
    path=report/(name+'.wav');path.write_bytes(b'RIFF'+struct.pack('<I',len(body))+body);return path

def find(sid):return next(s for s in state()['sources'] if s['id']==sid)

def observe(seconds):
    end=time.monotonic()+seconds;latencies=[]
    while time.monotonic()<end:
        tick=time.perf_counter();s=state();latencies.append((time.perf_counter()-tick)*1000)
        assert s['recording']['program_active'],s['recording']
        assert next(x for x in s['sources'] if x['id']==healthy)['frames']>0
        time.sleep(.15)
    return max(latencies)

try:
    fixtures=[(spec,wav(*spec)) for spec in specs]
    malformed=[]
    good=fixtures[1][1].read_bytes()
    for name,data in [('empty',b''),('truncated-header',good[:30]),('truncated-pcm',good[:100]),
                      ('bad-rate',good[:24]+struct.pack('<I',0)+good[28:]),
                      ('bad-channels',good[:22]+struct.pack('<H',0)+good[24:]),
                      ('random',random.Random(2601014).randbytes(8192))]:
        path=report/(name+'.wav');path.write_bytes(data);malformed.append(path)
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
    command('monitor.set',enabled=False)
    healthy=command('source.add',kind='media',name='Healthy stereo and video',locator=str(Path(args.fixture).resolve()))['id']
    command('layer.set',source=healthy,x=0,y=0,w=1,h=1)
    until(lambda:find(healthy)['frames']>30 and state()['master_peak']>.01,'healthy playback')
    for spec,path in fixtures:
        name,rate,channels,bits,floating=spec
        sid=command('source.add',kind='media',name=name,locator=str(path))['id']
        command('audio.set',source=sid,gain=.5)
        deadline=time.monotonic()+6
        while time.monotonic()<deadline:
            item=find(sid)
            if item['error'] or item['peak']>.001:break
            time.sleep(.15)
        recording=report/(name+'.mkv');command('record.start',path=str(recording),audioSources=[sid])
        until(lambda:state()['recording']['program_active'],'recording')
        row={'name':name,'rate':rate,'channels':channels,'bits':bits,'floating':floating,'source':find(sid),
             'maxApiMs':observe(4.5),'privateMiB':root.memory_info().private/2**20,'file':recording.name}
        command('record.stop');until(lambda:not state()['recording']['busy'],'recording finalized')
        command('source.remove',id=sid);results['cases'].append(row)
        print(name,'error='+row['source']['error'],'peak='+str(row['source']['peak']),flush=True)
    path=report/'bad-inputs.mkv';command('record.start',path=str(path))
    until(lambda:state()['recording']['program_active'],'fault recording')
    results['badInputs']=[]
    for kind,locator in [('media',str(p)) for p in malformed]+[
            ('camera','qa-camera-not-present-'+str(uuid.uuid4())),('microphone','qa-microphone-not-present-'+str(uuid.uuid4())),
            ('window','18446744073709551615')]:
        sid=command('source.add',kind=kind,name='Unavailable '+kind,locator=locator)['id']
        before=find(healthy)['frames'];latency=observe(3)
        results['badInputs'].append({'kind':kind,'name':Path(locator).name,'source':find(sid),'maxApiMs':latency})
        command('source.remove',id=sid)
        assert find(healthy)['frames']>before,'Healthy source stopped'
    command('record.stop');until(lambda:not state()['recording']['busy'],'fault recording saved')
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(children,timeout=10);assert not alive
    results['decoded']=[]
    for file in report.glob('*.mkv'):
        decoded=run([ffmpeg,'-v','error','-xerror','-i',file,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=120)
        assert decoded.returncode==0 and not decoded.stderr,(file.name,decoded.stderr)
        raw=run([ffmpeg,'-v','error','-i',file,'-map','0:a:0','-ar','48000','-ac','2','-f','f32le','-'],timeout=30)
        assert raw.returncode==0
        pcm=np.frombuffer(raw.stdout,np.float32).reshape(-1,2)[48000:-48000]
        assert len(pcm)>48000 and np.isfinite(pcm).all(),file.name
        windows=pcm[:len(pcm)//480*480].reshape(-1,480,2)
        minimum=float(np.sqrt(np.mean(windows**2,axis=1)).min())
        results['decoded'].append({'file':file.name,'clean':True,'minimum10msRms':minimum})
        assert minimum>.005,(file.name,'Healthy audio interrupted',minimum)
        row=next((r for r in results['cases'] if r['file']==file.name),None)
        if not row:continue
        raw=run([ffmpeg,'-v','error','-i',file,'-map','0:a:1','-ar','48000','-ac','2','-f','f32le','-'],timeout=30)
        assert raw.returncode==0
        pcm=np.frombuffer(raw.stdout,np.float32).reshape(-1,2)[48000:96000]
        assert len(pcm)==48000 and np.isfinite(pcm).all(),file.name
        row['tones']=[float(np.abs(np.sum(pcm*np.exp(-2j*np.pi*hz*np.arange(len(pcm))/48000)[:,None],axis=0)).max()*2/len(pcm))
                      for hz in frequencies[:row['channels']]]
        row['supported']=not row['source']['error'] and all(v>.002 for n,v in enumerate(row['tones']) if n!=3)
        row['gracefullyRejected']=bool(row['source']['error'])
    results['passed']=all(r['supported'] or (r['bits']==64 and r['gracefullyRejected']) for r in results['cases'])
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:
            if root and root.is_running():root.kill();root.wait(timeout=10)
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'audio-formats.json').write_text(json.dumps(results,indent=2))
print('Passed:',results.get('passed'),flush=True)
assert results.get('passed'),[(r['name'],r.get('tones'),r['source']['error']) for r in results['cases'] if not r.get('supported')]
