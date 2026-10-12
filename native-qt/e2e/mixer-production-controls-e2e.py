"""Visible 0.8 production controls; decode actual recordings for routing evidence."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np, urllib.request, ctypes, psutil
from pywinauto.keyboard import send_keys
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
results={};foreground=None
def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text())
    request=urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})
    return json.load(urllib.request.urlopen(request))
def ff(*args):
    r=subprocess.run([str(ffmpeg),*map(str,args)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=60)
    assert r.returncode==0,r.stderr.decode(errors='replace');return r.stdout
def record(name):
    path=report/(name+'.mkv');press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    field=dialog.child_window(title='File name:',control_type='Edit');field.set_edit_text(str(path));field.set_focus();field.type_keys('{ENTER}');wait_recording(True)
    time.sleep(3.5);press('studioRecord');wait_recording(False)
    ff('-v','error','-i',path,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-')
    pcm=np.frombuffer(ff('-v','error','-i',path,'-vn','-ac','2','-ar','48000','-f','f32le','-'),np.float32).reshape(-1,2)[48000:120000]
    # Measure short complete-cycle blocks so a file-loop phase reset cannot
    # cancel an otherwise present tone across the whole recording window.
    blocks=pcm[:len(pcm)//4800*4800].reshape(-1,4800,2)
    values=[[float(np.median(abs(blocks[:,:,c]@np.exp(-2j*np.pi*f*np.arange(4800)/48000))*2/4800)) for f in (440,880)] for c in (0,1)]
    results[name]=values;return np.array(values)
def track():return next(s for s in state()['sources'] if s['name']=='QA stereo')
try:
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
    fixture=report/'stereo-44100.wav'
    ff('-v','error','-y','-f','lavfi','-i','aevalsrc=0.12*sin(2*PI*440*t)|0.12*sin(2*PI*880*t):s=44100:d=8',fixture)
    add_source('Video / audio file','QA stereo',str(fixture));time.sleep(1);sid=track()['id']
    for mode,label,expected in [(0,'Stereo',[[.12,0],[0,.12]]),(1,'Mono (L + R)',[[.06,.06],[.06,.06]]),(2,'Left to both',[[.12,0],[.12,0]]),(3,'Right to both',[[0,.12],[0,.12]])]:
        choose_dialog(window,'audio_channels_'+sid,label);time.sleep(.5);assert track()['channelMode']==mode
        actual=record('channels-'+str(mode));assert np.max(abs(actual-np.array(expected)))<.016,(label,actual)
        assert track()['channelMode']==mode
    enabled(control('audio_program_'+sid),False);assert np.max(record('off-air'))<.001
    enabled(control('audio_program_'+sid),True);press('privateBreak');assert state()['private_break'];assert np.max(record('private-break'))<.001
    window.capture_as_image().save(str(report/'private-break.png'));press('privateBreak');assert not state()['private_break'];assert np.max(record('resumed'))>.09
    assert state()['recording']['remaining_seconds_estimate']>0
    # Export through the same file dialogs a user uses after a show.
    press('recordingFiles');item=next(c for top in app.windows() for c in top.descendants(control_type='MenuItem') if c.window_text()=='Export recording as MP4...');item.click_input()
    dialog=window.child_window(title='Choose recording to export',control_type='Window');dialog.wait('visible');field=dialog.child_window(title='File name:',control_type='Edit');field.set_edit_text(str(report/'channels-0.mkv'));field.set_focus();field.type_keys('{ENTER}')
    dialog=window.child_window(title='Export MP4 (original recording preserved)',control_type='Window');dialog.wait('visible');dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(report/'export.mp4'));dialog.child_window(title='Save',control_type='Button').click_input()
    deadline=time.time()+30
    while state()['recording']['exporting'] and time.time()<deadline:time.sleep(.2)
    assert state()['recording']['export_status'].startswith('MP4 saved:'),state()['recording'];ff('-v','error','-i',report/'export.mp4','-f','null','-')
    results['mp4Export']='GUI export completed and MP4 decoded; original MKV retained'
    # Configure actual OS hotkeys, then focus an unrelated visible window.
    press('globalHotkeys');dialog=window.child_window(title='Global hotkeys',control_type='Window');dialog.wait('visible')
    enabled(control('globalHotkeysEnabled',dialog),True);dialog.child_window(title='Save',control_type='Button').click_input();time.sleep(.5)
    foreground=subprocess.Popen([__import__('sys')._base_executable,'-c',"import tkinter as t; w=t.Tk();w.title('QA foreground game');w.geometry('420x180+80+80');t.Label(w,text='Studio must respond while this window has focus').pack();w.mainloop()"],creationflags=subprocess.CREATE_NO_WINDOW)
    other=Application(backend='uia').connect(process=foreground.pid,timeout=15).window(title='QA foreground game');other.wait('visible');other.set_focus()
    def hotkey(keys):
        other.set_focus();send_keys(keys);time.sleep(.5)
        assert ctypes.windll.user32.GetForegroundWindow()==other.handle,'Hotkey stole foreground focus'
    hotkey('^%b');assert state()['private_break'];hotkey('^%b');assert not state()['private_break']
    hotkey('^%m');assert state()['microphones_muted'];hotkey('^%m');assert not state()['microphones_muted']
    hotkey('^%1');s=state();assert s['program']==s['scenes'][0]['id'];hotkey('^%2');s=state();assert s['program']==s['scenes'][1]['id']
    other.set_focus();send_keys('{VK_CONTROL down}{VK_MENU down}{t down}');time.sleep(.4);assert state()['private_talkback']
    send_keys('{t up}{VK_MENU up}{VK_CONTROL up}');time.sleep(.2);assert not state()['private_talkback']
    results['globalHotkeys']='break/resume, microphone mute/unmute, scene selection and held talkback passed with another app focused'
    foreground.terminate();foreground.wait();foreground=None
    window.set_focus();press('globalHotkeys');dialog=window.child_window(title='Global hotkeys',control_type='Window');enabled(control('globalHotkeysEnabled',dialog),False);dialog.child_window(title='Save',control_type='Button').click_input()
    window.capture_as_image().save(str(report/'production-controls.png'))
finally:
    send_keys('{t up}{VK_MENU up}{VK_CONTROL up}')
    if foreground:foreground.terminate()
    (report/'production-controls.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results),flush=True)
