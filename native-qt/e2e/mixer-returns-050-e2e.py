"""Packaged guest/backstage/Program-return workflows using private synthetic peers.

VDO.Ninja signaling is public, but generated password-protected rooms/invites
are private and never sent to another person. Requires WebView2 loopback CDP.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import os,sys
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
import numpy as np

results={}; show=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/studio.mixer.json'
profile=show.parent/'browser';ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
instrument='''(() => {window.qaPeers=[];window.qaAnalysers=[];const PC=RTCPeerConnection;
window.RTCPeerConnection=new Proxy(PC,{construct(target,args){const pc=new target(...args);qaPeers.push(pc);
pc.addEventListener('track',e=>{if(e.track.kind!=='audio')return;const c=new AudioContext({sampleRate:48000}),a=c.createAnalyser();a.fftSize=32768;
const s=c.createMediaStreamSource(new MediaStream([e.track]));s.connect(a);a.smoothingTimeConstant=0;c.resume();qaAnalysers.push({c,a,s,track:e.track});});return pc;}});})();'''
stats_script='''async () => {const out=[];for(const pc of qaPeers){for(const s of (await pc.getStats()).values())
if(s.type==='inbound-rtp'||s.type==='outbound-rtp')out.push({type:s.type,kind:s.kind,frames:s.framesDecoded||s.framesEncoded||0,bytes:s.bytesReceived||s.bytesSent||0,energy:s.totalAudioEnergy||0});}return out;}'''
levels_script='''() => qaAnalysers.filter(x=>x.track.readyState==='live').map(({a,c})=>{const f=new Float32Array(a.frequencyBinCount);a.getFloatFrequencyData(f);const out={};
for(const hz of [440,1660,731]){const bin=Math.round(hz/c.sampleRate*a.fftSize);out[hz]=10*Math.log10(f.slice(bin-8,bin+9).reduce((s,x)=>s+Math.pow(10,x/10),0));}const t=new Float32Array(a.fftSize);a.getFloatTimeDomainData(t);out.rms=Math.sqrt(t.reduce((s,x)=>s+x*x,0)/t.length);return out;})'''

def wait_media(page,direction,timeout=60):
    deadline=time.time()+timeout;stats=[]
    while time.time()<deadline:
        stats=page.evaluate(stats_script)
        if any(s['type']==direction and s['kind']=='video' and s['frames']>20 for s in stats) and any(s['type']==direction and s['kind']=='audio' and s['bytes']>1000 for s in stats): return stats
        time.sleep(1)
    raise AssertionError({'missing':direction,'stats':stats,'body':page.locator('body').inner_text()[:1200]})

def receiver_page(pw,source_id):
    file=profile/source_id/'EBWebView/DevToolsActivePort';deadline=time.time()+30
    while not file.exists() and time.time()<deadline:time.sleep(.2)
    browser=pw.chromium.connect_over_cdp('http://127.0.0.1:'+file.read_text().splitlines()[0])
    page=next(p for c in browser.contexts for p in c.pages if 'vdo.ninja' in p.url)
    page.add_init_script(instrument);page.reload(wait_until='domcontentloaded');return page

def admit(name):
    source_row(name).select();press('admitGuest');time.sleep(.5)

window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(True)
scene('Break');press('takeScene');remove_sources(('QA ',))
scene('Main');enabled(control('guestProgramReturn'),True);enabled(control('monitorEnable'),False)
add_source('Video / audio file','QA media',str(media_fixture()))
# Space fixture tones apart so the 731 Hz Program codec skirts cannot be
# mistaken for the phone's tone; rejection still uses the same -55 dB limit.
invites=[]
for command,title,label in [('inviteGuest','Invite guest','QA guest'),('inviteCamera','Wireless camera','QA phone')]:
    press(command);prompt=window.child_window(title=title,control_type='Window');prompt.child_window(control_type='Edit').set_edit_text(label)
    prompt.child_window(title='OK',control_type='Button').invoke()
    dialog=window.child_window(title=label+' - invite',control_type='Window');dialog.wait('visible')
    invites.append((label,control('inviteUrl',dialog).get_value()));press('closeInvite',dialog)
time.sleep(1);data=json.loads(show.read_text());ids={s['name']:s['id'] for s in data['sources']}

try:
    with sync_playwright() as pw:
        sender=pw.chromium.launch(channel='msedge',headless=True,args=['--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream','--autoplay-policy=no-user-gesture-required','--mute-audio'])
        pages=[]
        for (name,url),hz in zip(invites,(440,1660)):
            receiver=receiver_page(pw,ids[name]);page=sender.new_page();page.add_init_script(instrument)
            page.add_init_script('''(() => {const original=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
                navigator.mediaDevices.getUserMedia=async constraints=>{const stream=await original(constraints);if(constraints.audio){stream.getAudioTracks().forEach(t=>{stream.removeTrack(t);t.stop();});
                const c=new AudioContext({sampleRate:48000}),o=c.createOscillator(),g=c.createGain(),d=c.createMediaStreamDestination();o.frequency.value=HZ;g.gain.value=.12;
                o.connect(g);g.connect(d);o.start();await c.resume();window.qaTone={c,o,g};stream.addTrack(d.stream.getAudioTracks()[0]);}return stream;};})();'''.replace('HZ',str(hz)))
            page.goto(url+'&webcam&autostart&aec=0&agc=0&denoise=0&stereo',wait_until='domcontentloaded',timeout=60000)
            results[name]={'send':wait_media(page,'outbound-rtp'),'receive':wait_media(receiver,'inbound-rtp'),'return':wait_media(page,'inbound-rtp')}
            pages.append(page);print(name+' connected both directions',flush=True)
        press('layout-Side by side');press('takeScene');time.sleep(3)
        assert all(not next(s for s in json.loads(show.read_text())['sources'] if s['name']==name)['admitted'] for name,_ in invites)
        results['backstage_levels']=[p.evaluate(levels_script) for p in pages]
        for levels in results['backstage_levels']:
            assert levels and max(v['731'] for v in levels)>-45,levels
            assert all(max(v['440'],v['1660']) < -55 for v in levels),levels
        for name,_ in invites:admit(name)
        time.sleep(2)
        # Admitting to Preview must not expose the guest in Program before Take.
        before=[p.evaluate(levels_script) for p in pages];results['previewOnlyLevels']=before
        assert all(all(max(v['440'],v['1660']) < -55 for v in levels) for levels in before),before
        press('takeScene');time.sleep(4)
        levels=[p.evaluate(levels_script) for p in pages];results['mix_minus_levels']=levels
        for incoming,own,other in zip(levels,('440','1660'),('1660','440')):
            assert max(v[other] for v in incoming)>-40,incoming
            assert max(v[own] for v in incoming)<-55,incoming
        print('Backstage, Take and guest mix-minus passed',flush=True)
        # Backstage a previously live guest, then re-admit without Take.
        admit('QA guest');admit('QA guest');time.sleep(3)
        assert all(v['440'] < -55 for v in pages[1].evaluate(levels_script))
        press('takeScene');time.sleep(2)
        add_source('Microphone / audio input','QA host',device='HD Pro Webcam C920')
        for name in ('QA guest','QA phone','QA media'):enabled(channel(name,'Mute'),True)
        time.sleep(3)
        results['host_on_air']=[p.evaluate(levels_script) for p in pages]
        # Private host talkback must remain audible in both returns while Program is silent.
        press('guestTalkback');time.sleep(3)
        results['private_host_returns']=[p.evaluate(levels_script) for p in pages]
        assert all(max(v['rms'] for v in levels)>1e-7 for levels in results['private_host_returns']),results['private_host_returns']
        path=report/'private-talkback-program.mkv';configure('Record to file (MKV)',path);press('studioStartOutput');wait_output(True)
        time.sleep(5);press('studioStartOutput');wait_output(False)
        pcm=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-vn','-f','f32le','-ac','1','pipe:1']),dtype=np.float32)
        rms=float(np.sqrt(np.mean(pcm*pcm)));assert rms<1e-6,rms
        results['private_host_program_rms']=rms;press('guestTalkback')
        window.capture_as_image().save(str(report/'guests.png'));sender.close()
finally:
    (report/'guest-returns.json').write_text(json.dumps(results,indent=2))
