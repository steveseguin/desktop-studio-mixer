"""Exercise private VDO.Ninja guest/phone invitations with synthetic WebRTC peers.

The packaged app must have been launched with WebView2 remote debugging on an
ephemeral loopback port. Fake browser input is optional for talkback checks.
No invitation is sent to another person. Uses VDO.Ninja's public signaling service.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
import os
import sys
import socket
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
from urllib.parse import urlparse,parse_qs

results={}
children=[]
fixture_server=None
show=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/studio.mixer.json'
profile=show.parent/'browser'
instrument='''(() => {window.qaPeers=[];const PC=RTCPeerConnection;
window.RTCPeerConnection=new Proxy(PC,{construct(target,args){const pc=new target(...args);qaPeers.push(pc);return pc;}});})();'''
stats_script='''async () => {const out=[];for(const pc of window.qaPeers||[]){const stats=await pc.getStats();
for(const s of stats.values())if(s.type==='inbound-rtp'||s.type==='outbound-rtp')out.push({type:s.type,kind:s.kind,ssrc:s.ssrc,bytes:s.bytesReceived||s.bytesSent||0,frames:s.framesDecoded||s.framesEncoded||0,energy:s.totalAudioEnergy||0});}return out;}'''

def cdp_page(playwright,source_id):
    deadline=time.time()+25
    file=profile/source_id/'EBWebView/DevToolsActivePort'
    while not file.exists() and time.time()<deadline: time.sleep(.2)
    port=int(file.read_text().splitlines()[0])
    browser=playwright.chromium.connect_over_cdp(f'http://127.0.0.1:{port}')
    page=next(p for c in browser.contexts for p in c.pages if 'vdo.ninja' in p.url)
    page.add_init_script(instrument); page.reload(wait_until='domcontentloaded')
    return page

def wait_media(page,direction,timeout=45):
    deadline=time.time()+timeout
    latest=[]
    while time.time()<deadline:
        latest=page.evaluate(stats_script)
        if any(s['type']==direction and s['kind']=='video' and s['frames']>20 for s in latest) and any(s['type']==direction and s['kind']=='audio' and s['bytes']>1000 for s in latest): return latest
        time.sleep(1)
    raise AssertionError({'missingMedia':direction,'stats':latest,'body':page.locator('body').inner_text()[:1500]})

window.child_window(title='Studio',control_type='TabItem').click_input()
scene('Break'); press('takeScene')
remove_sources(('QA guest ', 'QA phone '))
scene('Main')
invites=[]
for command,title,label in [('inviteGuest','Invite guest','QA guest'),('inviteCamera','Wireless camera','QA phone')]:
    press(command); prompt=window.child_window(title=title,control_type='Window')
    prompt.child_window(control_type='Edit').set_edit_text(label); prompt.child_window(title='OK',control_type='Button').invoke()
    invite=window.child_window(title=label+' - invite',control_type='Window'); invite.wait('visible')
    url=control('inviteUrl',invite).get_value(); press('closeInvite',invite)
    invites.append((label,url))
time.sleep(1)
data=json.loads(show.read_text()); ids={s['name']:s['id'] for s in data['sources']}
assert len({parse_qs(urlparse(url).query)['room'][0] for _,url in invites})==1
try:
    with sync_playwright() as pw:
        sender=pw.chromium.launch(channel='msedge',headless=True,args=['--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream','--autoplay-policy=no-user-gesture-required','--mute-audio'])
        senders=[]; receivers=[]
        for label,url in invites:
            receiver=cdp_page(pw,ids[label]); receivers.append(receiver)
            page=sender.new_page(); page.add_init_script(instrument)
            page.goto(url+'&webcam&autostart',wait_until='domcontentloaded',timeout=60000)
            senders.append(page)
            outgoing=wait_media(page,'outbound-rtp')
            incoming=wait_media(receiver,'inbound-rtp')
            results[label]={'sender':outgoing,'receiver':incoming}
            print(label+' audio/video connected',flush=True)
        press('layout-Side by side'); press('takeScene')
        time.sleep(4); window.capture_as_image().save(str(report/'guest-studio.png'))
        recording=report/f'guests-{int(time.time())}.mkv'
        press('mixerOutput'); select('outputSelect','Record to file (MKV)'); control('outputUrlInput').set_edit_text(str(recording))
        window.child_window(title='Studio',control_type='TabItem').click_input(); press('studioStartOutput')
        time.sleep(7); press('studioStartOutput'); time.sleep(4)
        import array,math
        ffmpeg=Path('dist/video-mixer-portable/ffmpeg/bin/ffmpeg.exe').resolve()
        raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-vn','-f','f32le','-ac','1','pipe:1'])
        pcm=array.array('f'); pcm.frombytes(raw); rms=math.sqrt(sum(x*x for x in pcm)/len(pcm))
        assert rms>.005,('Guest audio missing from program',rms)
        results['guest_program_recording']={'path':str(recording),'audioRms':rms}
        before=[{s['ssrc'] for s in p.evaluate(stats_script) if s['type']=='inbound-rtp' and s['kind']=='audio'} for p in senders]
        press('guestTalkback'); time.sleep(3)
        director=cdp_page(pw,'talkback-'+data['room'])
        director.wait_for_timeout(6000)
        # Save control labels for reproducible selection without exporting private URLs.
        results['directorControls']=director.locator('button').evaluate_all('(buttons)=>buttons.filter(b=>b.offsetWidth&&b.offsetHeight).map(b=>({id:b.id,text:b.innerText,title:b.title}))')
        (report/'talkback-panel.png').write_bytes(director.screenshot())
        director.locator('#press2talk').click()
        director.wait_for_timeout(5000)
        results['talkbackControls']=director.locator('button').evaluate_all('(buttons)=>buttons.filter(b=>b.offsetWidth&&b.offsetHeight).map(b=>({id:b.id,text:b.innerText,title:b.title}))')
        (report/'talkback-enabled.png').write_bytes(director.screenshot())
        results['talkbackStats']=director.evaluate(stats_script)
        assert director.evaluate('session.muted') is True,'Talkback did not start muted'
        director.locator('#mutebutton').click(force=True) # the muted control intentionally pulses
        assert director.evaluate('session.muted') is False
        director.wait_for_timeout(6000)
        heard=[]
        for index,page in enumerate(senders):
            audio=[s for s in page.evaluate(stats_script) if s['type']=='inbound-rtp' and s['kind']=='audio' and s['ssrc'] not in before[index]]
            assert any(s['energy']>.01 for s in audio),audio
            heard.append(audio)
        director.locator('#mutebutton').click(force=True)
        assert director.evaluate('session.muted') is True
        results['talkback_heard_by_both_guests']=heard
        # Exercise every input together, retaining both connected guests and talkback.
        helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
        exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
        window.set_focus()
        native=subprocess.Popen([sys.executable,'-B',str(Path(__file__).with_name('mixer-sources-e2e.py')),'--fixture'],creationflags=subprocess.CREATE_NO_WINDOW)
        children.append(native);time.sleep(2)
        add_source('Game / window','QA game',device='Video Mixer QA Window')
        add_source('Camera / webcam','QA webcam',device='HD Pro Webcam C920')
        add_source('Microphone / audio input','QA microphone',device='HD Pro Webcam C920')
        class Overlay(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body=b'<!doctype html><body style="margin:0;background:transparent"><div style="position:absolute;bottom:20px;left:20px;background:#e72452;padding:16px;color:white;font:32px sans-serif">Private production session</div>'
                self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body)
            def log_message(self,*_):pass
        fixture_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Overlay)
        threading.Thread(target=fixture_server.serve_forever,daemon=True).start()
        press('socialChat');prompt=window.child_window(title='Social Stream Ninja',control_type='Window')
        prompt.child_window(control_type='Edit').set_edit_text(f'http://127.0.0.1:{fixture_server.server_port}/')
        prompt.child_window(title='OK',control_type='Button').invoke();time.sleep(2)
        press('layout-Side by side');press('takeScene')
        combined=report/f'combined-production-{int(time.time())}.mkv'
        configure('Record to file (MKV)',combined);press('studioStartOutput');wait_output(True)
        time.sleep(6);scene('Break');press('fadeScene');time.sleep(2);scene('Main');press('takeScene');time.sleep(3)
        press('studioStartOutput');wait_output(False)
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reserve:
            reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
        received=report/f'combined-srt-{int(time.time())}.mkv';log=(report/'combined-srt.log').open('w')
        rx=subprocess.Popen([str(ffmpeg),'-hide_banner','-y','-i',f'srt://127.0.0.1:{port}?mode=listener&latency=120000',
            '-t','8','-map','0','-c','copy',str(received)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        children.append(rx);time.sleep(1)
        configure('SRT (caller)',f'srt://127.0.0.1:{port}');press('studioStartOutput');wait_output(True)
        # Pump the browser event loop while the native app publishes the mix.
        director.wait_for_timeout(15000);rx.wait(timeout=20);press('studioStartOutput');wait_output(False);log.close()
        assert rx.returncode==0 and combined.stat().st_size>10000 and received.stat().st_size>10000
        results['complete_production_session']={'inputs':['C920 webcam','C920 microphone','native window','guest WebRTC','wireless invitation WebRTC','transparent chat fixture'],
            'recording':str(combined),'localSrtRecording':str(received),'sceneCutsAndFades':'passed'}
        scene('Break');press('takeScene');remove_sources(('QA ','Social chat'))
        scene('Opening');press('takeScene')
        sender.close()
finally:
    if fixture_server:fixture_server.shutdown()
    for child in children:
        if child.poll() is None:child.terminate();child.wait(timeout=5)
    (report/'guest-workflow.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
