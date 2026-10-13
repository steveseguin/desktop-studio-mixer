"""Record actual browser audio across file/MediaStream/track changes and pause."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import sys
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
os.environ['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS']='--remote-debugging-port=0 --remote-debugging-address=127.0.0.1'
root=None;results={'phases':[]}
for frequency in (440,880):
    made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i',f'sine=frequency={frequency}:sample_rate=48000','-t','4',report/f'{frequency}.wav'],timeout=20)
    assert made.returncode==0,made.stderr
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path in ('/440.wav','/880.wav'):body=(report/self.path[1:]).read_bytes();mime='audio/wav'
        else:
            mime='text/html';body=b'''<!doctype html><style>body{background:red;color:white}</style>Audio switching fixture
<script>
window.a=document.createElement('audio');a.loop=true;document.body.append(a);
window.contexts=[];
window.toneStream=frequency=>{const c=new AudioContext(),o=c.createOscillator(),g=c.createGain(),d=c.createMediaStreamDestination();o.frequency.value=frequency;g.gain.value=.15;o.connect(g);g.connect(d);o.start();c.resume();contexts.push(c);return d.stream;};
</script>'''
            if self.path=='/embedded':body+=b"<script>a.src='/880.wav';a.play()</script>"
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)));self.end_headers()
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
    def log_message(self,*_):pass
try:
    browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
    threading.Thread(target=browser_server.serve_forever,daemon=True).start()
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    sid=command('source.add',kind='browser',name='Browser audio switching',locator=f'http://127.0.0.1:{browser_server.server_port}/')['id']
    def track():return next(s for s in state()['sources'] if s['id']==sid)
    until(lambda:track()['frames']>10,'Browser picture',45)
    initial=[p.pid for p in root.children() if '--studio-worker' in p.cmdline() and 'browser' in p.cmdline()]
    with sync_playwright() as pw:
        port=int((Path(os.environ['LOCALAPPDATA'])/'VideoMixer/browser'/sid/'EBWebView/DevToolsActivePort').read_text().splitlines()[0])
        browser=pw.chromium.connect_over_cdp(f'http://127.0.0.1:{port}')
        page=next(p for c in browser.contexts for p in c.pages if f':{browser_server.server_port}/' in p.url)
        assert page.evaluate('getComputedStyle(document.body).backgroundColor')=='rgba(0, 0, 0, 0)'
        phases=[
            ('file-440',"a.src='/440.wav';await a.play()",440),
            ('file-paused','a.pause()',None),
            ('file-muted','a.muted=true;await a.play()',None),
            ('file-880',"a.muted=false;a.src='/880.wav';await a.play()",880),
            ('file-to-stream','a.srcObject=toneStream(440);await a.play()',440),
            ('stream-440','a.pause();a.remove();a=document.createElement("audio");document.body.append(a);a.srcObject=toneStream(440);await a.play()',440),
            ('stream-880','a.srcObject=toneStream(880);await a.play()',880),
            ('track-replaced','const s=a.srcObject;for(const t of s.getAudioTracks())s.removeTrack(t);s.addTrack(toneStream(440).getAudioTracks()[0]);await a.play()',440),
            ('no-tracks','for(const t of a.srcObject.getAudioTracks())a.srcObject.removeTrack(t)',None),
            ('track-added','a.srcObject.addTrack(toneStream(440).getAudioTracks()[0]);await a.play()',440),
            ('stream-to-file',"a.srcObject=null;a.src='/880.wav';a.loop=true;await a.play()",880),
            ('final-pause','a.pause()',None),
            ('page-audio-graph',"a.remove();a=new Audio('/440.wav');a.loop=true;document.body.append(a);window.pageContext=new AudioContext();window.pageGain=pageContext.createGain();pageContext.createMediaElementSource(a).connect(pageGain);pageGain.connect(pageContext.destination);await pageContext.resume();await a.play()",440),
            ('graph-disconnected','pageGain.disconnect(pageContext.destination)',None),
            ('graph-reconnected','pageGain.connect(pageContext.destination)',440),
            ('graph-closed','await pageContext.close()',None),
            ('cross-origin-frame',f"document.body.innerHTML='<iframe allow=autoplay src=http://localhost:{browser_server.server_port}/embedded></iframe>'",880),
            ('frame-removed',"document.querySelector('iframe').remove()",None),
            ('navigation',None,None),
            ('after-navigation',"a.src='/880.wav';await a.play()",880),
        ]
        for name,script,expected in phases:
            if script is None:page.reload(wait_until='domcontentloaded')
            else:page.evaluate('async()=>{'+script+'}')
            page.wait_for_timeout(1400)
            path=report/(name+'.mkv');command('record.start',path=str(path))
            until(lambda:state()['recording']['status'].startswith('Recording:'),name+' recording')
            time.sleep(3);command('record.stop');until(lambda:not state()['recording']['busy'],name+' finalized')
            decoded=run([ffmpeg,'-v','error','-i',path,'-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],timeout=30)
            assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
            pcm=run([ffmpeg,'-v','error','-i',path,'-vn','-f','f32le','-ac','1','-ar','48000','-'],timeout=30);assert pcm.returncode==0
            samples=np.frombuffer(pcm.stdout,np.float32)[48000:96000];assert len(samples)==48000
            tones={str(f):float(abs(np.sum(samples*np.exp(-2j*np.pi*f*np.arange(len(samples))/48000)))*2/len(samples)) for f in (440,880)}
            rms=float(np.sqrt(np.mean(samples*samples)))
            passed=rms<.0001 if expected is None else tones[str(expected)]>.03 and tones[str(1320-expected)]<.002
            result={'name':name,'expectedHz':expected,'amplitudes':tones,'rms':rms,'passed':passed};results['phases'].append(result)
            print(json.dumps(result),flush=True)
        assert [p.pid for p in root.children() if '--studio-worker' in p.cmdline() and 'browser' in p.cmdline()]==initial
        assert page.evaluate('getComputedStyle(document.body).backgroundColor')=='rgba(0, 0, 0, 0)'
        results['sameBrowserWorker']=True;results['transparentDefault']=True
    results['passed']=all(p['passed'] for p in results['phases'])
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    if browser_server:browser_server.shutdown();browser_server.server_close()
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'browser-audio-switching.json').write_text(json.dumps(results,indent=2))
assert results.get('passed'),results
