"""Real VDO.Ninja invitation, shared-clock camera/mic, recorded sync and receive delay."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-timing-e2e.py').read_text().split('try:\n    fixture=')[0])
import sys
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
shared=Path(__file__).with_name('mixer-returns-050-e2e.py').read_text()
exec(shared[shared.index('results={};'):shared.index('window.set_focus();')])
results={}
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
press('inviteGuest');prompt=window.child_window(title='Invite guest',control_type='Window');prompt.child_window(control_type='Edit').set_edit_text('QA timed guest');prompt.child_window(title='OK',control_type='Button').click_input()
dialog=window.child_window(title='QA timed guest - invite',control_type='Window');dialog.wait('visible');url=control('inviteUrl',dialog).get_value();press('closeInvite',dialog);time.sleep(1)
source_id=next(s['id'] for s in json.loads(show.read_text())['sources'] if s['name']=='QA timed guest')
try:
    with sync_playwright() as pw:
        receiver=receiver_page(pw,source_id)
        sender=pw.chromium.launch(channel='msedge',headless=False,args=['--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream','--autoplay-policy=no-user-gesture-required','--mute-audio','--window-position=850,40','--window-size=1000,850'])
        page=sender.new_page();page.add_init_script(instrument)
        page.add_init_script('''(() => {navigator.mediaDevices.getUserMedia=async constraints=>{
          if(!window.qaClock){const a=new AudioContext({sampleRate:48000}),d=a.createMediaStreamDestination(),b=a.createBuffer(1,48000,48000);
            const f=b.getChannelData(0);for(let i=0;i<5760;i++)f[i]=.25*Math.sin(2*Math.PI*1000*i/48000);
            const tone=a.createBufferSource();tone.buffer=b;tone.loop=true;tone.connect(d);tone.start(0);await a.resume();
            const c=document.createElement('canvas');c.width=1280;c.height=720;const x=c.getContext('2d');
            function draw(){x.fillStyle='#246040';x.fillRect(0,0,1280,720);x.fillStyle=a.currentTime%1<.12?'white':'black';x.fillRect(0,0,128,128);
              x.fillStyle='#dddd20';x.fillRect(Math.floor(a.currentTime*240)%1100,200,100,120);
              const t=Math.floor(Date.now()/10)&0xffffff;for(let i=0;i<24;i++){x.fillStyle=(t>>i)&1?'white':'black';x.fillRect(10+i*24,640,20,40)}requestAnimationFrame(draw)}draw();
            window.qaClock={a,c,d,video:c.captureStream(30)};}
          const q=qaClock,s=new MediaStream();if(constraints.video)s.addTrack(q.video.getVideoTracks()[0].clone());if(constraints.audio)s.addTrack(q.d.stream.getAudioTracks()[0].clone());return s;};})();''')
        page.goto(url+'&aec=0&agc=0&denoise=0&stereo',wait_until='domcontentloaded',timeout=60000);page.wait_for_timeout(1500)
        if not page.locator('#gowebcam').is_visible():page.locator('#container-3').click()
        if page.locator('#getPermissions').is_visible():page.locator('#getPermissions').click()
        page.locator('#gowebcam').wait_for(state='visible',timeout=30000)
        page.wait_for_function("document.getElementById('gowebcam') && !document.getElementById('gowebcam').disabled",timeout=45000)
        page.locator('#gowebcam').click(force=True);wait_media(receiver,'inbound-rtp');wait_media(page,'inbound-rtp');admit('QA timed guest');time.sleep(3)
        latency=[]
        for _ in range(20):
            latency.append(receiver.evaluate('''() => {const v=[...document.querySelectorAll('video')].find(v=>v.videoWidth>0&&v.readyState>=2);if(!v)throw Error('No received video');
              const c=document.createElement('canvas');c.width=1280;c.height=720;const x=c.getContext('2d');x.drawImage(v,0,0,1280,720);let t=0;
              for(let i=0;i<24;i++){const p=x.getImageData(20+i*24,660,1,1).data;if(p[0]>128)t|=1<<i}return ((Math.floor(Date.now()/10)-t)&0xffffff)*10;}'''))
            time.sleep(.1)
        assert max(latency)<2000,latency
        results['samePcSenderToReceivedWebpageVideoMs']={'median':float(np.median(latency)),'p95':float(np.percentile(latency,95)),'samples':latency}
        path=report/'guest-sync.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
        time.sleep(30);window.set_focus();window.capture_as_image().save(str(report/'guest.png'));press('studioRecord');wait_recording(False)
        results['recording']=decode_timing(path,1280,720,30)
        assert results['recording']['p95AbsoluteSyncMs']<120 and abs(results['recording']['earlyLateSyncChangeMs'])<60,results['recording']
        results['passed']=True;sender.close()
finally:
    (report/'guest-timing.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results),flush=True)
