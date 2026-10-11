"""Exercise physical C920 controls, capture modes, recording and saved settings.

Run from native-qt against the packaged app and a disposable starter show. Saves
private camera images/recording locally, and restores the original driver controls.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
import os, sys
import numpy as np
import win32gui, win32process, win32con
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
results={}; original={}; dialog=None
show=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/studio.mixer.json'

def restore_window():
    def restore(h,_):
        if win32process.GetWindowThreadProcessId(h)[1]==app.process and win32gui.GetWindowText(h).startswith('Video Mixer - Powered'):
            win32gui.ShowWindow(h,win32con.SW_RESTORE)
        return True
    win32gui.EnumWindows(restore,None)
    window.wait('visible');window.set_focus()

def open_settings():
    global dialog
    press('cameraSettings')
    dialog=window.child_window(title='Camera settings - QA C920',control_type='Window');dialog.wait('visible')
    time.sleep(.5)

def close_settings():
    global dialog
    # Use the dialog's Close button, excluding the native title-bar control.
    next(c for c in dialog.descendants(control_type='Button') if c.window_text()=='Close' and c.element_info.automation_id).invoke()
    dialog.wait_not('visible');dialog=None;time.sleep(.3)

def value(id):return int(control('cameraValue_'+id,dialog).iface_range_value.CurrentValue)
def auto(id):return bool(control('cameraAuto_'+id,dialog).get_toggle_state())
def applied():
    deadline=time.time()+8
    while time.time()<deadline:
        if control('cameraApplyMode',dialog).is_enabled() and not control('cameraSettingsStatus',dialog).window_text().startswith('Applying '):break
        time.sleep(.1)
    status=control('cameraSettingsStatus',dialog).window_text()
    assert not any(word in status.lower() for word in ['rejected','could not','unavailable','range and step','does not support','applying ']),status
    return status

def set_auto(id,on):
    box=control('cameraAuto_'+id,dialog)
    if auto(id)!=on:box.toggle();time.sleep(.15);applied()
    assert auto(id)==on

def set_value(id,wanted):
    set_auto(id,False)
    control('cameraValue_'+id,dialog).iface_range_value.SetValue(wanted)
    time.sleep(.15);status=applied()
    assert value(id)==wanted,(id,wanted,value(id),status)

def pixels():
    time.sleep(1.5)
    return np.asarray(control('cameraPreview',dialog).capture_as_image().convert('RGB')).astype(float)

def choose_mode(text):
    combo=control('cameraMode',dialog);combo.expand();time.sleep(.2)
    # Use Qt's keyboard search; its popup UIA tree only exposes visible rows.
    from pywinauto.keyboard import send_keys
    send_keys('{HOME}');send_keys(text,with_spaces=True);send_keys('{ENTER}')
    time.sleep(.3)
    assert combo.selected_text()==text,combo.selected_text()
    press('cameraApplyMode',dialog);time.sleep(2)
    actual=control('cameraActiveMode',dialog).window_text()
    assert text in actual,(text,actual,control('cameraSettingsStatus',dialog).window_text())
    print(actual,flush=True)
    return actual

restore_window()
window.child_window(title='Studio',control_type='TabItem').click_input()
scene('Break');press('takeScene');remove_sources(('QA C920',))
scene('Main');add_source('Camera / webcam','QA C920',device='HD Pro Webcam C920');press('takeScene');time.sleep(3)
recording=report/f'camera-controls-{int(time.time())}.mkv'
configure('Record to file (MKV)',recording);press('studioStartOutput');wait_output(True)
open_settings()
try:
    for box in dialog.descendants(control_type='Spinner'):
        name=box.element_info.automation_id.rsplit('.',1)[-1]
        if not name.startswith('cameraValue_'):continue
        id=name.removeprefix('cameraValue_');rv=box.iface_range_value
        original[id]={'value':value(id),'auto':auto(id),'min':int(rv.CurrentMinimum),'max':int(rv.CurrentMaximum)}
    (report/'camera-original-controls.json').write_text(json.dumps(original,indent=2))
    results['controls']=original;results['initialMode']=control('cameraActiveMode',dialog).window_text()
    assert all(id in original for id in ['zoom','exposure','brightness','focus','white_balance'])
    baseline=pixels();assert baseline.std()>15,'Camera has no useful image'
    set_auto('exposure',False)
    set_value('brightness',64);dark=pixels()
    set_value('brightness',192);bright=pixels()
    assert bright.mean()>dark.mean()+15,(dark.mean(),bright.mean())
    results['brightness']={'lowMean':float(dark.mean()),'highMean':float(bright.mean())}
    press('cameraReset_brightness',dialog);time.sleep(.2);applied();assert value('brightness')==128
    results['resetBrightness']='128 (driver default)'
    normal=pixels();set_value('zoom',150);zoomed=pixels()
    assert np.abs(zoomed-normal).mean()>5
    results['zoom']={'readBack':value('zoom'),'meanImageDifference':float(np.abs(zoomed-normal).mean())}
    set_value('zoom',100)
    set_value('exposure',-6);assert not auto('exposure');set_auto('exposure',True)
    set_auto('focus',False);set_value('focus',25);set_auto('focus',True)
    set_auto('white_balance',False);set_value('white_balance',4500);set_auto('white_balance',True)
    results['manualAndAuto']=['exposure','focus','white balance']
    results['captureModes']=[choose_mode('1920x1080 @ 30 fps'),choose_mode('1280x720 @ 30 fps'),choose_mode('640x480 @ 15 fps')]
    set_value('brightness',176);set_value('zoom',150)
    dialog.capture_as_image().save(str(report/'camera-settings.png'))
    close_settings();press('studioStartOutput');wait_output(False)
    time.sleep(1);press('reconnectSource');time.sleep(3);open_settings()
    assert value('brightness')==176 and value('zoom')==150
    assert '640x480 @ 15 fps' in control('cameraActiveMode',dialog).window_text()
    results['reconnectRestoresControls']='passed'
    close_settings();time.sleep(1)
    saved=json.loads(show.read_text());source=next(s for s in saved['sources'] if s['name']=='QA C920')
    assert source['cameraControls']['brightness']['value']==176 and source['cameraWidth']==640 and abs(source['cameraFps']-15)<.001
    results['savedShow']='camera mode and explicitly changed controls persisted'
    # Restart the shipped portable app to verify actual hardware settings are reapplied.
    window.set_focus();window.type_keys('^q');app.wait_for_process_exit(timeout=15)
    startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=win32con.SW_HIDE
    child=subprocess.Popen([str(Path('dist/video-mixer-portable/video-mixer.exe').resolve())],startupinfo=startup)
    (report/'app.pid').write_text(str(child.pid));time.sleep(3)
    app=Application(backend='uia').connect(process=child.pid);window=app.window(title='Video Mixer - Powered by VDO.Ninja')
    # Helpers bind window as a default argument; rebind after restarting.
    control.__defaults__=(window,);press.__defaults__=(window,)
    restore_window();control('mixerSources').wheel_mouse_input(wheel_dist=-3)
    source_row('QA C920').click_input(coords=(100,12));time.sleep(.3);open_settings()
    assert value('brightness')==176 and value('zoom')==150
    assert '640x480 @ 15 fps' in control('cameraActiveMode',dialog).window_text()
    results['restartRestoresControls']='passed'
    ffmpeg=Path('dist/video-mixer-portable/ffmpeg/bin/ffmpeg.exe').resolve()
    metadata=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(recording)],capture_output=True,text=True).stderr
    assert 'Video: h264' in metadata and '1280x720' in metadata and '30 fps' in metadata,metadata
    frames=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-an','-vf','fps=1,scale=64:36','-pix_fmt','gray','-f','rawvideo','pipe:1']),dtype=np.uint8).reshape(-1,36,64)
    assert len(frames)>15 and float(np.std(frames))>15
    results['recording']={'path':str(recording),'decodedSampleFrames':len(frames),'frameStdDev':float(np.std(frames)),'format':'H.264 1280x720 30 fps'}
finally:
    if dialog is not None and dialog.exists():
        for id,setting in original.items():
            if id not in ['zoom','exposure','focus','brightness','white_balance']:continue
            if setting['auto']:set_auto(id,True)
            elif control('cameraValue_'+id,dialog).is_enabled() or control('cameraAuto_'+id,dialog).is_enabled():set_value(id,setting['value'])
        results['originalControlsRestored']=True
        close_settings()
    if control('studioStartOutput').window_text() in ('STOP','STOP RECORDING'):
        press('studioStartOutput');wait_output(False)
    (report/'camera-workflow.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
