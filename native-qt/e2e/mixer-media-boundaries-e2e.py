"""Large/corrupt media and existing-recording protection in the packaged app."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np,hashlib
results={};ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))
choose_dialog(window,'canvasFormat','1280x720 / 30 fps');enabled(control('guestProgramReturn'),False)
try:
    clip=report/'large.mp4'
    subprocess.run([str(ffmpeg),'-v','error','-y','-f','lavfi','-i','testsrc2=size=4096x2160:rate=30','-t','3','-c:v','libopenh264','-b:v','24000000',str(clip)],check=True,timeout=120)
    add_source('Video / audio file','QA large media',str(clip));time.sleep(3)
    first=np.asarray(control('previewCanvas').capture_as_image()).astype(float);time.sleep(.6);second=np.asarray(control('previewCanvas').capture_as_image()).astype(float)
    change=float(np.mean(abs(second-first)));assert change>.5,(change,control('sourceHealth').window_text())
    results['4096x2160Source']={'changedPixelsMean':change,'health':control('sourceHealth').window_text()}
    path=report/'large-recording.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').invoke();time.sleep(8)
    assert control('studioRecord').window_text()=='Stop recording',control('recordingStatus').window_text()
    press('studioRecord');deadline=time.time()+30
    while time.time()<deadline and 'saved' not in control('recordingStatus').window_text().lower():time.sleep(.3)
    assert 'saved' in control('recordingStatus').window_text().lower()
    decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,text=True,timeout=60)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr;results['largeSourceRecording']='fully decoded'
    original=hashlib.sha256(path.read_bytes()).hexdigest()
    press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').invoke();time.sleep(.7)
    next(c for c in window.descendants(control_type='Button') if c.window_text().replace('&','')=='Yes').invoke();time.sleep(1)
    assert 'already exists' in control('recordingStatus').window_text(),control('recordingStatus').window_text()
    assert hashlib.sha256(path.read_bytes()).hexdigest()==original;results['overwriteProtection']='existing recording byte-for-byte intact'
    remove('QA large media');bad=report/'corrupt.mp4';bad.write_bytes(b'Desktop Studio malformed media fixture')
    add_source('Video / audio file','QA corrupt media',str(bad));time.sleep(3)
    health=control('sourceHealth').window_text();assert any(s in health.lower() for s in ['open','failed','error']),health
    assert len(window.descendants())>40;results['corruptMedia']=health
finally:
    (report/'media-boundaries.json').write_text(json.dumps(results,indent=2))
