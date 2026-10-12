"""Keep a synthetic, password-protected VDO sender alive for other real workflows."""
import argparse,json,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
p=argparse.ArgumentParser();p.add_argument('--report',required=True);a=p.parse_args();report=Path(a.report).resolve();report.mkdir(parents=True,exist_ok=True)
stream='qa'+uuid.uuid4().hex;password=uuid.uuid4().hex
with sync_playwright() as pw:
    browser=pw.chromium.launch(channel='msedge',headless=True,args=['--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream','--autoplay-policy=no-user-gesture-required','--mute-audio'])
    try:
        page=browser.new_page();page.add_init_script('''(()=>{const original=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);navigator.mediaDevices.getUserMedia=async c=>{const s=await original(c);if(c.audio){s.getAudioTracks().forEach(t=>{s.removeTrack(t);t.stop()});const a=new AudioContext(),o=a.createOscillator(),g=a.createGain(),d=a.createMediaStreamDestination();o.frequency.value=1660;g.gain.value=.04;o.connect(g);g.connect(d);o.start();await a.resume();window.qaTone={a,o,g};s.addTrack(d.stream.getAudioTracks()[0])}return s}})();''')
        page.goto(f'https://vdo.ninja/?push={stream}&password={password}&codec=h264&aec=0&agc=0&denoise=0&stereo',wait_until='domcontentloaded',timeout=60000);page.wait_for_timeout(1500)
        if not page.locator('#gowebcam').is_visible():page.locator('#container-3').click()
        if page.locator('#getPermissions').is_visible():page.locator('#getPermissions').click()
        page.locator('#gowebcam').wait_for(state='visible',timeout=30000);page.wait_for_function("!document.getElementById('gowebcam').disabled",timeout=45000);page.locator('#gowebcam').click(force=True)
        page.wait_for_timeout(3000);(report/'viewer.txt').write_text(f'https://vdo.ninja/?view={stream}&password={password}&codec=h264')
        print('Private synthetic sender ready',flush=True)
        started=time.monotonic()
        while not (report/'stop').exists() and time.monotonic()-started<5400:page.wait_for_timeout(1000)
    finally:browser.close()
