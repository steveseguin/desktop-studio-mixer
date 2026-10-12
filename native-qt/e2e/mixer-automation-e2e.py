"""Packaged CLI/API/MCP + actual windowless composition, recording and publishing.

Uses a separate show and local receivers; never edits the user's show. Requires
the existing desktop QA Python environment (Pillow, numpy, psutil, pywinauto).
"""
import argparse, ctypes, hashlib, io, json, os, socket, subprocess, time, urllib.request, urllib.error, uuid, winreg, http.server, threading
from pathlib import Path
import numpy as np
import psutil
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument('--report',required=True)
parser.add_argument('--package',default='dist/desktop-studio-portable')
parser.add_argument('--mediamtx',default=os.environ.get('MEDIAMTX_PATH'))
args=parser.parse_args()
report=Path(args.report).resolve();report.mkdir(parents=True,exist_ok=True)
package=Path(args.package).resolve();cli=package/'tools/mcp/studio-cli.mjs'
ffmpeg=package/'ffmpeg/bin/ffmpeg.exe'
results={};owned=[];target=None;browser_server=None
show=report/'automation.mixer.json'
red,green=str(uuid.uuid4()),str(uuid.uuid4())
show.write_text(json.dumps({'version':1,'sources':[],'scenes':[
    {'id':red,'name':'Red','background':'#aa1122','layers':[]},
    {'id':green,'name':'Green','background':'#11aa22','layers':[]}],'canvasFormat':0,'renderer':1}))
settings=winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\VDO.Ninja\Video Mixer\ui')
saved={}
for key in ('theme','language','workspaceLayout'):
    try:saved[key]=winreg.QueryValueEx(settings,key)
    except FileNotFoundError:saved[key]=None
user_show=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/studio.mixer.json'
original_hash=hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None
def run(argv,**kwargs):return subprocess.run([str(a) for a in argv],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,**kwargs)
def cli_call(*args,stdin=None,fail=False):
    argv=['node',cli]+(['--discovery',target['discovery']] if target else [])+list(args)
    r=run(argv,input=stdin,text=True,encoding='utf-8',timeout=55)
    if fail:assert r.returncode!=0,(r.stdout,r.stderr);return
    assert r.returncode==0,(r.stdout,r.stderr)
    return json.loads(r.stdout)
def api(route,body=None,expected=200,headers=None,auth=True):
    d=json.loads(Path(target['discovery']).read_text())
    h={'Content-Type':'application/json'}
    if auth:h['Authorization']='Bearer '+d['token']
    h.update(headers or {})
    req=urllib.request.Request(d['base_url']+route,data=json.dumps(body).encode() if body is not None else None,headers=h)
    try:
        with urllib.request.urlopen(req,timeout=15) as response:status=response.status;data=json.load(response)
    except urllib.error.HTTPError as error:status=error.code;data=json.load(error)
    assert status==expected,(route,status,data)
    return data
def command(operation,**kwargs):return api('/studio/command',{'command':operation,**kwargs})
def state():return api('/studio/state')
def until(check,label,seconds=30):
    end=time.time()+seconds
    while time.time()<end:
        value=check()
        if value:return value
        time.sleep(.15)
    raise AssertionError('Timed out: '+label+' '+json.dumps(state()))
def image(kind,name):
    time.sleep(.22)
    path=report/name;cli_call('screenshot',kind,'--out',str(path),'--width','640')
    return Image.open(path).convert('RGB')
def decode(path,allow_gaps=False):
    r=run([ffmpeg,'-v','error','-i',path,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=60)
    assert r.returncode==0 and not r.stderr,r.stderr.decode(errors='replace')
    raw=run([ffmpeg,'-v','error','-i',path,'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1'],timeout=30)
    assert raw.returncode==0
    all_samples=np.frombuffer(raw.stdout,dtype=np.float32)
    samples=all_samples[48000:96000]
    assert len(samples)==48000
    tone=float(abs(np.sum(samples*np.exp(-2j*np.pi*731*np.arange(len(samples))/48000)))*2/len(samples))
    assert tone>.015,tone
    windows=[float(np.sqrt(np.mean(all_samples[n:n+24000]**2))) for n in range(24000,len(all_samples)-48000,24000)]
    if not allow_gaps:assert windows and min(windows)>.005,(path,windows)
    return {'file':path.name,'bytes':path.stat().st_size,'tone731Hz':tone,'minimumHalfSecondRms':min(windows),'fullDecode':'clean'}
def visible_windows(pid):
    found=[]
    callback=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
    @callback
    def visit(hwnd,param):
        proc=ctypes.c_ulong();ctypes.windll.user32.GetWindowThreadProcessId(hwnd,ctypes.byref(proc))
        if proc.value==pid and ctypes.windll.user32.IsWindowVisible(hwnd):found.append(int(hwnd))
        return True
    ctypes.windll.user32.EnumWindows(visit,0);return found
try:
    target=cli_call('launch','--headless','--show',str(show));owned.append(psutil.Process(target['pid']))
    assert state()['headless'];command('workspace.set',mode='single')
    assert not visible_windows(target['pid'])
    assert image('program','red.png').getpixel((320,180))==(170,17,34)
    command('workspace.set',mode='studio');command('scene.select',id=green)
    assert image('preview','preview-green.png').getpixel((320,180))==(17,170,34)
    assert image('program','program-red.png').getpixel((320,180))==(170,17,34)
    command('scene.take',transition='fade');time.sleep(.6)
    assert image('program','program-green.png').getpixel((320,180))==(17,170,34)
    image('window','headless-window.png');assert not visible_windows(target['pid'])
    results['windowlessScreenshots']={'program':True,'preview':True,'window':True,'visibleMainWindows':0}
    schema=cli_call('schema');assert len(schema['commands'])>=24
    api('/studio/screenshot',{'target':'program'},401,auth=False)
    api('/studio/command',{'command':'scene.add','name':'forbidden'},403,headers={'Origin':'https://malicious.invalid'})
    api('/studio/command',{'command':'scene.add','name':'bad','typo':1},400)
    api('/studio/command',{'command':'workspace.set','mode':False},400)
    api('/studio/command',{'command':'source.add','name':'unsafe','kind':'browser','locator':'file:///C:/Windows/win.ini'},400)
    cli_call('command','--stdin',stdin='{"command":"scene.select","id":"missing"}',fail=True)
    cli_call('screenshot','program','--out',str(report/'red.png'),fail=True)
    assert len(state()['scenes'])==2
    results['security']={'unauthenticatedScreenshot':401,'browserOriginWithToken':403,'invalidFieldsAndTypesRejected':True,'fileBrowserBlocked':True,'screenshotOverwriteBlocked':True}
    command('workspace.set',mode='single')
    fixture=report/'source.mp4'
    made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=640x360:r=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','4','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',fixture],timeout=30)
    assert made.returncode==0,made.stderr
    source=command('source.add',kind='media',name='Automation tone',locator=str(fixture))['id']
    until(lambda:any(s['id']==source and s['frames']>20 for s in state()['sources']),'media frames')
    command('audio.set',source=source,gain=.8,muted=True,noiseGate=True)
    track=next(s for s in state()['sources'] if s['id']==source);assert track['muted'] and track['noiseGate'] and abs(track['gain']-.8)<.001
    command('audio.set',source=source,gain=1,muted=False,noiseGate=False)
    command('layer.set',source=source,x=.1,y=.1,w=.8,h=.8)
    command('layer.set',source=source,locked=True)
    api('/studio/command',{'command':'layer.set','source':source,'x':.2},400)
    command('layer.set',source=source,locked=False,x=0,y=0,w=1,h=1)
    command('media.set',id=source,paused=True);assert next(s for s in state()['sources'] if s['id']==source)['paused']
    command('media.set',id=source,paused=False,restart=True)
    recording=report/'headless-recording.mkv'
    command('record.start',path=str(recording));until(lambda:state()['recording']['status'].startswith('Recording:'),'recording')
    api('/studio/command',{'command':'record.start','path':str(report/'second.mkv')},400)
    api('/studio/command',{'command':'canvas.set','format':1},400)
    # Real SRT caller output, consumed and decoded locally while recording runs.
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reserve:reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
    received=report/'srt.mkv';log=(report/'srt.log').open('wb')
    receiver=subprocess.Popen([str(ffmpeg),'-hide_banner','-y','-i',f'srt://127.0.0.1:{port}?mode=listener&latency=120000','-t','7','-map','0','-c','copy',str(received)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    owned.append(psutil.Process(receiver.pid));time.sleep(.5)
    command('output.configure',protocol='srt',url=f'srt://127.0.0.1:{port}',encoder='auto',bitrate_kbps=6000)
    command('output.start');until(lambda:state()['output']['live'],'SRT publishing')
    api('/studio/command',{'command':'output.configure','protocol':'whip','url':'http://127.0.0.1:1/whip'},400)
    receiver.wait(timeout=35);assert receiver.returncode==0;log.close()
    command('output.stop');until(lambda:not state()['output']['live'] and not state()['output']['stopping'],'SRT stopped')
    assert state()['recording']['active']
    command('record.stop');until(lambda:not state()['recording']['busy'],'recording finalized')
    results['outputs']=[decode(recording),decode(received)]
    if args.mediamtx:
        def free_port():
            with socket.socket() as reserve:reserve.bind(('127.0.0.1',0));return reserve.getsockname()[1]
        rtsp,rtmp,whip,udp,srt=[free_port() for _ in range(5)]
        config=report/'mediamtx.yml'
        config.write_text(f'''logLevel: info
rtspAddress: 127.0.0.1:{rtsp}
rtspTransports: [tcp]
rtmpAddress: 127.0.0.1:{rtmp}
webrtcAddress: 127.0.0.1:{whip}
webrtcLocalUDPAddress: 127.0.0.1:{udp}
webrtcIPsFromInterfaces: false
webrtcAdditionalHosts: [127.0.0.1]
webrtcICEServers2: []
srtAddress: 127.0.0.1:{srt}
hls: false
moq: false
paths:
  all_others:
''')
        log=(report/'mediamtx.log').open('wb')
        mtx=subprocess.Popen([args.mediamtx,str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        owned.append(psutil.Process(mtx.pid));time.sleep(.7)
        for protocol,url in [('rtmp',f'rtmp://127.0.0.1:{rtmp}/qa-rtmp'),('whip',f'http://127.0.0.1:{whip}/qa-whip/whip')]:
            simultaneous=report/(protocol+'-independent.mkv');command('record.start',path=str(simultaneous))
            command('output.configure',protocol=protocol,url=url,encoder='auto',bitrate_kbps=6000)
            command('output.start');until(lambda:state()['output']['live'],protocol+' live')
            received=report/(protocol+'-received.mkv')
            for attempt in range(10):
                rx=run([ffmpeg,'-v','error','-y','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/qa-{protocol}','-t','6','-map','0','-c','copy',received],timeout=35)
                if rx.returncode==0:break
                time.sleep(.4)
            assert rx.returncode==0,rx.stderr
            command('output.stop');until(lambda:not state()['output']['live'] and not state()['output']['stopping'],protocol+' stopped')
            assert state()['recording']['active']
            command('record.stop');until(lambda:not state()['recording']['busy'],'recording finalized')
            results['outputs'].extend([decode(received),decode(simultaneous)])
        mtx.terminate();mtx.wait(timeout=10);log.close()
    class BrowserFixture(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path=='/clip.mp4':data=fixture.read_bytes();mime='video/mp4'
            else:
                data=b'<!doctype html><body style="margin:0;background:red"><video style="width:320px;height:180px" src="/clip.mp4" autoplay loop playsinline></video>'
                mime='text/html'
            self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
    browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),BrowserFixture)
    threading.Thread(target=browser_server.serve_forever,daemon=True).start()
    command('layer.set',source=source,visible=False)
    browser_source=command('source.add',kind='browser',name='Windowless browser',locator=f'http://127.0.0.1:{browser_server.server_port}/')['id']
    def browser_track():return next(s for s in state()['sources'] if s['id']==browser_source)
    until(lambda:browser_track()['frames']>20 and browser_track()['peak']>.01,'browser video and audio',45)
    assert browser_track()['transparent'] and not browser_track()['muted']
    assert image('program','browser-transparent.png').getpixel((600,330))==(17,170,34)
    browser_recording=report/'headless-browser.mkv';command('record.start',path=str(browser_recording))
    until(lambda:state()['recording']['status'].startswith('Recording:'),'browser recording');time.sleep(2)
    root=psutil.Process(target['pid'])
    victim=next(p for p in root.children() if '--studio-worker' in p.cmdline() and 'browser' in p.cmdline())
    victim.kill();victim.wait(timeout=10)
    until(lambda:any('--studio-worker' in p.cmdline() and 'browser' in p.cmdline() and p.pid!=victim.pid for p in root.children()),'browser restart',25)
    until(lambda:browser_track()['peak']>.01,'browser audio recovered',25)
    times=[];cpu={};private=[];start=time.perf_counter()
    for _ in range(30):
        before=time.perf_counter();state();times.append((time.perf_counter()-before)*1000)
        total=0
        for p in [root,*root.children(recursive=True)]:
            try:
                ct=p.cpu_times();t=ct.user+ct.system
                if p.pid in cpu:cpu[p.pid][1]=t
                else:cpu[p.pid]=[t,t]
                total+=p.memory_info().private
            except psutil.NoSuchProcess:pass
        private.append(total/1048576);time.sleep(.2)
    elapsed=time.perf_counter()-start
    results['profile']={'cpuPercentOneLogicalCore':sum(b-a for a,b in cpu.values())/elapsed*100,'privateMiBMin':min(private),'privateMiBMax':max(private),'stateLatencyP95Ms':float(np.percentile(times,95)),'seconds':elapsed}
    command('record.stop');until(lambda:not state()['recording']['busy'],'browser recording finalized')
    results['browser']={'transparentDefault':True,'audioDefault':True,'crashRecovery':True,'recording':decode(browser_recording,allow_gaps=True)}
    command('source.remove',id=browser_source);command('layer.set',source=source,visible=True)
    browser_server.shutdown();browser_server.server_close();browser_server=None
    saved_show=report/'saved.mixer.json';command('show.save',path=str(saved_show));command('show.open',path=str(saved_show))
    assert any(s['id']==source for s in state()['sources'])
    result=run(['node',Path(__file__).resolve().parents[1]/'tools/mcp/studio-packaged-e2e.mjs',package,target['discovery'],report],text=True,encoding='utf-8',timeout=75)
    assert result.returncode==0,result.stdout+result.stderr
    assert state()['headless'],'MCP disconnect must not quit the app'
    results['mcp']=json.loads((report/'mcp.json').read_text())
    command('preferences.set',theme='rose');rose=image('window','rose.png')
    assert state()['preferences']['theme']=='rose'
    for theme in ('graphite','neon','sand','original'):
        command('preferences.set',theme=theme);image('window',theme+'.png')
    command('workspace.set',layout='podcast');assert state()['layout']=='podcast'
    command('preferences.set',language='es')
    results['appearance']={'themes':['original','rose','graphite','neon','sand'],'podcastLayout':True,'languageNextLaunch':'es'}
    cli_call('quit');until(lambda:not psutil.pid_exists(target['pid']),'app exit')
    target=None
    # Open a real visible window for accessibility and language verification.
    target=cli_call('launch','--gui','--show',str(saved_show));owned.append(psutil.Process(target['pid']))
    from pywinauto import Application
    from pywinauto.keyboard import send_keys
    window=Application(backend='uia').connect(process=target['pid']).window(title='Desktop Studio - Powered by VDO.Ninja')
    window.wait('visible',timeout=20);window.set_focus()
    elements=window.descendants()
    titles=[e.window_text() for e in elements]
    assert any('Idioma' in t for t in titles),titles[:30]
    def named(suffix):return next(e for e in window.descendants() if e.element_info.automation_id.endswith('.'+suffix))
    assert named('addScene').window_text()=='Añadir escena'
    assert named('removeScene').window_text()=='Eliminar escena'
    assert named('layerX').element_info.name=='Posición horizontal de la capa'
    muted=named('audio_mute_'+source);assert 'Automation tone' in muted.window_text()
    assert named('sceneList').is_enabled()
    # Qt's list-item UIA SetFocus selects an item without focusing its view.
    # Exercise the actual keyboard Tab path instead of that automation quirk.
    from pywinauto.uia_defines import IUIA
    for _ in range(100):
        send_keys('{TAB}',pause=.02)
        focused=IUIA().iuia.GetFocusedElement()
        if focused.CurrentAutomationId.endswith('.sceneList'):break
    else:raise AssertionError('Scenes cannot be reached with Tab')
    send_keys('{HOME}');time.sleep(.3)
    assert state()['preview']==red
    send_keys('{DOWN}');time.sleep(.3);assert state()['preview']==green
    # Program stays live when changing only visual layout.
    before=state()['program']
    send_keys('%p');time.sleep(.2)
    send_keys('es',pause=.2);time.sleep(.3)
    assert state()['layout']=='simple' and state()['program']==before
    image('window','spanish-accessible.png')
    results['accessibility']={'uiaNames':True,'perTrackLabels':True,'keyboardSceneSelection':True,'spanishMenus':True,'layoutMenu':True}
    command('preferences.set',language='en',theme='original')
finally:
    if browser_server:browser_server.shutdown();browser_server.server_close()
    if target:
        try:cli_call('quit')
        except Exception:pass
    for process in owned:
        try:process.wait(timeout=20)
        except psutil.NoSuchProcess:pass
        except psutil.TimeoutExpired:
            for child in process.children(recursive=True):
                try:child.kill()
                except psutil.NoSuchProcess:pass
            process.kill();process.wait(timeout=10)
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    results['originalShowUnchanged']=True
    (report/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results,indent=2))
