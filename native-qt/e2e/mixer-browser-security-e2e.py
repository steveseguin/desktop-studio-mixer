"""Hostile local pages in the packaged WebView2 source, not a mock bridge.

Launch the app with WebView2's loopback-only remote debugging enabled for QA.
The production app does not enable a debugging endpoint itself.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import os, sys, ctypes, psutil, numpy as np
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright

results={}; requests=[]; root=psutil.Process(args.pid)
show=show_path
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append(self.path)
        if self.path.startswith('/download'):
            body=b'Desktop Studio private browser security fixture'
            self.send_response(200); self.send_header('Content-Type','application/octet-stream'); self.send_header('Content-Disposition','attachment; filename=desktop-studio-security-fixture.txt')
        else:
            body=b'''<!doctype html><style>body{margin:0;background:#24b46a;color:white;font:32px sans-serif}</style><div id=clock></div><button id=screen>Screen capture test</button><a id=download href="/download" download>Download test</a>
<script>setInterval(()=>clock.textContent=Date.now(),33);document.getElementById('screen').onclick=()=>navigator.mediaDevices.getDisplayMedia({video:true}).then(s=>{s.getTracks().forEach(t=>t.stop());window.screenResult='allowed'},e=>window.screenResult=e.name);</script>'''
            self.send_response(200); self.send_header('Content-Type','text/html')
        self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self,*_): pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture); threading.Thread(target=server.serve_forever,daemon=True).start()
url=f'http://127.0.0.1:{server.server_port}/'

def responsive():
    response=ctypes.c_size_t(); begin=time.perf_counter()
    assert ctypes.windll.user32.SendMessageTimeoutW(window.handle,0,0,0,2,1500,ctypes.byref(response)), 'Desktop Studio became unresponsive'
    return (time.perf_counter()-begin)*1000

def connect(pw,source_id):
    file=(Path(os.environ['LOCALAPPDATA'])/'VideoMixer/browser')/source_id/'EBWebView/DevToolsActivePort'; deadline=time.time()+30
    while time.time()<deadline:
        try:
            browser=pw.chromium.connect_over_cdp('http://127.0.0.1:'+file.read_text().splitlines()[0],timeout=1500)
            page=next(p for c in browser.contexts for p in c.pages if p.url.startswith(url));return browser,page
        except Exception: time.sleep(.3)
    raise AssertionError('QA WebView2 page not found')

window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))
enabled(control('guestProgramReturn'),False);choose_dialog(window,'canvasFormat','1280x720 / 30 fps')
try:
    add_source('Video / audio file','QA security tone',str(media_fixture()))
    add_source('Browser /','QA security browser',url); time.sleep(3)
    source_id=next(s['id'] for s in json.loads(show.read_text())['sources'] if s['name']=='QA security browser')
    with sync_playwright() as pw:
        browser,page=connect(pw,source_id)
        results['permissions']=page.evaluate('''async () => {
            const test=async fn=>{try {const value=await fn();if(value?.getTracks)value.getTracks().forEach(t=>t.stop());return 'allowed'}catch(e){return e.name}};
            return {camera:await test(()=>navigator.mediaDevices.getUserMedia({video:true})),
                    microphone:await test(()=>navigator.mediaDevices.getUserMedia({audio:true})),
                    clipboard:await test(()=>navigator.clipboard.readText()),
                    notification:await Notification.requestPermission(),
                    hostObject:await test(()=>chrome.webview.hostObjects.desktopStudio.execute('noop'))};
        }''')
        assert results['permissions']['camera']=='NotAllowedError',results['permissions']
        assert results['permissions']['microphone']=='NotAllowedError',results['permissions']
        assert results['permissions']['clipboard']!='allowed' and results['permissions']['notification']=='denied'
        assert results['permissions']['hostObject']!='allowed'
        page.locator('#screen').click();page.wait_for_function('window.screenResult !== undefined',timeout=8000)
        results['screenCapture']=page.evaluate('screenResult');assert results['screenCapture']!='allowed'
        # Source-initiated windows/downloads and OS URI handlers cannot escape the host.
        page.evaluate("window.open('/popup');const f=document.createElement('iframe');f.src='file:///C:/Windows/win.ini';document.body.append(f)")
        page.locator('#download').click();time.sleep(1)
        assert not any(path.startswith('/popup') for path in requests),requests
        assert not (Path(os.environ['USERPROFILE'])/'Downloads/desktop-studio-security-fixture.txt').exists()
        for destination in ['file:///C:/Windows/win.ini','ms-settings:display']:
            page.evaluate('(url)=>{try{location.href=url}catch(e){}}',destination);time.sleep(.4)
            assert page.url.startswith(url),page.url
        results['navigationDownloadsPopups']='file/custom schemes blocked; popup/download blocked'
        page.evaluate('''() => {
            for(const data of [null,17,'hello',[],{type:'quit'},{type:'exec',command:'calc.exe'},
                {type:'mixer-audio',rate:-1,pcm:'AAAA'},{type:'mixer-audio',rate:48000,pcm:'!bad!'},
                {type:'mixer-audio',rate:48000,pcm:'A'.repeat(1000000)}])chrome.webview.postMessage(data);
            alert('This dialog must not block Desktop Studio');
        }''')
        responsive(); assert page.evaluate('1+1')==2
        results['malformedNativeMessages']='ignored; no host command execution or blocking dialog'
        discovery=Path(os.environ.get('STUDIO_E2E_DISCOVERY',str(report/'control.json')))
        if discovery.exists():
            data=json.loads(discovery.read_text())
            # Deliberately supply no token, as an untrusted webpage would.
            outcome=page.evaluate("""async base => {try {const r=await fetch(base+'/commands',{method:'POST',body:JSON.stringify({command:'quit'})});return r.status}catch(e){return e.name}}""",data['base_url'])
            time.sleep(.5);responsive();assert root.is_running()
            assert outcome in (401,403,'TypeError'),outcome
            results['unauthenticatedLoopbackCommand']={'webResult':outcome,'mainAppSurvived':True}
        else:
            results['unauthenticatedLoopbackCommand']='not run: this launch has no control listener'
        # Flood the real WebView bridge; a separate media source supplies audio.
        path=report/'hostile-browser.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').invoke()
        deadline=time.time()+30
        while time.time()<deadline and not control('recordingStatus').window_text().startswith('Recording:'):time.sleep(.3)
        assert control('recordingStatus').window_text().startswith('Recording:')
        page.evaluate('''() => {window.flood=setInterval(()=>{for(let i=0;i<500;i++)chrome.webview.postMessage({type:'mixer-audio-error'});chrome.webview.postMessage({type:'mixer-audio',rate:48000,pcm:'A'.repeat(2000000)});},10)}''')
        latency=[];before=root.memory_info().rss
        for _ in range(40):time.sleep(.2);latency.append(responsive())
        try:page.evaluate('clearInterval(flood)')
        except Exception:pass
        assert control('studioRecord').window_text()=='Stop recording'
        results['messageFlood']={'seconds':8,'uiMaxMs':round(max(latency),2),'parentRssGrowthMiB':round((root.memory_info().rss-before)/2**20,2)}
        # Kill the entire browser host (not just Chromium's renderer).
        host=next(p for p in root.children() if '--studio-worker' in p.cmdline() and 'browser' in p.cmdline());old=host.pid
        host.kill();host.wait(timeout=8);time.sleep(4)
        browser,page=connect(pw,source_id);responsive()
        assert control('studioRecord').window_text()=='Stop recording'
        deadline=time.time()+12
        while time.time()<deadline and 'reconnecting automatically' in control('mixerStatus').window_text().lower():time.sleep(.2)
        assert 'reconnecting automatically' not in control('mixerStatus').window_text().lower(),control('mixerStatus').window_text()
        results['recoveryNotice']=control('mixerStatus').window_text()
        results['browserHostCrash']='automatically recovered; recording stayed active'
        press('studioRecord');deadline=time.time()+30
        while time.time()<deadline and 'saved' not in control('recordingStatus').window_text().lower():time.sleep(.3)
        assert 'saved' in control('recordingStatus').window_text().lower()
        ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
        decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,text=True,timeout=60)
        assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
        results['recording']='fully decoded after hostile messages and browser crash'
        window.capture_as_image().save(str(report/'browser-security.png'))
finally:
    server.shutdown();(report/'browser-security.json').write_text(json.dumps(results,indent=2))
