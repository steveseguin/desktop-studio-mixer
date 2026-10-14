"""Seeded command/show fuzz against a packaged Studio with real recording.

Rejected imports must preserve the existing show. During recording, invalid
commands and bounded valid scene/layer churn must preserve healthy media/audio.
"""
from pathlib import Path
helpers=Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0]
helpers=helpers.replace('args=parser.parse_args()',"parser.add_argument('--fixture',required=True)\nparser.add_argument('--seed',type=int,default=2601015)\nargs=parser.parse_args()")
exec(helpers)
import copy,random,base64
rng=random.Random(args.seed);root=None;receiver=None;receiver_log=None
results={'sha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),'seed':args.seed,'imports':[],'commands':[]}

def stable(s):
    result={k:s[k] for k in ('scenes','preview','program','program_layers','canvas')}
    result['sources']=[{k:source[k] for k in ('id','kind','name','gain','muted','followsScene','monitored','programAudio','channelMode','audioDelayMs','paused','loop')}
                       for source in s['sources']]
    return result

def record_import(name,data):
    file=report/(name+'.mixer.json');file.write_text(data if isinstance(data,str) else json.dumps(data),encoding='utf-8')
    before=stable(state())
    request={'command':'show.open','path':str(file)}
    try:
        reply=api('/studio/command',request,400);rejected=True
    except AssertionError as error:
        results['imports'].append({'name':name,'rejected':False,'detail':str(error)});raise
    after=stable(state());assert before==after,(name,'Rejected file changed the current show')
    results['imports'].append({'name':name,'rejected':rejected,'showPreserved':True})

try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
    command('monitor.set',enabled=False)
    healthy=command('source.add',kind='media',name='Healthy audio and video',locator=str(Path(args.fixture).resolve()))['id']
    command('layer.set',source=healthy,x=0,y=0,w=1,h=1)
    until(lambda:state()['master_peak']>.01 and state()['sources'][0]['frames']>30,'healthy source')
    base={'version':1,'sources':[],'scenes':[{'id':str(uuid.uuid4()),'name':'Imported','layers':[]}]}
    for field in ('sources','scenes'):
        for name,value in [('null',None),('object',{}),('string','broken'),('bool',False),('number',1)]:
            bad=copy.deepcopy(base);bad[field]=value;record_import(field+'-'+name,bad)
    for name,value in [('null',None),('object',{}),('string','broken'),('bool',False),('number',1)]:
        bad=copy.deepcopy(base);bad['scenes'][0]['layers']=value;record_import('layers-'+name,bad)
    for name,text in [('truncated','{"version":1,"sources":['),('array-root','[]'),('oversized',' '*2097153),
                      ('deep','['*1100+'0'+']'*1100)]:record_import(name,text)
    bad=copy.deepcopy(base);bad['scenes']*=2;record_import('duplicate-scene',bad)
    bad=copy.deepcopy(base);bad['scenes'][0]['layers']=[{'source':str(uuid.uuid4())}];record_import('dangling-layer',bad)
    for n in range(30):
        bad=copy.deepcopy(base);bad[rng.choice(('sources','scenes'))]=rng.choice([None,False,123,'x',{'not':'an array'}]);record_import('seeded-'+str(n),bad)
    saved_show=report/'valid-roundtrip.mixer.json';before=stable(state())
    command('show.save',path=str(saved_show));command('show.open',path=str(saved_show))
    until(lambda:state()['master_peak']>.01 and state()['sources'][0]['frames']>30,'valid show reloaded')
    assert stable(state())==before,'Valid show roundtrip changed its configuration'
    results['validShowRoundtrip']=True
    extra=command('source.add',kind='color',name='Layer under fuzz',locator='#114477')['id']
    scene=state()['preview']
    # Keep healthy video and audio playing while the overlay is mutated.
    command('layer.set',source=extra,x=.7,y=.7,w=.2,h=.2,opacity=.5)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    receiver_log=(report/'receiver.log').open('wb')
    receiver=subprocess.Popen([str(ffmpeg),'-v','warning','-i',f'udp://127.0.0.1:{port}?fifo_size=8192&overrun_nonfatal=1',
        '-map','0','-c','copy',str(report/'received.mkv')],stdin=subprocess.PIPE,stdout=receiver_log,stderr=receiver_log,creationflags=subprocess.CREATE_NO_WINDOW)
    command('record.start',path=str(report/'local.mkv'),audioSources=[healthy])
    command('output.configure',protocol='udp',url=f'udp://127.0.0.1:{port}',encoder='software',bitrate_kbps=4000)
    command('output.start');until(lambda:state()['output']['live'] and state()['recording']['program_active'],'both outputs',45)
    media_pids=[p.pid for p in root.children() if '--studio-worker' in p.cmdline() and 'media' in p.cmdline()]
    invalid=[{'command':'audio.set','source':healthy,'gain':-1}, {'command':'audio.set','source':healthy,'gain':1e100},
        {'command':'audio.set','source':healthy,'muted':'false'}, {'command':'audio.set','source':healthy,'audioDelayMs':501},
        {'command':'audio.set','source':healthy,'channelMode':4}, {'command':'audio.set','source':healthy,'gain':None},
        {'command':'layer.order','source':extra,'index':-1}, {'command':'layer.order','source':extra,'index':1000},
        {'command':'layer.set','source':extra,'opacity':1.001}, {'command':'layer.set','source':extra,'cropX':.9,'cropW':.5},
        {'command':'layer.set','source':extra,'w':0}, {'command':'layer.set','source':extra,'x':float('inf')},
        {'command':'canvas.set','format':1}, {'command':'show.open','path':str(show)},
        {'command':'source.update','id':healthy,'locator':str(report/'missing.mp4')},
        {'command':'source.add','kind':'browser','name':'Blocked','locator':'file:///C:/Windows/win.ini'},
        {'command':'source.add','kind':'browser','name':'Blocked','locator':'javascript:alert(1)'}]
    time.sleep(2)
    started=time.monotonic()
    for n in range(180):
        before=stable(state());bad=copy.deepcopy(rng.choice(invalid));tick=time.perf_counter()
        api('/studio/command',bad,400)
        elapsed=(time.perf_counter()-tick)*1000;after=state()
        assert stable(after)==before,(bad,'Rejected request changed state')
        assert after['recording']['program_active'] and after['output']['live']
        assert [p.pid for p in root.children() if '--studio-worker' in p.cmdline() and 'media' in p.cmdline()]==media_pids
        command('layer.set',source=extra,x=.7,y=.7,w=rng.choice((.1,.2,.3)),h=.2,opacity=rng.choice((0,.5,1)))
        command('scene.rename',id=scene,name=rng.choice(('Main','Interview \U0001f3a4','\u65e5\u672c\u8a9e','e\u0301','A'*256)))
        # Keep deliberately invalid JSON numbers inside a string in the report.
        results['commands'].append({'requestJson':json.dumps(bad),'apiMs':elapsed,'drops':after['recording']['dropped_frames']})
        time.sleep(.09)
    results['durationSeconds']=time.monotonic()-started
    picture=api('/studio/screenshot',{'target':'program','max_width':640});(report/'program.png').write_bytes(base64.b64decode(picture['data']))
    receiver.stdin.write(b'q\n');receiver.stdin.flush();assert receiver.wait(timeout=10)==0
    command('output.stop');until(lambda:not state()['output']['active'],'output stopped')
    command('record.stop');until(lambda:not state()['recording']['busy'],'recording saved')
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(children,timeout=10);assert not alive
    results['recordings']=[]
    for file in (report/'local.mkv',report/'received.mkv'):
        dec=run([ffmpeg,'-v','error','-xerror','-i',file,'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=120)
        assert dec.returncode==0 and not dec.stderr,dec.stderr
        raw=run([ffmpeg,'-v','error','-i',file,'-map','0:a:0','-ar','48000','-ac','2','-f','f32le','-'],timeout=45)
        assert raw.returncode==0
        pcm=np.frombuffer(raw.stdout,np.float32).reshape(-1,2)[48000:-48000]
        assert np.isfinite(pcm).all() and len(pcm)>48000
        windows=pcm[:len(pcm)//480*480].reshape(-1,480,2);minimum=float(np.sqrt(np.mean(windows**2,axis=1)).min());assert minimum>.005,minimum
        results['recordings'].append({'file':file.name,'cleanDecode':True,'minimum10msRms':minimum})
    results['passed']=True
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:
            if root and root.is_running():root.kill();root.wait(timeout=10)
    if receiver and receiver.poll() is None:receiver.kill();receiver.wait(timeout=10)
    if receiver_log:receiver_log.close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'stateful-fuzz.json').write_text(json.dumps(results,indent=2))
print('Passed:',results.get('passed'),'imports',len(results['imports']),'commands',len(results['commands']),flush=True)
