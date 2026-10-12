"""Verify first-click chat-dock interaction across asynchronous browser startup."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import psutil,win32gui,win32process,uuid
root=psutil.Process(args.pid);results={};title='Desktop Studio - Social Stream chat dock'
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ', 'Social chat'))
def dock_window():
    owned={p.pid for p in root.children()};found=[]
    def visit(h,_):
        if win32process.GetWindowThreadProcessId(h)[1] in owned and win32gui.GetWindowText(h)==title and win32gui.GetWindowRect(h)[0]>-1000:found.append(h)
    win32gui.EnumWindows(visit,None);return found[0] if found else None
try:
    press('socialChat');prompt=window.child_window(title='Social Stream Ninja',control_type='Window')
    prompt.child_window(control_type='Edit').set_edit_text('studioqa'+uuid.uuid4().hex);prompt.child_window(title='OK',control_type='Button').invoke();time.sleep(1)
    attempts=[]
    for _ in range(3):
        window.set_focus();press('interactSource');start=time.monotonic();end=start+15;hwnd=None
        while time.monotonic()<end:
            hwnd=dock_window()
            if hwnd:break
            time.sleep(.2)
        assert hwnd,'first Open / Invite did not show the new chat dock'
        attempts.append(round(time.monotonic()-start,3));time.sleep(1)
        win32gui.PostMessage(hwnd,0x10,0,0);time.sleep(.5)
        assert not dock_window(),'Close did not park the browser dock'
    results['firstClickOpenSeconds']=attempts;results['closeAndReopen']='passed'
finally:
    (report/'browser-interaction.json').write_text(json.dumps(results,indent=2))
