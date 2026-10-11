"""Packaged 0.5 workflows: native canvas, editing, media and independent recording.

Use a disposable show. Produces private local recordings; no public destination.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np
import os

results = {}
ffmpeg = Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
window.set_focus()
window.child_window(title='Studio', control_type='TabItem').click_input()
studio_mode(False)

def combo(name, text):
    choose_dialog(window, name, text)
    time.sleep(.4)

def spin(name, value):
    control(name).iface_range_value.SetValue(value); time.sleep(.4)

def wait_record(active):
    deadline = time.time()+35
    while time.time()<deadline:
        value = control('recordingStatus').window_text()
        if active and value.startswith('Recording:'): return
        if not active and 'saved' in value.lower(): return
        time.sleep(.3)
    raise AssertionError(value)

try:
    for _ in range(10):
        rows=[r for r in control('mixerSources').descendants(control_type='ListItem') if r.window_text().startswith('QA ')]
        if not rows: break
        rows[0].select(); press('removeSource')
    scene('Main')
    add_source('Color', 'QA green', '#24b46a')
    near(color('previewCanvas'), (36,180,106))
    combo('compositorBackend', 'CPU composition'); near(color('previewCanvas'), (36,180,106))
    combo('compositorBackend', 'GPU composition'); near(color('previewCanvas'), (36,180,106))
    results['gpu_cpu_solid']='passed'
    spin('layerW',50); spin('layerH',50)
    press('undoEdit'); assert abs(control('layerH').iface_range_value.CurrentValue-100)<.1
    press('redoEdit'); assert abs(control('layerH').iface_range_value.CurrentValue-50)<.1
    enabled(control('layerLock'),True); assert not control('layerW').is_enabled()
    enabled(control('layerLock'),False); press('layout-Full')
    results['layout_undo_redo_lock']='passed'
    clip=report/'fixture.mp4'
    subprocess.run([str(ffmpeg),'-hide_banner','-loglevel','error','-y','-f','lavfi','-i','testsrc2=size=640x360:rate=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','3','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',str(clip)],check=True,timeout=30)
    add_source('Video / audio file','QA media',str(clip)); time.sleep(3)
    assert 'failed' not in control('sourceHealth').window_text().lower(),control('sourceHealth').window_text()
    first=control('previewCanvas').capture_as_image();time.sleep(.6);second=control('previewCanvas').capture_as_image()
    assert np.mean(abs(np.asarray(first).astype(float)-np.asarray(second)))>1
    press('mediaPlayback');dialog=window.child_window(title='Media playback',control_type='Window',found_index=0)
    enabled(control('mediaPaused',dialog),True);time.sleep(.7)
    first=control('previewCanvas').capture_as_image();time.sleep(.6);second=control('previewCanvas').capture_as_image()
    assert np.mean(abs(np.asarray(first).astype(float)-np.asarray(second)))<.5
    enabled(control('mediaPaused',dialog),False); __import__('win32gui').PostMessage(dialog.handle,0x10,0,0);time.sleep(.4)
    results['media_play_pause_loop']='passed'
    press('mediaPlayback');dialog=window.child_window(title='Media playback',control_type='Window',found_index=0)
    enabled(control('mediaLoop',dialog),False)
    __import__('win32gui').PostMessage(dialog.handle,0x10,0,0);time.sleep(4)
    assert 'Playback finished' in control('sourceHealth').window_text()
    press('mediaPlayback');dialog=window.child_window(title='Media playback',control_type='Window',found_index=0)
    assert control('mediaPaused',dialog).get_toggle_state()==1
    enabled(control('mediaPaused',dialog),False)
    enabled(control('mediaLoop',dialog),True)
    __import__('win32gui').PostMessage(dialog.handle,0x10,0,0);time.sleep(.3)
    first=control('previewCanvas').capture_as_image();time.sleep(.6);second=control('previewCanvas').capture_as_image()
    assert np.mean(abs(np.asarray(first).astype(float)-np.asarray(second)))>1
    assert 'failed' not in control('sourceHealth').window_text().lower()
    results['media_resume_after_end']='passed'
    combo('canvasFormat','1920x1080 / 60 fps')
    path=report/f'program-{int(time.time())}.mkv'
    press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path))
    dialog.child_window(title='Save',control_type='Button').invoke();wait_record(True)
    time.sleep(7);press('studioRecord');wait_record(False)
    result=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(path),'-f','null','-'],capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stderr
    assert '1920x1080' in result.stderr and '60 fps' in result.stderr,result.stderr
    assert 'Audio: aac' in result.stderr,result.stderr
    results['native_1080p60_recording']={'path':str(path),'decoded':True}
    window.capture_as_image().save(str(report/'workflows.png'))
finally:
    (report/'workflow.json').write_text(json.dumps(results,indent=2))
