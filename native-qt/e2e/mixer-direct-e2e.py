"""Packaged single-canvas workflows, including decoded recorded output.

Start with the standard show and the default single-canvas preference. Uses a
private browser fixture. Leaves the show cleaned and Studio mode disabled.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np
import psutil
import hashlib
import win32gui
import win32process
from pywinauto import mouse

results = {'exeSha256':hashlib.sha256(Path(psutil.Process(args.pid).exe()).read_bytes()).hexdigest()}
overlay_color = '#e02020'
def restore(hwnd, _):
    if win32process.GetWindowThreadProcessId(hwnd)[1] == args.pid and win32gui.GetWindowText(hwnd) == 'Desktop Studio - Powered by VDO.Ninja':
        win32gui.ShowWindow(hwnd,9)
    return True
win32gui.EnumWindows(restore,None)
window.set_focus()
window.child_window(title='Studio', control_type='TabItem').click_input()

def pixel(x, y):
    image = control('previewCanvas').capture_as_image().convert('RGB')
    scale = min(image.width/1280, image.height/720)
    return image.getpixel((round((image.width-1280*scale)/2+x*1280*scale),
                           round((image.height-720*scale)/2+y*720*scale)))

def coordinate(name, value):
    control('layer'+name).iface_range_value.SetValue(value)
    time.sleep(.3)

def point(x, y):
    rect=control('previewCanvas').rectangle()
    scale=min(rect.width()/1280,rect.height()/720)
    return (round(rect.left+(rect.width()-1280*scale)/2+x*1280*scale),
            round(rect.top+(rect.height()-720*scale)/2+y*720*scale))

def visible(name):
    return any(c.element_info.automation_id.endswith('.'+name) and c.is_visible()
               for c in window.descendants())

def visibility(name):
    row = source_row(name)
    row.click_input(coords=(15, row.rectangle().height()//2))
    time.sleep(.4)

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'''<!doctype html><style>body{margin:0;background:#24b46a}</style>
<script>let a=new AudioContext(),o=a.createOscillator(),g=a.createGain();
o.frequency.value=440;g.gain.value=.12;o.connect(g);g.connect(a.destination);o.start();a.resume();</script>'''
        if self.path.startswith('/overlay'):
            body=b'''<!doctype html><style>body{margin:0;background:transparent}#mark{position:absolute;right:0;bottom:0;width:25vw;height:25vh}</style>
<div id=mark></div><script>setInterval(async()=>{mark.style.background=await(await fetch('/state')).text()},100)</script>'''
        elif self.path.startswith('/state'): body=overlay_color.encode()
        self.send_response(200); self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self, *_): pass

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    assert not control('studioMode').get_toggle_state(), 'First run should default to single canvas'
    assert visible('previewCanvas') and not visible('programCanvas')
    assert not visible('takeScene') and not visible('fadeScene')
    results['default_single_canvas'] = 'passed'
    scene('Main'); near(pixel(.7,.7), (16,23,34))
    add_source('Browser /', 'QA direct browser', f'http://127.0.0.1:{server.server_port}/')
    time.sleep(3); near(pixel(.7,.7), (36,180,106))
    window.capture_as_image().save(str(report/'single-canvas.png'))
    press('socialChat')
    prompt=window.child_window(title='Social Stream Ninja',control_type='Window')
    prompt.child_window(control_type='Edit').set_edit_text(f'http://127.0.0.1:{server.server_port}/overlay')
    prompt.child_window(title='OK',control_type='Button').invoke(); time.sleep(4)
    near(pixel(.9,.9),(224,32,32)); near(pixel(.2,.2),(36,180,106))
    time.sleep(2); overlay_color='#2040e0'; time.sleep(1)
    near(pixel(.9,.9),(32,64,224)); near(pixel(.2,.2),(36,180,106))
    results['transparent_cache_updates_and_alpha']='passed'
    source_row('Social chat').select(); press('removeSource')
    source_row('QA direct browser').select()
    coordinate('W',40); coordinate('H',40)
    mouse.press(coords=point(.2,.2)); mouse.move(coords=point(.4,.3)); mouse.release(coords=point(.4,.3)); time.sleep(.5)
    assert 18 < control('layerX').iface_range_value.CurrentValue < 22
    assert 8 < control('layerY').iface_range_value.CurrentValue < 12
    near(pixel(.3,.2),(36,180,106)); near(pixel(.1,.1),(16,23,34))
    press('programWindow')
    popout=app.window(title='Desktop Studio - Program'); popout.wait('visible'); popout.set_focus(); time.sleep(.5)
    # Sample the actual client area, excluding the native window decoration.
    from PIL import ImageGrab
    client=win32gui.GetClientRect(popout.handle)
    left,top=win32gui.ClientToScreen(popout.handle,(0,0))
    clean=ImageGrab.grab((left,top,left+client[2],top+client[3])).convert('RGB')
    near(clean.getpixel((round(client[2]*.3),round(client[3]*.2))),(36,180,106))
    near(clean.getpixel((round(client[2]*.1),round(client[3]*.1))),(16,23,34))
    assert not np.any(np.max(np.abs(np.asarray(clean).astype(int)-(124,168,255)),axis=2)<10), 'Selection outline leaked into Program window'
    popout.close(); window.set_focus(); press('layout-Full')
    results['direct_drag_and_clean_program_window']='passed'
    recording = report/f'direct-{int(time.time())}.mkv'
    press('studioRecord')
    dialog=window.child_window(title='Record Program', control_type='Window'); dialog.wait('visible')
    edit=next(c for c in dialog.descendants(control_type='Edit') if c.is_visible() and c.element_info.automation_id=='1001')
    edit.set_edit_text(str(recording)); dialog.child_window(title='Save',control_type='Button').invoke()
    wait_recording(True)
    phases=[]
    def phase(name):
        phases.append(name); time.sleep(2.5)
    phase('green_audio')
    visibility('QA direct browser'); near(pixel(.7,.7),(16,23,34)); phase('hidden_silent')
    visibility('QA direct browser'); coordinate('W',40); coordinate('H',40)
    near(pixel(.2,.2),(36,180,106)); near(pixel(.7,.7),(16,23,34)); phase('resized_audio')
    results['live_visibility_geometry'] = 'passed'
    scene('Break'); near(pixel(.7,.7),(24,54,87)); phase('break_silent')
    scene('Main'); near(pixel(.2,.2),(36,180,106))
    studio_mode(True); assert visible('programCanvas') and visible('takeScene')
    scene('Break'); near(color('programCanvas'),(16,23,34))
    phase('staged_break_program_still_green')
    press('fadeScene'); time.sleep(.7); near(color('programCanvas'),(24,54,87))
    scene('Main'); studio_mode(False); near(pixel(.2,.2),(36,180,106))
    results['studio_isolation_and_return_to_direct'] = 'passed'
    source_row('QA direct browser').select(); press('layout-Full')
    phase('full_green_audio')
    source_row('QA direct browser').select(); press('removeSource')
    near(pixel(.7,.7),(16,23,34)); phase('removed_silent')
    results['remove_on_air_source'] = 'passed'
    press('studioRecord'); wait_recording(False); time.sleep(2)
    ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
    raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-an','-vf','scale=160:90','-pix_fmt','rgb24','-f','rawvideo','pipe:1'])
    frames=np.frombuffer(raw,dtype=np.uint8).reshape(-1,90,160,3)
    raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(recording),'-vn','-ac','1','-ar','48000','-f','f32le','pipe:1'])
    audio=np.frombuffer(raw,dtype=np.float32)
    def match(values, rgb): return np.max(np.abs(values.astype(int)-rgb),axis=-1)<10
    green=match(frames[:,65,120],(36,180,106))
    blank=match(frames[:,65,120],(16,23,34)) & match(frames[:,18,32],(16,23,34))
    resized=match(frames[:,18,32],(36,180,106)) & match(frames[:,65,120],(16,23,34))
    pause=match(frames[:,65,120],(24,54,87))
    for name,mask in [('green',green),('blank',blank),('resized',resized),('break',pause)]:
        assert mask.sum()>55,(name,int(mask.sum()))
    # Verify that the rendered hide/remove/scene changes also gated actual audio.
    rms=np.array([np.sqrt(np.mean(audio[i*1600:(i+1)*1600]**2)) for i in range(min(len(frames),len(audio)//1600))])
    assert rms[green[:len(rms)]].max()>.06
    for mask in [blank,pause]:
        indices=np.flatnonzero(mask[:len(rms)])
        quiet=sum(rms[i]<.002 for i in indices if i>0 and mask[i-1])
        assert quiet>45,quiet
    results['decoded_output']={'frames':len(frames),'audiblePeakRms':float(rms.max()),'blankFrames':int(blank.sum()),'resizedFrames':int(resized.sum()),'recording':str(recording)}
    # Isolated --studio-show sessions deliberately do not write global UI preferences.
    # Run this check against a backed-up normal profile to cover persistence.
    if 'STUDIO_E2E_SHOW' not in os.environ:
        # Persist both layout choices across real process restarts.
        exe=psutil.Process(args.pid).exe()
        for mode in (True,False):
            studio_mode(mode); window.set_focus(); window.type_keys('^q')
            psutil.Process(app.process).wait(15)
            child=subprocess.Popen([exe],creationflags=subprocess.CREATE_NO_WINDOW)
            (report/'app.pid').write_text(str(child.pid))
            app=Application(backend='uia').connect(process=child.pid,timeout=20)
            window=app.window(title='Desktop Studio - Powered by VDO.Ninja'); window.wait('visible',timeout=25)
            control.__defaults__=(window,); press.__defaults__=(window,)
            window.set_focus(); time.sleep(1)
            assert bool(control('studioMode').get_toggle_state())==mode
        results['mode_restart_persistence']='passed'
        (report/'app.pid').write_text(str(app.process))
    else:
        results['mode_restart_persistence']='not applicable to isolated profile; separate normal-profile check required'
    scene('Opening')
finally:
    server.shutdown()
    (report/'direct-workflow.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2),flush=True)
