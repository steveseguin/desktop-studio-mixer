"""Visible two-guest room conversation and wireless return modes, real WebRTC."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import sys,psutil,numpy as np
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
shared=Path(__file__).with_name('mixer-returns-050-e2e.py').read_text()
exec(shared[shared.index('results={};'):shared.index('window.set_focus();')])
levels_script=levels_script.replace('[440,1660,731]','[440,1660,731,2370]')
root=psutil.Process(args.pid)
def create_invite(command,title,name):
    press(command);prompt=window.child_window(title=title,control_type='Window');prompt.child_window(control_type='Edit').set_edit_text(name)
    prompt.child_window(title='OK',control_type='Button').click_input()
    dialog=window.child_window(title=name+' - invite',control_type='Window');dialog.wait('visible')
    mode=control('inviteReturnMode',dialog).selected_text();url=control('inviteUrl',dialog).get_value()
    dialog.capture_as_image().save(str(report/(name.replace(' ','-')+'-invite.png')));press('closeInvite',dialog)
    return url,mode
def join(page,url):
    page.goto(url+'&aec=0&agc=0&denoise=0&stereo',wait_until='domcontentloaded',timeout=60000);page.wait_for_timeout(1500)
    if not page.locator('#gowebcam').is_visible():page.locator('#container-3').click()
    if page.locator('#getPermissions').is_visible():page.locator('#getPermissions').click()
    page.locator('#gowebcam').wait_for(state='visible',timeout=30000)
    page.wait_for_function("document.getElementById('gowebcam') && !document.getElementById('gowebcam').disabled",timeout=45000)
    # Start pulses with CSS; click its visible center without waiting for the
    # animation to become stable. This is real pointer input, not autostart.
    page.locator('#gowebcam').click(force=True);wait_media(page,'outbound-rtp')
def return_mode(name,mode):
    window.set_focus();source_row(name).select();press('interactSource')
    dialog=window.child_window(title=name+' - invite',control_type='Window');dialog.wait('visible')
    choose_dialog(dialog,'inviteReturnMode',mode);url=control('inviteUrl',dialog).get_value();press('closeInvite',dialog);return url
def signal(page,hz):
    levels=page.evaluate(levels_script);assert levels, 'No return audio track'
    return max(v[str(hz)] for v in levels)
def returns_match(pages,expected):
    time.sleep(3)
    values=[p.evaluate(levels_script) for p in pages]
    for page,own,other in zip(pages,(440,1660),(1660,440)):
        assert signal(page,own)<-55,(own,values)
        assert (signal(page,other)>-40) if expected else (signal(page,other)<-55),(other,values)
    return values
def cpu_sample():
    time.sleep(3);owned=[root,*root.children(recursive=True)]
    for p in owned:p.cpu_percent()
    time.sleep(6)
    return {'cpuPercentOneCore':sum(p.cpu_percent() for p in owned if p.is_running()),'privateMiB':sum(p.memory_info().private for p in owned if p.is_running())/2**20}
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(True);scene('Main');time.sleep(1)
add_source('Video / audio file','QA media',str(media_fixture()));press('takeScene')
invites=[]
for command,title,name in [('inviteGuest','Invite guest','QA Alice'),('inviteGuest','Invite guest','QA Bob'),('inviteCamera','Wireless camera','QA wireless')]:
    url,mode=create_invite(command,title,name);invites.append((name,url));results[name+' default']=mode
assert 'No return' in results['QA wireless default']
assert all('Program video' in results[name+' default'] for name in ('QA Alice','QA Bob'))
time.sleep(1);saved=json.loads(show.read_text());ids={s['name']:s['id'] for s in saved['sources']}
assert all(not s['followsScene'] for s in saved['sources'] if s['kind']=='guest')
try:
    with sync_playwright() as pw:
        sender=pw.chromium.launch(channel='msedge',headless=False,args=['--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream','--autoplay-policy=no-user-gesture-required','--mute-audio','--window-position=850,40','--window-size=1000,850'])
        pages=[]
        for (name,url),hz in zip(invites,(440,1660,2370)):
            receiver=receiver_page(pw,ids[name]);page=sender.new_page();page.add_init_script(instrument)
            page.add_init_script('''(() => {const original=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
            navigator.mediaDevices.getUserMedia=async constraints=>{const s=await original(constraints);if(constraints.audio){s.getAudioTracks().forEach(t=>{s.removeTrack(t);t.stop()});
            const c=new AudioContext({sampleRate:48000}),o=c.createOscillator(),g=c.createGain(),d=c.createMediaStreamDestination();o.frequency.value=HZ;g.gain.value=.12;o.connect(g);g.connect(d);o.start();await c.resume();window.qaTone={c,o,g};s.addTrack(d.stream.getAudioTracks()[0])}return s};})();'''.replace('HZ',str(hz)))
            join(page,url);wait_media(receiver,'inbound-rtp');print(name+' sending to Studio',flush=True)
            if name!='QA wireless':wait_media(page,'inbound-rtp')
            pages.append(page);page.screenshot(path=str(report/(name.replace(' ','-')+'-joined.png')))
        guests=pages[:2];phone=pages[2]
        time.sleep(3);assert not any(s['type']=='inbound-rtp' and (s['frames'] or s['bytes']) for s in phone.evaluate(stats_script))
        results['wirelessSendOnly']='video/audio reached Studio; no inbound audio/video'
        print('All peers joined; send-only wireless verified',flush=True)
        results['backstageIsolation']=returns_match(guests,False)
        for name in ('QA Alice','QA Bob'):admit(name)
        results['admittedBeforeTake']=returns_match(guests,True)
        # The call continues while Program contains neither guest.
        scene('Break');press('takeScene');results['offscreenConversation']=returns_match(guests,True)
        silent=report/'offscreen-program.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(silent));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
        time.sleep(5);press('studioRecord');wait_recording(False)
        raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(silent),'-vn','-f','f32le','-ac','1','-']);pcm=np.frombuffer(raw,np.float32)
        blocks=pcm[:len(pcm)//4800*4800].reshape(-1,4800)
        tones={hz:float(np.median(abs(blocks@np.exp(-2j*np.pi*hz*np.arange(4800)/48000))*2/4800)) for hz in (440,1660)}
        assert all(v>.02 for v in tones.values()),tones
        results['offscreenProgramGuestsAudible']=tones
        # Keep the conversation private without having to remove admitted callers.
        for name in ('QA Alice','QA Bob'):enabled(channel(name,'On air'),False)
        results['offAirConversation']=returns_match(guests,True)
        private=report/'private-rehearsal.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(private));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
        time.sleep(4);press('studioRecord');wait_recording(False)
        pcm=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(private),'-vn','-f','f32le','-ac','1','-']),np.float32)
        assert float(np.max(abs(pcm[48000:-24000])))<.001
        for name in ('QA Alice','QA Bob'):enabled(channel(name,'On air'),True)
        press('privateBreak');results['privateBreakConversation']=returns_match(guests,True)
        window.capture_as_image().save(str(report/'private-break-guests.png'));press('privateBreak')
        for name in ('QA Alice','QA Bob'):enabled(channel(name,'Scene'),True)
        silent=report/'offscreen-scene-follow.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(silent));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
        time.sleep(4);press('studioRecord');wait_recording(False)
        raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(silent),'-vn','-f','f32le','-ac','1','-']);pcm=np.frombuffer(raw,np.float32)
        assert np.sqrt(np.mean(pcm**2))<1e-6;results['offscreenSceneFollowSilent']=True
        results['sceneFollowStillPrivateConversation']=returns_match(guests,True)
        for name in ('QA Alice','QA Bob'):enabled(channel(name,'Scene'),False)
        scene('Main');press('takeScene');time.sleep(2)
        enabled(channel('QA Alice','Mute'),True);time.sleep(2);assert signal(guests[1],440)<-55
        enabled(channel('QA Alice','Mute'),False);admit('QA Alice');time.sleep(2);assert signal(guests[1],440)<-55
        admit('QA Alice');results['readmittedConversation']=returns_match(guests,True)
        # Audio-only camera return must not pull video, and excludes its own mic.
        admit('QA wireless');press('takeScene');url=return_mode('QA wireless','Audio return only');join(phone,url)
        time.sleep(5);assert signal(phone,440)>-40 and signal(phone,1660)>-40 and signal(phone,2370)<-55
        stats=phone.evaluate(stats_script);assert not any(s['type']=='inbound-rtp' and s['kind']=='video' and s['frames'] for s in stats),stats
        results['wirelessAudioOnly']={'stats':stats,'levels':phone.evaluate(levels_script)}
        url=return_mode('QA wireless','Program video and mix-minus audio');join(phone,url);wait_media(phone,'inbound-rtp')
        results['wirelessVideoReturn']=cpu_sample();assert signal(phone,2370)<-55
        url=return_mode('QA wireless','No return');join(phone,url);time.sleep(3)
        assert not any(s['type']=='inbound-rtp' and (s['frames'] or s['bytes']) for s in phone.evaluate(stats_script))
        results['wirelessNoReturn']=cpu_sample();results['guestsAfterWirelessRejoins']=returns_match(guests,True)
        # A leaving caller must not freeze the other caller's return video.
        guests[0].close();before=sum(s['frames'] for s in guests[1].evaluate(stats_script) if s['type']=='inbound-rtp' and s['kind']=='video')
        time.sleep(4);after=sum(s['frames'] for s in guests[1].evaluate(stats_script) if s['type']=='inbound-rtp' and s['kind']=='video')
        assert after>before+20;results['remainingGuestVideoAfterPeerLeaves']=after-before
        window.set_focus();window.capture_as_image().save(str(report/'conversation-final.png'))
        # A reopened show must retain the return mode and the master switch.
        executable=root.exe()
        for active in (True,False):
            enabled(control('guestProgramReturn'),active);time.sleep(1)
            assert json.loads(show.read_text())['guestReturns']==active
            window.set_focus();window.type_keys('^q');root.wait(25)
            restarted=subprocess.Popen([executable,'--studio-show='+str(show),'--local-control-discovery='+os.environ['STUDIO_E2E_DISCOVERY']],creationflags=subprocess.CREATE_NO_WINDOW)
            (report/'app.pid').write_text(str(restarted.pid));root=psutil.Process(restarted.pid)
            app=Application(backend='uia').connect(process=restarted.pid,timeout=25)
            window=app.window(title='Desktop Studio - Powered by VDO.Ninja');window.wait('visible',timeout=25)
            control.__defaults__=(window,);press.__defaults__=(window,);window.set_focus();time.sleep(1)
            assert bool(control('guestProgramReturn').get_toggle_state())==active
            assert next(s for s in json.loads(show.read_text())['sources'] if s['name']=='QA wireless')['returnMode']=='none'
            if active:
                before=sum(s['frames'] for s in guests[1].evaluate(stats_script) if s['type']=='inbound-rtp' and s['kind']=='video')
                deadline=time.monotonic()+35
                while time.monotonic()<deadline:
                    after=sum(s['frames'] for s in guests[1].evaluate(stats_script) if s['type']=='inbound-rtp' and s['kind']=='video')
                    if after>before+20:break
                    time.sleep(1)
                assert after>before+20,'Guest return did not resume after show restart'
        results['returnPersistence']='enabled return resumed; disabled master stayed disabled; wireless stayed send-only'
        sender.close()
        results['passed']=True
finally:
    (report/'conversation.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
