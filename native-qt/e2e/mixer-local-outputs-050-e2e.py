"""Verify Program through separate Spout and real Edge camera consumers."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import sys
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
results={}
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
choose_dialog(window,'canvasFormat','1280x720 / 30 fps')
remove_sources(('QA ',))
clip=media_fixture()
add_source('Video / audio file','QA local output',str(clip));time.sleep(2)
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'<!doctype html><title>Private camera receiver</title><video id=v muted autoplay playsinline></video>'
        self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body)
    def log_message(self,*_):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    enabled(control('spoutOutput'),True);time.sleep(2)
    probe=subprocess.run([str(Path('build-mixer/qa/studio_spout_output_probe.exe').resolve())],capture_output=True,text=True,timeout=22)
    assert probe.returncode==0,probe.stdout+probe.stderr
    results['spout']=json.loads(next(line for line in probe.stdout.splitlines() if line.startswith('{')))
    enabled(control('spoutOutput'),False)
    enabled(control('virtualCamera'),True);time.sleep(2)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(channel='msedge',headless=os.environ.get('STUDIO_E2E_POINTER')!='1',args=['--autoplay-policy=no-user-gesture-required','--use-fake-ui-for-media-stream'])
        context=browser.new_context(permissions=['camera']);page=context.new_page()
        page.goto(f'http://127.0.0.1:{server.server_port}/')
        devices=page.evaluate('async()=>(await navigator.mediaDevices.enumerateDevices()).filter(d=>d.kind==="videoinput").map(d=>({id:d.deviceId,label:d.label}))')
        device=next(d for d in devices if d['label']=='Desktop Studio Virtual Camera')
        result=page.evaluate('''async id=>{const stream=await navigator.mediaDevices.getUserMedia({video:{deviceId:{exact:id},width:{ideal:1280},height:{ideal:720},frameRate:{ideal:30}},audio:false});v.srcObject=stream;await v.play();
            const canvas=document.createElement('canvas');canvas.width=64;canvas.height=36;const c=canvas.getContext('2d',{willReadFrequently:true});let frames=0;const hashes=new Set();
            await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error('Camera frames timed out')),15000);function frame(){c.drawImage(v,0,0,64,36);let hash=0;for(const x of c.getImageData(0,0,64,36).data)hash=(hash*31+x)|0;hashes.add(hash);if(++frames>=45){clearTimeout(timeout);resolve();}else v.requestVideoFrameCallback(frame);}v.requestVideoFrameCallback(frame);});
            const settings=stream.getVideoTracks()[0].getSettings();stream.getTracks().forEach(t=>t.stop());return {frames,distinctFrames:hashes.size,width:settings.width,height:settings.height,frameRate:settings.frameRate};}''',device['id'])
        assert result['frames']==45 and result['distinctFrames']>20 and result['width']==1280 and result['height']==720,result
        page.screenshot(path=str(report/'edge-camera.png'));results['edge_camera']=result;browser.close()
finally:
    enabled(control('spoutOutput'),False);enabled(control('virtualCamera'),False);press('unregisterCamera');server.shutdown()
    (report/'local-outputs.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
