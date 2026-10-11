"""Exercise cached scene invalidation and canvas selection in the packaged app."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
from pywinauto import mouse

results = {}

def coordinate(name, value):
    control('layer'+name).iface_range_value.SetValue(value)
    time.sleep(.2)

def pixel(name, x, y):
    image = control(name).capture_as_image().convert('RGB')
    scale = min(image.width/1280, image.height/720)
    px = (image.width-1280*scale)/2 + x*1280*scale
    py = (image.height-720*scale)/2 + y*720*scale
    return image.getpixel((round(px),round(py)))

def point(x, y):
    rect = control('previewCanvas').rectangle()
    scale = min(rect.width()/1280, rect.height()/720)
    return (round(rect.left+(rect.width()-1280*scale)/2+x*1280*scale),
            round(rect.top+(rect.height()-720*scale)/2+y*720*scale))

window.child_window(title='Studio',control_type='TabItem').click_input()
studio_mode()
try:
    scene('Opening'); press('takeScene'); scene('Main')
    add_source('Color','QA cache red','#e02020'); press('takeScene')
    near(pixel('programCanvas',.7,.7),(224,32,32))
    coordinate('W',40); coordinate('H',40)
    near(pixel('previewCanvas',.7,.7),(16,23,34))
    near(pixel('programCanvas',.7,.7),(224,32,32))
    press('takeScene'); near(pixel('programCanvas',.7,.7),(16,23,34))
    results['geometry_preview_and_program_isolation'] = 'passed'
    mouse.press(coords=point(.8,.8)); mouse.move(coords=point(.9,.9)); mouse.release(coords=point(.9,.9)); time.sleep(.3)
    assert control('layerX').iface_range_value.CurrentValue == 0
    assert control('layerY').iface_range_value.CurrentValue == 0
    results['empty_canvas_drag_does_not_move_selection'] = 'passed'
    add_source('Microphone / audio input','QA cache mic',device='HD Pro Webcam C920')
    mouse.click(coords=point(.2,.2)); time.sleep(.3)
    assert control('mixerSources').get_selection()[0].name.startswith('QA cache red')
    mouse.press(coords=point(.2,.2)); mouse.move(coords=point(.4,.3)); mouse.release(coords=point(.4,.3)); time.sleep(.3)
    assert control('layerX').iface_range_value.CurrentValue > 15
    results['microphone_does_not_intercept_video_drag'] = 'passed'
    add_source('Color','QA cache blue','#2040e0'); press('takeScene')
    near(pixel('programCanvas',.3,.25),(32,64,224))
    press('layerBack'); press('layerBack')
    near(pixel('previewCanvas',.3,.25),(224,32,32))
    near(pixel('programCanvas',.3,.25),(32,64,224))
    press('takeScene'); near(pixel('programCanvas',.3,.25),(224,32,32))
    results['layer_order_invalidation'] = 'passed'
    source_row('QA cache blue').select(); press('editSource')
    dialog=window.child_window(title='Edit source',control_type='Window')
    control('sourceValue',dialog).set_edit_text('#20d050')
    dialog.child_window(title='OK',control_type='Button').invoke();time.sleep(.5)
    near(pixel('programCanvas',.8,.8),(32,208,80))
    results['source_replacement_invalidation'] = 'passed'
    scene('Break');press('fadeScene');time.sleep(.6)
    near(pixel('programCanvas',.7,.7),(24,54,87))
    results['fade_to_cached_scene'] = 'passed'
    remove_sources(('QA cache',));scene('Opening');press('takeScene')
    results['cleanup'] = 'passed'
finally:
    (report/'compositor-workflow.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
