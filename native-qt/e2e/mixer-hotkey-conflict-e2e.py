"""Exercise real OS shortcut conflicts, editing and Studio staging through the GUI."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import ctypes, urllib.request
from pywinauto.keyboard import send_keys

def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text())
    return json.load(urllib.request.urlopen(urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})))

registered=ctypes.windll.user32.RegisterHotKey(None,0x59ff,0x4003,ord('B'))
assert registered,'The test shortcut is already owned by an unrelated application'
try:
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input()
    press('globalHotkeys');dialog=window.child_window(title='Global hotkeys',control_type='Window');dialog.wait('visible')
    enabled(control('globalHotkeysEnabled',dialog),True);dialog.child_window(title='Save',control_type='Button').click_input();time.sleep(.3)
    assert dialog.is_visible() and not control('globalHotkeysEnabled',dialog).get_toggle_state()
    assert any('Already in use' in c.window_text() for c in dialog.descendants())
    dialog.capture_as_image().save(str(report/'conflicting-hotkey.png'))
    ctypes.windll.user32.UnregisterHotKey(None,0x59ff);registered=False
    editor=control('globalHotkey_0',dialog);editor.set_focus();send_keys('^%+b');time.sleep(1.2)
    enabled(control('globalHotkeysEnabled',dialog),True);dialog.child_window(title='Save',control_type='Button').click_input();time.sleep(.5)
    send_keys('^%+b');time.sleep(.3);assert state()['private_break']
    send_keys('^%b');time.sleep(.3);assert state()['private_break'],'Old shortcut remained registered'
    send_keys('^%+b');time.sleep(.3);assert not state()['private_break']
    studio_mode(True);scene('Main');press('takeScene');program=state()['program']
    send_keys('^%3');time.sleep(.4);staged=state();assert staged['program']==program and staged['preview']!=program
    send_keys('^%{ENTER}');time.sleep(.4);assert state()['program']==staged['preview']
    press('globalHotkeys');dialog=window.child_window(title='Global hotkeys',control_type='Window');dialog.wait('visible')
    enabled(control('globalHotkeysEnabled',dialog),False);dialog.child_window(title='Save',control_type='Button').click_input()
    (report/'hotkey-conflict.json').write_text(json.dumps({'conflictDisablesAll':True,'customKeyWorks':True,'oldKeyReleased':True,'globalSceneStagesPreview':True,'globalTakeWorks':True},indent=2))
finally:
    if registered:ctypes.windll.user32.UnregisterHotKey(None,0x59ff)
