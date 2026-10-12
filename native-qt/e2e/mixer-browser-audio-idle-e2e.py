"""A suspended browser AudioContext must resume without restarting its source."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
playing=True;results={};root=None
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=(b'1' if playing else b'0') if self.path=='/playing' else b'''<!doctype html>
<style>body{color:white}</style>Browser audio resume
<script>const a=new AudioContext(),o=a.createOscillator(),g=a.createGain();
o.frequency.value=731;g.gain.value=.15;o.connect(g);g.connect(a.destination);o.start();a.resume();
setInterval(async()=>{let on=await(await fetch('/playing')).text();
if(on==='1'&&a.state!=='running')await a.resume();
if(on==='0'&&a.state==='running')await a.suspend()},100);</script>'''
        self.send_response(200);self.send_header('Content-Type','text/plain' if self.path=='/playing' else 'text/html')
        self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*_):pass
try:
    browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
    threading.Thread(target=browser_server.serve_forever,daemon=True).start()
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
    sid=command('source.add',kind='browser',name='Suspend and resume audio',locator=f'http://127.0.0.1:{browser_server.server_port}/')['id']
    command('audio.set',source=sid,monitored=False)
    def track():return next(s for s in state()['sources'] if s['id']==sid)
    def worker_pids():
        return [p.pid for p in root.children() if '--studio-worker' in p.cmdline() and 'browser' in p.cmdline()]
    until(lambda:track()['peak']>.05,'Initial browser audio')
    initial=worker_pids();assert len(initial)==1
    recording=report/'browser-audio-idle.mkv';command('record.start',path=str(recording))
    until(lambda:state()['recording']['status'].startswith('Recording:'),'Recording started');time.sleep(3)
    playing=False;until(lambda:track()['peak']<1e-5,'AudioContext suspended')
    frames=track()['frames'];time.sleep(15)
    assert worker_pids()==initial and track()['frames']>frames and not track()['error']
    assert track()['peak']<1e-5 and state()['recording']['active']
    results['silentFor15SecondsWithoutRestart']=True
    playing=True;until(lambda:track()['peak']>.05,'AudioContext resumed')
    assert worker_pids()==initial;results['audioResumedWithoutRestart']=True;time.sleep(4)
    command('record.stop');until(lambda:not state()['recording']['busy'],'Recording finalized')
    results['recording']=decode(recording,allow_gaps=True);results['passed']=True
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=25)
        except Exception:pass
    if browser_server:browser_server.shutdown();browser_server.server_close()
    winreg.CloseKey(settings)
    results['originalShowUnchanged']=(hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'browser-audio-idle.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
