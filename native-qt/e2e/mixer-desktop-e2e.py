"""Run real workflows in the packaged mixer. Requires pywinauto and Pillow.

Uses a private loopback browser fixture and a new MKV output. Does not publish
to an external streaming destination or open physical cameras/microphones.
"""
import argparse
import http.server
import json
import subprocess
import threading
import time
from pathlib import Path

from pywinauto import Application

parser = argparse.ArgumentParser()
parser.add_argument('--pid', type=int, required=True)
parser.add_argument('--report', default='e2e/reports/mixer')
args = parser.parse_args()
report = Path(args.report).resolve()
report.mkdir(parents=True, exist_ok=True)
app = Application(backend='uia').connect(process=args.pid)
window = app.window(title='Desktop Studio - Powered by VDO.Ninja')


def control(name, root=window):
    elements = root.descendants()
    # Qt rows can expose the object path while their view uses its accessible
    # name as AutomationId. Resolve the owning view, not an arbitrary row.
    list_name = {'sceneList':'Scenes', 'mixerSources':'Sources in selected scene'}.get(name)
    found = (next(c for c in elements if c.element_info.control_type == 'List' and c.window_text() == list_name)
             if list_name else next(c for c in elements if (c.element_info.automation_id or '').endswith('.' + name)))
    if found.element_info.control_type in ('ListItem','TreeItem','DataItem'):
        found = found.parent()
    # Real pointer/screenshot workflows must reveal controls in the workspace,
    # just as a user scrolls them into view on a smaller display. Layout checks
    # opt out while inspecting intentionally clipped viewport edges.
    if root is window and globals().get('reveal_workspace', True):
        for name in ('audioTracksScroll','sourcesPanelScroll','scenesPanelScroll','audioPanelScroll','studioWorkspaceScroll'):
            if '.'+name+'.' not in found.element_info.automation_id: continue
            viewport=next((c for c in elements if (c.element_info.automation_id or '').endswith('.'+name)),None)
            if not viewport: continue
            bars=[c for c in viewport.descendants(control_type='ScrollBar') if c.is_visible() and c.rectangle().height()>c.rectangle().width()]
            if not bars: continue
            bar=min(bars,key=lambda c:len(c.element_info.automation_id))
            bounds, rect=viewport.rectangle(),found.rectangle()
            delta=rect.top-bounds.top-10 if rect.top<bounds.top+8 else rect.bottom-bounds.bottom+10 if rect.bottom>bounds.bottom-8 else 0
            if delta:
                value=bar.iface_range_value
                value.SetValue(max(value.CurrentMinimum,min(value.CurrentMaximum,value.CurrentValue+delta)))
                time.sleep(.1)
    return root.child_window(auto_id=found.element_info.automation_id, control_type=found.element_info.control_type)


def press(name, root=window):
    control(name, root).wrapper_object().invoke()
    time.sleep(0.2)


def studio_mode(enabled=True):
    box = control('studioMode')
    if bool(box.get_toggle_state()) != enabled: box.toggle()
    time.sleep(.3)


def color(name):
    image = control(name).capture_as_image()
    return image.getpixel((image.width // 2, image.height // 4))[:3]


def scene(name):
    control('sceneList').child_window(title=name, control_type='ListItem').select()
    time.sleep(0.4)


def near(actual, expected):
    assert max(abs(a-b) for a, b in zip(actual, expected)) <= 8, (actual, expected)


def media_fixture():
    """Private moving video and tone; no dependency on another workflow's files."""
    ffmpeg = Path(__file__).resolve().parents[1] / 'dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe'
    clip = report / 'fixture.mp4'
    subprocess.run([str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-y',
                    '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=30',
                    '-f', 'lavfi', '-i', 'sine=frequency=731:sample_rate=48000',
                    '-t', '3', '-c:v', 'libopenh264', '-pix_fmt', 'yuv420p',
                    '-c:a', 'aac', str(clip)], check=True, timeout=30)
    return clip


def remove_sources(prefixes):
    # Qt recycles row accessibility objects after deletion; reacquire each time.
    while True:
        window.set_focus()
        control('mixerSources').wheel_mouse_input(wheel_dist=50)
        # Single canvas leaves a shorter source list. Search every viewport;
        # Qt may expose only visible rows to UI Automation.
        seen = set()
        while True:
            all_rows = control('mixerSources').descendants(control_type='ListItem')
            rows = [c for c in all_rows if c.window_text().startswith(prefixes)]
            if rows: break
            names = tuple(c.window_text() for c in all_rows)
            if names in seen: return
            seen.add(names)
            control('mixerSources').wheel_mouse_input(wheel_dist=-2)
            time.sleep(.15)
        name = rows[0].window_text()
        # This provider includes partially clipped rows. Scroll the target fully
        # inside the viewport before clicking its label, away from the checkbox.
        for _ in range(20):
            row = next(c for c in control('mixerSources').descendants(control_type='ListItem') if c.window_text() == name)
            rect, viewport = row.rectangle(), control('mixerSources').rectangle()
            if viewport.top < rect.top and rect.bottom < viewport.bottom: break
            control('mixerSources').wheel_mouse_input(wheel_dist=-1)
        row.select()
        press('removeSource')
        after = [c for c in control('mixerSources').descendants(control_type='ListItem')
                 if c.window_text() == name]
        assert not after, 'Take a scene without QA sources before removing them'


def add(kind, name, value=None):
    press('addSource')
    dialog = window.child_window(title='Add source', control_type='Window')
    dialog.wait('visible')
    combo = control('sourceKind', dialog)
    combo.click_input()
    combo.type_keys('{HOME}{DOWN 5}{ENTER}')
    control('sourceName', dialog).set_edit_text(name)
    # This workflow exercises opaque, smooth video capture.
    if control('browserTransparency', dialog).get_toggle_state():
        control('browserTransparency', dialog).toggle()
    if value is not None:
        control('sourceValue', dialog).set_edit_text(value)
    dialog.child_window(title='OK', control_type='Button').invoke()
    time.sleep(1)



def select(name, text):
    target = control(name).wrapper_object()
    window.set_focus()
    view = window.rectangle()
    window.wheel_mouse_input(coords=(view.width()-25, 150), wheel_dist=60)
    time.sleep(0.2)
    for _ in range(70):
        rect = target.rectangle()
        if view.top+85 < rect.mid_point().y < view.bottom-70:
            break
        window.wheel_mouse_input(coords=(view.width()-25,150), wheel_dist=-2)
        time.sleep(0.06)
    target.expand()
    time.sleep(0.25)
    item = next(c for top in app.windows() for c in top.descendants(control_type='ListItem') if c.window_text() == text and c.is_visible())
    item.click_input()
    time.sleep(0.2)
    assert target.selected_text() == text

class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'''<!doctype html><style>body {margin:0;background:#24b46a;color:white;font:48px sans-serif} div{padding:40px}</style>
<div>Browser source / live audio<br><span id="counter"></span></div>
<script>setInterval(()=>counter.textContent=Date.now(),100);
let a=new AudioContext(), o=a.createOscillator(), g=a.createGain();
o.frequency.value=440; g.gain.value=0.12; o.connect(g); g.connect(a.destination); o.start(); a.resume();</script>'''
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
threading.Thread(target=server.serve_forever, daemon=True).start()
results = {}
try:
    window.child_window(title='Studio', control_type='TabItem').click_input()
    studio_mode()
    scene('Opening')
    press('takeScene')
    scene('Main')
    near(color('previewCanvas'), (16, 23, 34))
    near(color('programCanvas'), (24, 54, 87))
    press('takeScene')
    near(color('programCanvas'), (16, 23, 34))
    scene('Break')
    near(color('programCanvas'), (16, 23, 34))
    press('fadeScene')
    time.sleep(0.6)
    near(color('programCanvas'), (24, 54, 87))
    results['preview_cut_fade'] = 'passed'
    scene('Main')
    add('Browser / remote VDO.Ninja feed', 'Loopback browser', f'http://127.0.0.1:{server.server_port}/')
    deadline = time.time() + 20
    while time.time() < deadline:
        if color('previewCanvas')[1] > 130:
            break
        time.sleep(0.4)
    near(color('previewCanvas'), (36, 180, 106))
    # The newly staged source must not affect Program until Take.
    near(color('programCanvas'), (24, 54, 87))
    press('takeScene')
    near(color('programCanvas'), (36, 180, 106))
    results['webview2_capture_and_take'] = 'passed'
    window.capture_as_image().save(str(report / 'browser-studio.png'))
    press('mixerOutput')
    if not control('advancedToggle').get_toggle_state(): control('advancedToggle').toggle()
    select('outputSelect', 'Record to file (MKV)')
    output = report / f'program-{int(time.time())}.mkv'
    control('outputUrlInput').set_edit_text(str(output))
    # Record the mixer at its native composition size/cadence.
    select('resolutionSelect', '1280x720')
    select('fpsSelect', '30')
    press('goLiveButton')
    time.sleep(5)
    window.child_window(title='Studio', control_type='TabItem').click_input()
    time.sleep(3)
    results['recording_path'] = str(output)
    assert output.exists() and output.stat().st_size > 10000, 'Recording did not produce media'
    # Mute at the actual channel strip, then switch scenes while recording.
    window.child_window(title='Mute', control_type='CheckBox').toggle()
    time.sleep(3)
    scene('Break')
    press('takeScene')
    time.sleep(3)
    press('mixerOutput')
    press('goLiveButton')
    time.sleep(4)
    results['record_and_stop'] = 'passed'
    window.capture_as_image().save(str(report / 'recording-stopped.png'))
finally:
    server.shutdown()
    (report / 'workflow.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2))
