"""Visible portrait presets, stacked interview layout, crop and show persistence."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np
results={}
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
add_source('Color','QA upper','#d02020');add_source('Color','QA lower','#2020d0');press('layout-Stacked')
for choice,index in [('1080x1920 / 30 fps',4),('720x1280 / 30 fps',5),('720x1280 / 60 fps',6),('1080x1920 / 60 fps',7)]:
    choose_dialog(window,'canvasFormat',choice);time.sleep(.7)
    frame=control('previewCanvas').capture_as_image();w=min(frame.width,frame.height*9/16);h=w*16/9;left=(frame.width-w)/2;top=(frame.height-h)/2
    for x in (.1,.5,.9):
        near(frame.getpixel((int(left+w*x),int(top+h/4)))[:3],(208,32,32));near(frame.getpixel((int(left+w*x),int(top+3*h/4)))[:3],(32,32,208))
    saved=json.loads(show_path.read_text());assert saved['canvasFormat']==index
    layers=next(s for s in saved['scenes'] if s['name']=='Main')['layers']
    assert [(l['x'],l['y'],l['w'],l['h']) for l in layers]==[(0,0,1,.5),(0,.5,1,.5)],layers
    window.capture_as_image().save(str(report/('portrait-'+str(index)+'.png')));results[choice]='stacked portrait picture and saved format verified'
# Staging a portrait show must preserve separate Preview and Program behavior.
studio_mode(True);scene('Break');before=np.asarray(control('programCanvas').capture_as_image());assert before.std()>10
press('takeScene');time.sleep(.5);after=np.asarray(control('programCanvas').capture_as_image());assert not np.array_equal(before,after)
scene('Main');press('fadeScene');time.sleep(.7)
source_row('QA upper').select();choose_dialog(window,'layerFit','Fill');control('cropLeft').iface_range_value.SetValue(10);press('takeScene')
time.sleep(.7);assert abs(control('cropLeft').iface_range_value.CurrentValue-10)<.1
path=report/'portrait.mixer.json';press('exportShow');dialog=window.child_window(title='Save show (includes source URLs and invite links)',control_type='Window');dialog.wait('visible')
dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').click_input();time.sleep(.5)
choose_dialog(window,'canvasFormat','1280x720 / 30 fps');press('importShow');dialog=window.child_window(title='Open show',control_type='Window');dialog.wait('visible')
filename=dialog.child_window(title='File name:',control_type='Edit');filename.set_edit_text(str(path));filename.set_focus();filename.type_keys('{ENTER}');time.sleep(1)
assert '1080x1920 / 60 fps' in control('canvasFormat').selected_text();scene('Main');source_row('QA upper').select()
assert control('layerFit').selected_text()=='Fill' and abs(control('cropLeft').iface_range_value.CurrentValue-10)<.1
results['persistence']='1080x1920/60, crop, Fill and stacked geometry survived export/open'
(report/'portrait.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
