"""Exercise real packaged outputs against private local consumers."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import socket
import numpy as np
results={}; receiver=None
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False)

def recording(active):
    deadline=time.time()+45
    while time.time()<deadline:
        text=control('recordingStatus').window_text()
        if active and text.startswith('Recording:'):return
        if not active and 'saved' in text.lower():return
        time.sleep(.3)
    raise AssertionError(text)

def record(path):
    press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path))
    dialog.child_window(title='Save',control_type='Button').invoke();recording(True)

def decode(path):
    result=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(path),'-f','null','-'],capture_output=True,text=True,timeout=45)
    assert result.returncode==0,result.stderr
    assert 'Video: h264' in result.stderr and 'Audio: aac' in result.stderr,result.stderr
    raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1'])
    samples=np.frombuffer(raw,dtype=np.float32)[48000:96000];t=np.arange(len(samples))/48000
    tone=float(abs(np.sum(samples*np.exp(-2j*np.pi*731*t)))*2/len(samples));assert tone>.02,tone
    return {'path':str(path),'tone731Hz':tone}

try:
    scene('Main')
    remove_sources(('QA ',))
    add_source('Video / audio file','QA output media',str(media_fixture()))
    choose_dialog(window,'canvasFormat','1280x720 / 30 fps');time.sleep(1)
    enabled(control('virtualCamera'),True);time.sleep(2)
    probe=subprocess.run([str(ffmpeg),'-hide_banner','-loglevel','error','-f','dshow','-video_size','1280x720','-framerate','30','-i','video=Desktop Studio Virtual Camera','-frames:v','45','-f','framemd5','-'],capture_output=True,text=True,timeout=25)
    (report/'virtual-camera.txt').write_text(probe.stdout+'\n'+probe.stderr)
    assert probe.returncode==0,probe.stderr
    frames=[line for line in probe.stdout.splitlines() if line and not line.startswith('#')]
    assert len(frames)==45 and len({line.split(',')[-1] for line in frames})>20
    results['virtual_camera']={'decodedFrames':len(frames),'distinctFrames':len({line.split(',')[-1] for line in frames})}
    enabled(control('virtualCamera'),False);press('unregisterCamera')
    with socket.socket() as reservation:reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
    received=report/'srt.mkv';log=(report/'srt.log').open('w')
    receiver=subprocess.Popen([str(ffmpeg),'-hide_banner','-y','-i',f'srt://127.0.0.1:{port}?mode=listener&latency=120000','-t','22','-map','0','-c','copy',str(received)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    first=report/'before-and-during-stream.mkv';second=report/'during-and-after-stream.mkv'
    record(first);time.sleep(3)
    configure('SRT (caller)',f'srt://127.0.0.1:{port}');press('studioStartOutput');wait_output(True)
    time.sleep(6);press('studioRecord');recording(False)
    assert control('studioStartOutput').window_text()=='STOP'
    record(second);receiver.wait(timeout=45);press('studioStartOutput');wait_output(False)
    assert control('studioRecord').window_text()=='Stop recording'
    time.sleep(4);press('studioRecord');recording(False);log.close()
    results['independent_stream_record_lifecycles']=[decode(path) for path in (first,second,received)]
finally:
    if receiver and receiver.poll() is None:receiver.terminate();receiver.wait(timeout=10)
    (report/'outputs.json').write_text(json.dumps(results,indent=2))
