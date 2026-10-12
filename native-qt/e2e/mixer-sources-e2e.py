"""Native window capture, source lifetime, and invitation UI in the packaged app.

Run after mixer-desktop-e2e.py against the same app. Uses its argument parsing
and desktop helpers, but does not execute its workflow.
"""
from pathlib import Path
import sys
import time
if '--fixture' in sys.argv:
    import tkinter
    root = tkinter.Tk()
    root.title('Desktop Studio QA Window')
    root.geometry('640x360+1500+100')
    canvas = tkinter.Canvas(root, background='#c43a46', highlightthickness=0)
    canvas.pack(fill='both', expand=True)
    label = canvas.create_text(320, 180, text='Native window source', fill='white', font=('Arial', 26))
    def tick():
        canvas.itemconfig(label, text='Native window source / ' + str(int(time.monotonic()*10)))
        root.after(100, tick)
    tick()
    root.mainloop()
    sys.exit(0)

exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
from urllib.parse import urlparse, parse_qs
fixture_process = subprocess.Popen([sys._base_executable, '-B', str(Path(__file__).resolve()), '--fixture'],
                                   creationflags=subprocess.CREATE_NO_WINDOW)
time.sleep(1.5)
results = {}
try:
    window.child_window(title='Studio', control_type='TabItem').click_input()
    studio_mode()
    scene('Main')
    press('addSource')
    dialog = window.child_window(title='Add source', control_type='Window')
    dialog.wait('visible')
    device = control('sourceDevice', dialog)
    device.expand()
    time.sleep(.3)
    item = next(c for top in app.windows() for c in top.descendants(control_type='ListItem')
                if c.window_text() == 'Desktop Studio QA Window' and c.is_visible())
    item.click_input()
    control('sourceName', dialog).set_edit_text('QA game feed')
    dialog.child_window(title='OK', control_type='Button').invoke()
    time.sleep(3)
    near(color('previewCanvas'), (196, 58, 70))
    press('takeScene')
    near(color('programCanvas'), (196, 58, 70))
    results['native_window_capture_and_take'] = 'passed'
    press('removeSource')
    assert any(c.window_text().startswith('QA game feed') for c in window.descendants(control_type='ListItem'))
    results['program_source_deletion_guard'] = 'passed'
    scene('Break')
    press('takeScene')
    next(c for c in window.descendants(control_type='ListItem') if c.window_text().startswith('QA game feed')).select()
    press('removeSource')
    assert not any(c.window_text().startswith('QA game feed') for c in window.descendants(control_type='ListItem'))
    results['source_stop_and_remove'] = 'passed'

    for command, title, label in [('inviteGuest', 'Invite guest', 'QA guest'), ('inviteCamera', 'Wireless camera', 'QA phone')]:
        press(command)
        prompt = window.child_window(title=title, control_type='Window')
        prompt.child_window(control_type='Edit').set_edit_text(label)
        prompt.child_window(title='OK', control_type='Button').invoke()
        time.sleep(.5)
        invite = window.child_window(title=label+' - invite', control_type='Window')
        url = control('inviteUrl', invite).get_value()
        parsed = urlparse(url); query = parse_qs(parsed.query, keep_blank_values=True)
        assert parsed.scheme == 'https' and parsed.hostname == 'vdo.ninja'
        assert len(query['push'][0]) >= 32 and len(query['password'][0]) >= 32
        assert ('rear' in query) == (command == 'inviteCamera')
        press('closeInvite', invite)
        # Preview edits have not taken this receiver live.
        press('removeSource')
        results[command+'_link_ui'] = 'passed'

    # Leave the new app with its starter scenes, without test inputs.
    for c in window.descendants(control_type='ListItem'):
        if c.window_text().startswith('Loopback browser'):
            c.select(); press('removeSource'); break
    scene('Opening'); press('takeScene')
    window.capture_as_image().save(str(report/'final-studio.png'))
    time.sleep(1)
    window.set_focus(); window.type_keys('^q')
    deadline = time.time()+10
    while app.is_process_running() and time.time() < deadline:
        time.sleep(.2)
    assert not app.is_process_running(), 'App did not quit cleanly'
    results['clean_quit'] = 'passed'
finally:
    fixture_process.terminate()
    fixture_process.wait(timeout=5)
    (report/'sources-workflow.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2))
