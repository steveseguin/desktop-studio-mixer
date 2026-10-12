"""Packaged show import/export, unavailable-device edits and invalid-file recovery."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import uuid
import hashlib
import psutil
import win32gui
import win32process

results = {'exeSha256': hashlib.sha256(Path(psutil.Process(args.pid).exe()).read_bytes()).hexdigest()}
def uid(): return str(uuid.uuid4())

def file_dialog(command, title, path, action):
    press(command)
    handles = []
    def visit(handle, _):
        if (win32gui.IsWindowVisible(handle) and win32gui.GetWindowText(handle) == title
                and win32process.GetWindowThreadProcessId(handle)[1] == args.pid):
            handles.append(handle)
    deadline = time.time()+10
    while not handles and time.time()<deadline:
        win32gui.EnumWindows(visit, None)
        if not handles: time.sleep(.1)
    assert len(handles) == 1, handles
    handle = handles[0]
    dialog = app.window(handle=handle)
    dialog.wait('visible', timeout=10)
    filename = dialog.child_window(title='File name:', control_type='Edit')
    filename.set_edit_text(str(path))
    filename.set_focus(); filename.type_keys('{ENTER}')
    deadline = time.time()+20
    while win32gui.IsWindow(handle) and win32gui.IsWindowVisible(handle) and time.time()<deadline:
        time.sleep(.1)
    assert not win32gui.IsWindow(handle) or not win32gui.IsWindowVisible(handle), action+' did not close the dialog'
    time.sleep(1)

missing = 'desktop-studio-e2e-unavailable-device-'+uid()
sources = [dict(id=uid(), name='QA unavailable '+kind, kind=kind, locator=missing,
                color='#ff183657', gain=1) for kind in ('camera', 'microphone')]
sources.append(dict(id=uid(), name='QA translucent color', kind='color',
                    locator='#80443322', color='#80443322', gain=1))
fixture = dict(version=1, sources=sources, scenes=[dict(id=uid(), name='QA imported show',
               background='#80504030', layers=[dict(source=sources[-1]['id'])])])
input_file = report/'input.mixer.json'
input_file.write_text(json.dumps(fixture), encoding='utf-8')
try:
    window.set_focus(); window.child_window(title='Studio', control_type='TabItem').click_input()
    studio_mode(False)
    file_dialog('importShow', 'Open show', input_file, 'Open')
    scene('QA imported show')
    for kind in ('camera', 'microphone'):
        source_row('QA unavailable '+kind).select(); press('editSource')
        dialog = window.child_window(title='Edit source', control_type='Window')
        selected = control('sourceDevice', dialog).selected_text()
        results[kind+'Selection'] = selected
        if 'unavailable' not in selected.lower():
            dialog.child_window(title='Cancel', control_type='Button').invoke()
            raise AssertionError('Unavailable '+kind+' silently selected another device: '+selected)
        control('sourceName', dialog).set_edit_text('QA renamed '+kind)
        dialog.child_window(title='OK', control_type='Button').invoke(); time.sleep(1)
    exported = report/('export-'+str(int(time.time()))+'.mixer.json')
    file_dialog('exportShow', 'Save show (includes source URLs and invite links)', exported, 'Save')
    actual = json.loads(exported.read_text(encoding='utf-8'))
    for kind in ('camera', 'microphone'):
        item = next(s for s in actual['sources'] if s['kind'] == kind)
        assert item['locator'] == missing and item['name'] == 'QA renamed '+kind, item
    results['unavailableDeviceIdentityPreserved'] = 'passed'
    assert actual['scenes'][0]['background'].lower() == '#80504030', actual['scenes'][0]
    assert next(s for s in actual['sources'] if s['kind']=='color')['color'].lower() == '#80443322'
    results['translucentColorsRoundTrip'] = 'passed'
    before = color('previewCanvas')
    file_dialog('importShow', 'Open show', exported, 'Open')
    near(color('previewCanvas'), before)
    results['savedShowReopensWithSamePixels'] = 'passed'
    fixture['scenes'][0]['layers'][0]['source'] = uid()
    invalid = report/'invalid.mixer.json'
    invalid.write_text(json.dumps(fixture), encoding='utf-8')
    file_dialog('importShow', 'Open show', invalid, 'Open')
    failure=window.child_window(title='Could not open show',control_type='Window')
    failure.wait('visible',timeout=5)
    time.sleep(3)  # Device retries must not hide the import error.
    text=' '.join(c.window_text() for c in failure.descendants(control_type='Text'))
    assert 'Invalid source reference' in text and 'current show has been kept' in text,text
    failure.capture_as_image().save(str(report/'invalid-show-error.png'))
    failure.child_window(title='OK',control_type='Button').click_input()
    scene('QA imported show'); near(color('previewCanvas'), before)
    results['invalidShowKeepsCurrentShow'] = 'passed'
finally:
    (report/'show-workflow.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2), flush=True)
