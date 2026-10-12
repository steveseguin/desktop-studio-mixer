"""Real Spout sender -> isolated input -> packaged Program recording/recovery."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import psutil,numpy as np
root=psutil.Process(args.pid);results={};sender=None
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
def workers():
    return [p for p in root.children() if '--studio-worker' in p.cmdline() and 'spout' in p.cmdline()]
def changed():
    a=np.asarray(control('previewCanvas').capture_as_image()).astype(float);time.sleep(1)
    b=np.asarray(control('previewCanvas').capture_as_image()).astype(float)
    delta=float(abs(b-a).mean());assert delta>.1,delta;return delta
try:
    sender=subprocess.Popen([str(Path('build-isolation-gates/bin/spout_test_sender.exe').resolve()),'--name=Desktop Studio QA input','--duration-ms=180000','--width=640','--height=360','--resize-after-ms=20000','--resize-width=1280','--resize-height=720'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(2);window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input()
    studio_mode(False);scene('Main');remove_sources(('QA ', 'Social chat'));enabled(control('guestProgramReturn'),False)
    add_source('Spout','QA Spout input',device='Desktop Studio QA input');time.sleep(3)
    results['inputChange']=changed()
    path=report/'spout.mkv';configure('Record to file (MKV)',path);press('studioStartOutput');wait_output(True)
    before=workers();assert len(before)==1;before[0].kill();before[0].wait(timeout=10)
    deadline=time.time()+20
    while time.time()<deadline and not any(p.pid!=before[0].pid for p in workers()):time.sleep(.3)
    assert any(p.pid!=before[0].pid for p in workers());time.sleep(4)
    results['recoveredInputChange']=changed();assert control('studioStartOutput').window_text()=='STOP RECORDING'
    time.sleep(15);results['resizedSenderChange']=changed();press('studioStartOutput');wait_output(False)
    decode=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,text=True,timeout=90)
    assert decode.returncode==0 and not decode.stderr,decode.stderr
    frames=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-an','-vf','fps=2,scale=64:36','-pix_fmt','gray','-f','rawvideo','pipe:1']),np.uint8).reshape(-1,36,64)
    distinct=len({f.tobytes() for f in frames});assert distinct>15,distinct
    results['recording']={'fullDecode':'passed','distinctSampledFrames':distinct}
finally:
    if sender and sender.poll() is None:sender.terminate();sender.wait(timeout=5)
    try:
        if control('studioStartOutput').window_text()=='STOP RECORDING':press('studioStartOutput');wait_output(False)
    except Exception:pass
    (report/'spout-input.json').write_text(json.dumps(results,indent=2))
