"""Measure an actual WebRTC color chart with/without preserved color metadata."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
import sys
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
palette=['#c03030','#30b040','#3040c0','#c0a030','#a030b0','#30b0b0','#d07030','#7050c0']
sender=None
try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    guest=command('guest.invite',kind='guest',name='Private color chart',returnMode='none')
    with sync_playwright() as pw:
        sender=pw.chromium.launch(channel='msedge',headless=True,args=['--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream','--autoplay-policy=no-user-gesture-required','--mute-audio'])
        page=sender.new_page()
        page.add_init_script('''(()=>{const original=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);navigator.mediaDevices.getUserMedia=async c=>{const s=await original(c);if(c.video){s.getVideoTracks().forEach(t=>{s.removeTrack(t);t.stop()});const canvas=document.createElement('canvas');canvas.width=640;canvas.height=360;const ctx=canvas.getContext('2d'),colors=COLORS;let tick=0;const draw=()=>{colors.forEach((v,i)=>{ctx.fillStyle=v;ctx.fillRect(i*80,0,80,360)});ctx.fillStyle='white';ctx.fillRect((tick++*5)%600,330,20,20)};draw();setInterval(draw,50);s.addTrack(canvas.captureStream(20).getVideoTracks()[0])}return s}})();'''.replace('COLORS',json.dumps(palette)))
        page.goto(guest['invite']+'&codec=h264',wait_until='domcontentloaded',timeout=60000);page.wait_for_timeout(1200)
        if not page.locator('#gowebcam').is_visible():page.locator('#container-3').click()
        if page.locator('#getPermissions').is_visible():page.locator('#getPermissions').click()
        page.locator('#gowebcam').wait_for(state='visible',timeout=30000);page.wait_for_function("!document.getElementById('gowebcam').disabled",timeout=45000);page.locator('#gowebcam').click(force=True)
        command('guest.set',id=guest['id'],admitted=True)
        for preserve in (False,True):
            command('source.update',id=guest['id'],preserveColorMetadata=preserve)
            until(lambda:next(s for s in state()['sources'] if s['id']==guest['id'])['frames']>60,'guest video',60);time.sleep(2)
            picture=image('program','metadata-'+str(preserve)+'.png');samples=[]
            for n,color in enumerate(palette):
                expected=tuple(int(color[i:i+2],16) for i in (1,3,5));actual=picture.getpixel((int((n+.5)*picture.width/8),int(picture.height*.4)))
                samples.append({'expected':expected,'received':actual,'maxChannelError':max(abs(a-b) for a,b in zip(actual,expected))})
            assert len({tuple(s['received']) for s in samples})==8,'Chart not visible'
            results['preserveMetadata'+str(preserve)]=samples
        sender.close();sender=None
    results['measurementCompleted']=True
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
    (report/'guest-colors.json').write_text(json.dumps(results,indent=2))
print(json.dumps({k:max(s['maxChannelError'] for s in v) for k,v in results.items() if isinstance(v,list)},indent=2))
