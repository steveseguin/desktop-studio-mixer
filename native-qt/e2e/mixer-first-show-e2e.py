"""Operate the packaged GUI from its starter show through a first recording.

Use a private --studio-show and STUDIO_E2E_SHOW. No Studio API mutations.
Screenshots/recordings contain the connected Logitech camera and microphone.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np
results={};started=time.monotonic()
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()

def wait_for(fn,label,seconds=30):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if fn():return
        time.sleep(.25)
    raise AssertionError(label)

try:
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input()
    assert not control('studioMode').get_toggle_state()
    window.capture_as_image().save(str(report/'01-starter.png'))
    scene('Main')
    add_source('Camera / webcam','QA first camera',device='HD Pro Webcam C920')
    wait_for(lambda:'fps received' in control('sourceHealth').window_text(),'Camera never showed capture status')
    time.sleep(2);window.capture_as_image().save(str(report/'02-camera-only.png'))
    results['cameraVideoWithoutImplicitMicrophone']=True
    add_source('Microphone / audio input','QA first microphone',device='HD Pro Webcam C920')
    assert not channel('QA first microphone','Listen').get_toggle_state()
    wait_for(lambda:control('masterAudioMeter').iface_range_value.CurrentValue>-60,'Microphone meter did not move')
    window.capture_as_image().save(str(report/'03-camera-and-mic.png'))
    recording=report/'first-recording.mkv'
    press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(recording))
    dialog.child_window(title='Save',control_type='Button').click_input()
    wait_for(lambda:control('recordingStatus').window_text().startswith('Recording:'),'Recording did not start')
    results['secondsToRecording']=round(time.monotonic()-started,2)
    time.sleep(4);enabled(channel('QA first microphone','Mute'),True);time.sleep(3)
    assert control('masterAudioMeter').iface_range_value.CurrentValue==-60
    window.capture_as_image().save(str(report/'04-muted-recording.png'))
    enabled(channel('QA first microphone','Mute'),False);time.sleep(4)
    press('studioRecord');wait_for(lambda:'saved' in control('recordingStatus').window_text().lower(),'Recording did not finalize')
    saved=report/'my-first-show.mixer.json';press('exportShow')
    dialog=window.child_window(title='Save show (includes source URLs and invite links)',control_type='Window')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(saved))
    dialog.child_window(title='Save',control_type='Button').click_input();wait_for(saved.exists,'Show was not saved')
    window.capture_as_image().save(str(report/'05-saved.png'))
    decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(recording),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,timeout=45)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-vn','-ar','48000','-ac','1','-f','f32le','-'])
    samples=np.frombuffer(raw,dtype=np.float32)
    rms=[float(np.sqrt(np.mean(samples[n:n+24000]**2))) for n in range(0,len(samples)-24000,24000)]
    assert max(rms)>1e-5 and min(rms)<1e-7,rms
    results.update(recordingDecoded=True,microphoneMuteRecorded=True,halfSecondRms=rms,savedShow=True,passed=True)
finally:
    (report/'first-show.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2),flush=True)
