"""Real packaged overlay defaults, navigation, CSP, capture modes and recording.

Use a private loopback fixture and QA-only loopback WebView2 debugging. The
caller must back up and restore the user's show and settings around this run.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
import os, sys
sys.path.insert(0, str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
from PIL import Image

helpers = Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
profile = Path(os.environ['LOCALAPPDATA']) / 'VideoMixer/browser'
show = show_path
results = {}


class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/style.css':
            content_type = 'text/css'
            body = b'''html {background:#ee2233 !important} body {margin:0;min-height:100vh;background:#8833aa !important}
            .card {position:absolute;left:10%;top:30%;width:35%;height:40%;background:#25b46a;color:white;font:30px sans-serif}
            iframe {position:absolute;left:80%;top:80%;width:10%;height:10%;border:0}'''
        else:
            content_type = 'text/html'
            body = b'<!doctype html><link rel="stylesheet" href="/style.css"><body><div class="card">Overlay card</div><iframe src="/child"></iframe>'
            if self.path == '/child':
                body = b'<!doctype html><style>html,body {margin:0;background:#cab02a}</style><body>Embedded content'
        self.send_response(200)
        if self.path == '/strict':
            self.send_header('Content-Security-Policy', "default-src 'none'; style-src 'self'; frame-src 'self'; script-src 'none'")
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)

    def log_message(self, *_): pass


def connect(pw):
    deadline = time.time() + 20
    while time.time() < deadline:
        try:
            port = int((profile / source_id / 'EBWebView/DevToolsActivePort').read_text().splitlines()[0])
            browser = pw.chromium.connect_over_cdp(f'http://127.0.0.1:{port}', timeout=2000)
            page = next(p for c in browser.contexts for p in c.pages if p.url.startswith(url))
            page.wait_for_load_state('load')
            return browser, page
        except Exception:
            time.sleep(.3)
    raise AssertionError('Browser source did not become ready')


def page_colors(page, transparent=True):
    colors = page.evaluate('''() => [document.documentElement, document.body, document.querySelector('.card')]
        .map(e => getComputedStyle(e).backgroundColor)''')
    expected = ['rgba(0, 0, 0, 0)', 'rgba(0, 0, 0, 0)'] if transparent else ['rgb(238, 34, 51)', 'rgb(136, 51, 170)']
    assert colors == expected + ['rgb(37, 180, 106)'], colors
    return colors


def canvas_colors(name='programCanvas', background=(33, 74, 121)):
    time.sleep(.8)
    image = control(name).capture_as_image()
    near(image.getpixel((int(image.width*.75), image.height//2))[:3], background)
    near(image.getpixel((int(image.width*.25), image.height//2))[:3], (37,180,106))
    return image


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = f'http://127.0.0.1:{server.server_port}'
recording = False
try:
    window.child_window(title='Studio', control_type='TabItem').click_input()
    studio_mode(); scene('Main'); press('takeScene')
    add_source('Color', 'QA overlay backing', '#214a79')
    press('takeScene')
    press('addSource'); dialog = window.child_window(title='Add source', control_type='Window')
    choose_dialog(dialog, 'sourceKind', 'Browser /')
    assert control('browserTransparency', dialog).get_toggle_state(), 'New browsers must default to overlay mode'
    control('sourceName', dialog).set_edit_text('QA overlay background')
    control('sourceValue', dialog).set_edit_text(url+'/normal')
    dialog.child_window(title='OK', control_type='Button').invoke(); time.sleep(4)
    source_id = next(s['id'] for s in json.loads(show.read_text())['sources'] if s['name']=='QA overlay background')
    with sync_playwright() as pw:
        browser, page = connect(pw)
        results['newBrowserDefault'] = page_colors(page)
        canvas_colors('previewCanvas'); press('takeScene'); canvas_colors()
        frame = next(f for f in page.frames if f.url.endswith('/child'))
        assert frame.evaluate('getComputedStyle(document.body).backgroundColor') == 'rgb(202, 176, 42)'
        results['embeddedDocumentStyling'] = 'preserved'
        page.reload(); page_colors(page); canvas_colors()
        results['reload'] = 'transparent in Program'
        page.goto(url+'/strict'); page_colors(page); canvas_colors()
        results['navigationWithStrictCsp'] = 'transparent in Program; page CSP unchanged'
        # A stylesheet added after load and a replaced body must also work.
        page.evaluate('''() => {
            const sheet = document.createElement('link'); sheet.rel='stylesheet'; sheet.href='/style.css'; document.head.append(sheet);
            const replacement=document.body.cloneNode(true); document.body.replaceWith(replacement);
        }''')
        time.sleep(1); page_colors(page); canvas_colors()
        results['lateStylesheetAndBodyReplacement'] = 'transparent; card still opaque'
        source_row('QA overlay background').select(); press('reconnectSource'); time.sleep(4)
        browser, page = connect(pw); page_colors(page); canvas_colors()
        results['sourceReconnect'] = 'transparency reapplied'
        press('editSource'); dialog = window.child_window(title='Edit source', control_type='Window')
        assert control('browserTransparency', dialog).get_toggle_state()
        enabled(control('browserTransparency', dialog), False)
        dialog.child_window(title='OK', control_type='Button').invoke(); time.sleep(4)
        browser, page = connect(pw); page_colors(page, False)
        canvas_colors(background=(136,51,170))
        assert 'Windows capture / 30 fps' in control('sourceHealth').window_text()
        # Opening and saving an existing opaque source must preserve its mode.
        press('editSource'); dialog = window.child_window(title='Edit source', control_type='Window')
        assert not control('browserTransparency', dialog).get_toggle_state()
        dialog.child_window(title='OK', control_type='Button').invoke()
        results['smoothOpaqueMode'] = 'original CSS preserved; Windows capture at 30 fps; edit preserves mode'
        press('editSource'); dialog = window.child_window(title='Edit source', control_type='Window')
        enabled(control('browserTransparency', dialog), True)
        dialog.child_window(title='OK', control_type='Button').invoke(); time.sleep(4)
        browser, page = connect(pw); page_colors(page); canvas_colors()
        output = report/'transparent-overlay.mkv'
        press('studioRecord'); dialog=window.child_window(title='Record Program', control_type='Window'); dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(output))
        dialog.child_window(title='Save',control_type='Button').invoke(); recording=True
        time.sleep(7)
        assert control('studioRecord').window_text() == 'Stop recording'
        canvas_colors().save(str(report/'overlay-program.png'))
        press('studioRecord'); recording=False
        deadline=time.time()+25
        while time.time()<deadline and 'saved' not in control('recordingStatus').window_text().lower(): time.sleep(.3)
        assert 'saved' in control('recordingStatus').window_text().lower()
        ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
        decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(output),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,timeout=45)
        assert decoded.returncode==0 and not decoded.stderr, decoded.stderr
        raw=subprocess.run([str(ffmpeg),'-v','error','-ss','3','-i',str(output),'-frames:v','1','-vf','scale=320:180','-pix_fmt','rgb24','-f','rawvideo','-'],capture_output=True,check=True,timeout=20).stdout
        image=Image.frombytes('RGB',(320,180),raw)
        near(image.getpixel((240,90)),(33,74,121)); near(image.getpixel((80,90)),(37,180,106))
        results['recording'] = 'fully decoded; backing layer and opaque card colors verified in encoded frame'
finally:
    if recording:
        try: press('studioRecord'); time.sleep(3)
        except Exception: pass
    server.shutdown()
    (report/'overlay-background.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2),flush=True)
