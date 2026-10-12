"""Use scene, appearance and portrait-output controls in the packaged GUI."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np
results={}
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False)

def prompt(command,title,value):
    press(command);dialog=window.child_window(title=title,control_type='Window');dialog.wait('visible')
    dialog.child_window(control_type='Edit').set_edit_text(value)
    dialog.child_window(title='OK',control_type='Button').click_input()
    time.sleep(.3)

try:
    prompt('addScene','New scene','QA portrait')
    add_source('Video / audio file','QA workspace media',str(media_fixture()))
    prompt('renameScene','Rename scene','QA renamed scene')
    press('duplicateScene');scene('QA renamed scene copy')
    assert source_row('QA workspace media').is_visible()
    press('removeScene');scene('QA renamed scene')
    results['sceneCreateRenameDuplicateDelete']='passed'
    source_row('QA workspace media').select();press('mediaPlayback')
    playback=window.child_window(title='Media playback',control_type='Window');playback.wait('visible')
    enabled(control('mediaPaused',playback),True);playback.type_keys('{ESC}');time.sleep(.5)
    paused=np.asarray(control('previewCanvas').capture_as_image());time.sleep(.8)
    assert np.array_equal(paused,np.asarray(control('previewCanvas').capture_as_image()))
    press('mediaPlayback');enabled(control('mediaLoop',playback),False);press('mediaRestart',playback)
    playback.type_keys('{ESC}');time.sleep(.7)
    first=np.asarray(control('previewCanvas').capture_as_image());time.sleep(.5)
    assert not np.array_equal(first,np.asarray(control('previewCanvas').capture_as_image()))
    time.sleep(3);press('mediaPlayback');assert control('mediaPaused',playback).get_toggle_state()
    enabled(control('mediaLoop',playback),True);press('mediaRestart',playback);playback.type_keys('{ESC}')
    results['mediaPauseRestartLoopEnd']='paused frame held; restart moved; non-looping clip ended; loop restored'
    theme_colors={}
    for theme in ('Graphite','Rose','Neon','Warm sand','Original'):
        window.type_keys('{ESC 2}');window.menu_select('Appearance->'+theme);window.type_keys('{ESC 2}');time.sleep(.5)
        assert control('studioRecord').is_enabled()
        shot=window.capture_as_image();theme_colors[theme]=shot.convert('RGB').getpixel((10,100))
        shot.save(str(report/('theme-'+theme.replace(' ','-')+'.png')))
    assert len(set(theme_colors.values()))==5,theme_colors
    results['appearanceMenu']='five themes selected through the menu'
    widths={}
    for layout in ('Simple','Podcast - larger audio mixer','Production - larger source panel','Balanced'):
        window.set_focus();window.type_keys('{ESC 2}');window.type_keys('%a');time.sleep(.2);window.type_keys('l');time.sleep(.2)
        window.type_keys({'Simple':'s','Podcast - larger audio mixer':'p','Production - larger source panel':'r','Balanced':'b'}[layout]);time.sleep(.4)
        for name in ('addSource','studioRecord','masterGain'):
            item=control(name);assert item.is_enabled() and item.is_visible(),(layout,name)
        window.capture_as_image().save(str(report/('layout-'+layout.split()[0]+'.png')))
        widths[layout]={'sources':control('sourcesPanelScroll').rectangle().width(),'audio':control('audioPanelScroll').rectangle().width()}
    assert widths['Podcast - larger audio mixer']['audio']>widths['Production - larger source panel']['audio']+100,widths
    assert widths['Production - larger source panel']['sources']>widths['Podcast - larger audio mixer']['sources']+100,widths
    results['workspaceMenu']=widths
    choose_dialog(window,'canvasFormat','1080x1920')
    time.sleep(1);window.capture_as_image().save(str(report/'portrait.png'))
    path=report/'portrait.mkv';press('studioRecord')
    dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path))
    dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
    time.sleep(5);press('studioRecord');wait_recording(False)
    ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
    decode=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(path),'-f','null','-'],capture_output=True,text=True,timeout=40)
    assert decode.returncode==0 and '1080x1920' in decode.stderr,decode.stderr
    results['portraitRecording']='1080x1920 output decoded'
    press('programWindow');popout=app.window(title='Desktop Studio - Program');popout.wait('visible')
    popout.set_focus();popout.type_keys('{F11}');time.sleep(.5)
    popout.capture_as_image().save(str(report/'program-fullscreen.png'))
    popout.type_keys('{F11}');popout.type_keys('%{F4}');popout.wait_not('visible')
    window.set_focus();results['programWindowFullscreen']='opened, fullscreen toggled, closed'
finally:
    (report/'workspace.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
