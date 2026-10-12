"""Eight private VDO.Ninja guests with real WebRTC returns and mix-minus."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
import ast,sys
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
shared=Path(__file__).with_name('mixer-returns-050-e2e.py').read_text();tree=ast.parse(shared)
for node in tree.body:
    if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id in ('instrument','stats_script','levels_script') for t in node.targets):exec(ast.get_source_segment(shared,node))
freqs=[440,780,1180,1660,2120,2660,3220,3880];levels_script=levels_script.replace('[440,1660,731]',json.dumps(freqs))
colors=['#c03030','#30b040','#3040c0','#c0a030','#a030b0','#30b0b0','#d07030','#7050c0']
sender=None
def join(page,url):
    page.goto(url+'&aec=0&agc=0&denoise=0&stereo',wait_until='domcontentloaded',timeout=60000);page.wait_for_timeout(1200)
    if not page.locator('#gowebcam').is_visible():page.locator('#container-3').click()
    if page.locator('#getPermissions').is_visible():page.locator('#getPermissions').click()
    page.locator('#gowebcam').wait_for(state='visible',timeout=30000);page.wait_for_function("!document.getElementById('gowebcam').disabled",timeout=45000);page.locator('#gowebcam').click(force=True)
def signal(page,hz):
    levels=page.evaluate(levels_script);assert levels,'No returned audio track';return max(v[str(hz)] for v in levels)
try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    pages=[];guests=[]
    with sync_playwright() as pw:
        sender=pw.chromium.launch(channel='msedge',headless=True,args=['--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream','--autoplay-policy=no-user-gesture-required','--mute-audio'])
        for n,hz in enumerate(freqs):
            g=command('guest.invite',kind='guest',name='Panel '+str(n+1),returnMode='video');guests.append(g)
            page=sender.new_page();page.add_init_script(instrument)
            page.add_init_script('''(()=>{const original=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);navigator.mediaDevices.getUserMedia=async c=>{const s=await original(c);if(c.audio){s.getAudioTracks().forEach(t=>{s.removeTrack(t);t.stop()});const a=new AudioContext({sampleRate:48000}),o=a.createOscillator(),g=a.createGain(),d=a.createMediaStreamDestination();o.frequency.value=HZ;g.gain.value=.04;o.connect(g);g.connect(d);o.start();await a.resume();window.qaTone={a,o,g};s.addTrack(d.stream.getAudioTracks()[0])}return s}})();'''.replace('HZ',str(hz)))
            page.add_init_script('''(()=>{const original=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);navigator.mediaDevices.getUserMedia=async c=>{const s=await original(c);if(c.video){s.getVideoTracks().forEach(t=>{s.removeTrack(t);t.stop()});const canvas=document.createElement('canvas');canvas.width=640;canvas.height=360;const ctx=canvas.getContext('2d');let tick=0;const draw=()=>{ctx.fillStyle=COLOR;ctx.fillRect(0,0,640,360);ctx.fillStyle='white';ctx.fillRect((tick++*5)%600,300,30,30)};draw();setInterval(draw,50);s.addTrack(canvas.captureStream(20).getVideoTracks()[0])}return s}})();'''.replace('COLOR',json.dumps(colors[n])))
            join(page,g['invite']);command('guest.set',id=g['id'],admitted=True)
            until(lambda:next(s for s in state()['sources'] if s['id']==g['id'])['peak']>.01,'guest audio',45)
            pages.append(page)
            for k,g in enumerate(guests):command('layer.set',source=g['id'],x=(k%4)/4,y=(k//4)/2,w=.25,h=.5,fit='fit')
            if len(pages) in (4,6,8):
                time.sleep(4);checks=[]
                for k,p in enumerate(pages):
                    stats=p.evaluate(stats_script);assert any(s['type']=='inbound-rtp' and s['kind']=='video' and s['frames']>20 for s in stats),stats
                    own,other=signal(p,freqs[k]),signal(p,freqs[(k+1)%len(pages)])
                    assert own<-50 and other>-48,(k,own,other);checks.append({'ownDb':own,'otherDb':other})
                results[str(len(pages))+'Guests']={'mixMinus':checks,'studioPrivateMiB':sum(p.memory_info().private for p in [root,*root.children(recursive=True)] if p.is_running())/2**20}
                print('Panel passed at '+str(len(pages))+' guests',flush=True)
        frame=image('program','eight-guest-grid.png')
        palette=[tuple(int(color[i:i+2],16) for i in (1,3,5)) for color in colors];measurements=[]
        for n,color in enumerate(colors):
            expected=palette[n];actual=frame.getpixel((int((n%4+.3)*frame.width/4),int((n//4+.42)*frame.height/2)))
            # This checks guest placement/identity, not codec color accuracy.
            # Preserve actual RGB deviations instead of claiming lossless color.
            distances=[sum((a-b)**2 for a,b in zip(actual,target)) for target in palette]
            assert int(np.argmin(distances))==n,(n,actual,expected,distances)
            measurements.append({'expectedRgb':expected,'receivedRgb':actual,'maxChannelDeviation':max(abs(a-b) for a,b in zip(actual,expected))})
        results['grid']='All eight distinct WebRTC pictures occupy their assigned grid cells'
        results['gridColors']=measurements
        recorded=report/'eight-guests.mkv';command('record.start',path=str(recorded));until(lambda:state()['recording']['active'],'record');time.sleep(8)
        # Private break keeps the panel conversation active; off-screen image
        # selection should not strand another guest's return.
        command('production.set',privateBreak=True);time.sleep(3);assert signal(pages[0],freqs[1])>-48
        command('production.set',privateBreak=False);command('guest.set',id=guests[3]['id'],admitted=False);time.sleep(3)
        assert signal(pages[0],freqs[3])<-50;command('guest.set',id=guests[3]['id'],admitted=True)
        pages[-1].close();time.sleep(4);assert signal(pages[0],freqs[1])>-48
        command('record.stop');until(lambda:not state()['recording']['busy'],'record finalized')
        r=run([ffmpeg,'-v','error','-i',recorded,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=60);assert r.returncode==0 and not r.stderr,r.stderr
        results['recording']='fully decoded through private break, backstage/readmit and a peer leaving';sender.close();sender=None
    results['passed']=True
finally:
    if sender:
        try:sender.close()
        except Exception:pass
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'panel-080.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2),flush=True)
