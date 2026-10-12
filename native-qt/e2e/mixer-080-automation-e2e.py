"""Real packaged headless production, browser audio paths, viewport, and app-kill recovery."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
from urllib.parse import urlparse,parse_qs
servers=[]
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed=urlparse(self.path);query=parse_qs(parsed.query)
        if parsed.path=='/tone.wav':
            body=(report/'tone.wav').read_bytes();mime='audio/wav'
        else:
            port=self.server.server_port;mode=query.get('mode',['oscillator'])[0]
            if mode=='worklet':
                script="""const a=new AudioContext();await a.audioWorklet.addModule(URL.createObjectURL(new Blob([`class Tone extends AudioWorkletProcessor {process(i,o){for(let c of o[0])for(let n=0;n<c.length;n++)c[n]=.12*Math.sin(2*Math.PI*731*(currentFrame+n)/sampleRate);return true}}registerProcessor('tone',Tone)`],{type:'text/javascript'})));new AudioWorkletNode(a,'tone').connect(a.destination);await a.resume();"""
            elif mode in ('element','cors','nocors'):
                url=f'http://'+('127.0.0.1' if mode=='element' else 'localhost')+f':{port}/tone.wav'+('?cors=1' if mode=='cors' else '')
                script=f"const e=document.createElement('audio');"+("e.crossOrigin='anonymous';" if mode=='cors' else '')+f"e.src={json.dumps(url)};e.loop=true;document.body.append(e);await e.play();"
            elif mode=='iframe':script=f"const f=document.createElement('iframe');f.src='http://localhost:{port}/?mode=oscillator';document.body.append(f);"
            else:script="const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();o.frequency.value=731;g.gain.value=.12;o.connect(g);g.connect(a.destination);o.start();await a.resume();"
            body=("<!doctype html><style>body{margin:0;background:red}#color{position:fixed;inset:0;background:#2244cc}@media(max-aspect-ratio:1/1){#color{background:#22cc44}}iframe{position:absolute;width:2px;height:2px;opacity:0}</style><div id=color></div><script>(async()=>{"+script+"})().catch(e=>document.title=e.message);</script>").encode();mime='text/html'
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)))
        if 'cors' in query:self.send_header('Access-Control-Allow-Origin','*')
        self.end_headers();self.wfile.write(body)
    def log_message(self,*args):pass
def tones(path):
    r=run([ffmpeg,'-v','error','-i',path,'-vn','-ar','48000','-ac','1','-f','f32le','-'],timeout=40);assert r.returncode==0,r.stderr
    pcm=np.frombuffer(r.stdout,np.float32)[48000:-24000]
    blocks=pcm[:len(pcm)//4800*4800].reshape(-1,4800)
    return float(np.median(abs(blocks@np.exp(-2j*np.pi*731*np.arange(4800)/48000))*2/4800))
try:
    made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','3',report/'tone.wav'],timeout=30);assert made.returncode==0
    browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=browser_server.serve_forever,daemon=True).start()
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    command('canvas.set',format=5);command('production.set',microphonesMuted=True,privateTalkback=True)
    assert state()['microphones_muted'] and state()['private_talkback'];command('production.set',microphonesMuted=False,privateTalkback=False)
    results['browserAudio']={}
    for mode in ('oscillator','worklet','element','cors','iframe','nocors'):
        sid=command('source.add',kind='browser',name='QA '+mode,locator=f'http://127.0.0.1:{browser_server.server_port}/?mode={mode}',browserWidth=720,browserHeight=1280,transparent=True)['id']
        until(lambda:next(s for s in state()['sources'] if s['id']==sid)['frames']>20,'browser frames',45)
        png=image('program',mode+'.png')
        for x,y in ((.1,.1),(.5,.5),(.9,.9)):
            assert max(abs(a-b) for a,b in zip(png.getpixel((int(png.width*x),int(png.height*y))),(34,204,68)))<5,'Browser viewport was clipped or letterboxed'
        path=report/(mode+'.mkv');command('record.start',path=str(path));until(lambda:state()['recording']['status'].startswith('Recording:'),'record');time.sleep(4)
        command('record.stop');until(lambda:not state()['recording']['busy'],'finalize');amp=tones(path)
        results['browserAudio'][mode]={'amplitude731Hz':amp,'portraitViewport':'green CSS portrait layout'}
        if mode!='nocors':assert amp>.03,(mode,amp)
        else:
            warning=next(s for s in state()['sources'] if s['id']==sid)['error'];assert 'CORS' in warning,warning
            results['browserAudio'][mode]['warning']=warning
        if mode=='oscillator':
            command('source.update',id=sid,browserWidth=1280,browserHeight=720,transparent=False);time.sleep(5)
            png=image('program','landscape-browser.png');assert max(abs(a-b) for a,b in zip(png.getpixel((png.width//2,png.height//2)),(34,68,204)))<6
            out=report/'export-api.mp4';command('record.export',input=str(path),path=str(out));until(lambda:not state()['recording']['exporting'],'MP4 export')
            assert state()['recording']['export_status'].startswith('MP4 saved:') and tones(out)>.03
            before=hashlib.sha256(out.read_bytes()).hexdigest();api('/studio/command',{'command':'record.export','input':str(path),'path':str(out)},400)
            assert hashlib.sha256(out.read_bytes()).hexdigest()==before;results['mp4Export']='decoded audio; overwrite refused without changing existing file'
        command('scene.select',id=green);command('source.remove',id=sid);command('scene.select',id=red)
    # API invitations carry secrets only in the explicit response, not ordinary state.
    guests=[]
    for kind in ('guest','wireless'):
        g=command('guest.invite',kind=kind,name='API '+kind,programAudio=False,returnMode='none');guests.append(g['id'])
        assert urlparse(g['invite']).hostname=='vdo.ninja';assert not next(s for s in state()['sources'] if s['id']==g['id'])['admitted']
        command('guest.set',id=g['id'],admitted=True);assert next(s for s in state()['sources'] if s['id']==g['id'])['admitted']
        command('guest.set',id=g['id'],admitted=False,returnMode='audio');assert 'noaudio' not in parse_qs(urlparse(command('guest.set',id=g['id'])['invite']).query,keep_blank_values=True)
        assert g['invite'] not in json.dumps(state())
    command('production.set',guestReturns=False)
    for sid in guests:command('source.remove',id=sid)
    results['guestAutomation']='create/admit/backstage/return-mode commands passed; credentials absent from state'
    # Kill only our owned main process, verify worker cleanup and useful MKV recovery.
    sid=command('source.add',kind='media',name='Crash recovery tone',locator=str(report/'tone.wav'))['id'];command('audio.set',source=sid,channelMode=2,programAudio=True)
    interrupted=report/'interrupted.mkv';command('record.start',path=str(interrupted));until(lambda:state()['recording']['active'],'record');time.sleep(8)
    descendants=root.children(recursive=True);root.kill();root.wait(timeout=20)
    deadline=time.time()+15
    while any(p.is_running() for p in descendants) and time.time()<deadline:time.sleep(.2)
    assert not any(p.is_running() for p in descendants),'Workers survived main-app kill'
    result=run([ffmpeg,'-v','error','-i',interrupted,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=45)
    results['mainAppCrash']={'recordingBytes':interrupted.stat().st_size,'decodeExitCode':result.returncode,'decoderMessages':result.stderr.decode(errors='replace')[-800:],'tone731Hz':tones(interrupted),'workersRemaining':0}
    assert result.returncode==0 and not result.stderr and results['mainAppCrash']['tone731Hz']>.03
    target=None;target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    restored=next(s for s in state()['sources'] if s['id']==sid);assert restored['channelMode']==2 and restored['programAudio']
    assert not state()['recording']['active'] and not state()['output']['active'];results['relaunch']='autosave and routing retained; no automatic recording/publishing'
    results['passed']=True
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    if browser_server:browser_server.shutdown();browser_server.server_close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'automation-080.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2),flush=True)
