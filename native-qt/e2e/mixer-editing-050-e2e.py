"""Exercise composition, drag handles, shortcuts and show persistence in the app."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
from PIL import Image,ImageDraw
from pywinauto import mouse
import numpy as np,os
results={};show=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/studio.mixer.json'
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))

def spin(name,value):
    c=control(name);c.iface_range_value.SetValue(value);time.sleep(.2)

def geometry():return {name:control('layer'+name).iface_range_value.CurrentValue for name in ('X','Y','W','H')}

try:
    fixture=Image.new('RGBA',(640,360),(0,0,0,0));draw=ImageDraw.Draw(fixture)
    draw.rectangle((40,30,319,179),fill=(255,0,0,160));draw.rectangle((320,30,599,179),fill=(0,255,0,255))
    draw.rectangle((40,180,319,329),fill=(0,0,255,255));draw.rectangle((320,180,599,329),fill=(255,255,255,96))
    path=report/'quadrants.png';fixture.save(path)
    add_source('Color','QA background','#246090');add_source('Image','QA quadrants',str(path))
    spin('layerW',50);spin('layerH',80);spin('layerX',25);spin('layerY',10)
    spin('cropLeft',12);spin('cropTop',16);spin('cropRight',7);spin('cropBottom',5)
    studio_mode(True)
    comparisons={}
    for mode in ('Fit','Fill','Stretch'):
        choose_dialog(window,'layerFit',mode);press('takeScene')
        choose_dialog(window,'compositorBackend','CPU composition');time.sleep(.3)
        cpu=np.asarray(control('programCanvas').capture_as_image()).astype(float)
        choose_dialog(window,'compositorBackend','GPU composition');time.sleep(.3)
        gpu=np.asarray(control('programCanvas').capture_as_image()).astype(float)
        diff=np.abs(cpu-gpu);assert diff.mean()<1.2 and np.percentile(diff,99)<6,(mode,diff.mean(),np.percentile(diff,99))
        comparisons[mode]={'meanPixelDifference':float(diff.mean()),'p99Difference':float(np.percentile(diff,99))}
    results['gpu_cpu_alpha_crop_scaling']=comparisons
    press('layout-Full');spin('layerX',10);spin('layerY',10);spin('layerW',50);spin('layerH',50)
    before=geometry();canvas=control('previewCanvas');r=canvas.rectangle();width=min(r.width(),r.height()*16/9);height=width*9/16
    left=r.left+(r.width()-width)/2;top=r.top+(r.height()-height)/2
    start=(round(left+.6*width),round(top+.6*height));end=(round(left+.8*width),round(top+.8*height))
    mouse.press(coords=start)
    for step in range(1,6):
        mouse.move(coords=tuple(round(a+(b-a)*step/5) for a,b in zip(start,end)));time.sleep(.1)
    mouse.release(coords=end);time.sleep(.4)
    resized=geometry();assert resized['W']>65 and resized['H']>65,(before,resized)
    # Put keyboard focus on a non-text control so Ctrl+Z addresses the show.
    control('snapLayers').set_focus();window.type_keys('^z');time.sleep(.3)
    restored=geometry();assert all(abs(restored[n]-before[n])<.3 for n in before),(before,restored)
    window.type_keys('^y');time.sleep(.3);assert geometry()['W']>65
    enabled(control('layerLock'),True);assert not control('layerW').is_enabled()
    results['drag_resize_keyboard_undo_redo_lock']='passed'
    control('snapLayers').set_focus();window.type_keys('^1');time.sleep(.3)
    assert control('sceneList').get_selection()[0].name=='Opening'
    window.type_keys('^2');time.sleep(.3)
    results['scene_shortcuts']='passed'
    source_row('QA quadrants').select();time.sleep(.2)
    path=report/'saved.mixer.json';press('exportShow');dialog=window.child_window(title='Save show (includes source URLs and invite links)',control_type='Window')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').invoke();time.sleep(.5)
    expected=json.loads(path.read_text());layer=next(s for s in expected['scenes'] if s['name']=='Main')['layers'][-1]
    assert layer['locked'] and layer['fit']=='stretch' and abs(layer['cropX']-.12)<.001,layer
    scene('Break');press('takeScene');remove('QA quadrants');press('undoEdit');assert source_row('QA quadrants').is_visible()
    results['undo_removed_source']='passed'
    press('importShow');dialog=window.child_window(title='Open show',control_type='Window')
    edit=dialog.child_window(title='File name:',control_type='Edit');edit.set_edit_text(str(path));edit.set_focus();edit.type_keys('{ENTER}');time.sleep(1)
    scene('Main');source_row('QA quadrants').select();time.sleep(.3)
    assert control('layerLock').get_toggle_state() and abs(control('cropLeft').iface_range_value.CurrentValue-12)<.1
    assert control('layerFit').selected_text()=='Stretch'
    results['saved_show_roundtrip']='passed';window.capture_as_image().save(str(report/'editing.png'))
finally:
    (report/'editing.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
