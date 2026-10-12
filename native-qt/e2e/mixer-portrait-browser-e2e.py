"""Edit a responsive browser viewport and verify safe guides stay out of recording."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import urllib.request, numpy as np

class Page(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'<!doctype html><style>body{margin:0;background:white}div{position:fixed;inset:0;background:#2244cc}@media(max-aspect-ratio:1/1){div{background:#22cc44}}</style><div></div>'
        self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*args):pass

def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text())
    return json.load(urllib.request.urlopen(urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})))

def track():return next(s for s in state()['sources'] if s['name']=='QA responsive browser')

server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Page);threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
    choose_dialog(window,'canvasFormat','720x1280 / 30 fps')
    add_source('Browser /','QA responsive browser',f'http://127.0.0.1:{server.server_port}/',transparent=True)
    deadline=time.time()+40
    while track()['frames']<10 and time.time()<deadline:time.sleep(.3)
    assert track()['browserWidth']==720 and track()['browserHeight']==1280,track()
    near(color('previewCanvas'),(34,204,68))
    enabled(control('safeArea'),False);before=np.asarray(control('previewCanvas').capture_as_image())
    enabled(control('safeArea'),True);after=np.asarray(control('previewCanvas').capture_as_image())
    assert np.count_nonzero(np.max(abs(after.astype(int)-before.astype(int)),axis=2)>12)>100
    window.capture_as_image().save(str(report/'portrait-safe-area.png'))
    recording=report/'portrait-guide.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    field=dialog.child_window(title='File name:',control_type='Edit');field.set_edit_text(str(recording));field.set_focus();field.type_keys('{ENTER}');wait_recording(True)
    time.sleep(3);press('studioRecord');wait_recording(False)
    ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
    r=subprocess.run([str(ffmpeg),'-v','error','-ss','1','-i',str(recording),'-frames:v','1','-pix_fmt','rgb24','-f','rawvideo','-'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=30)
    assert r.returncode==0 and not r.stderr,r.stderr
    pixels=np.frombuffer(r.stdout,np.uint8).reshape(1280,720,3)
    # The full recorded picture is green, including the editing guide's 10% border.
    assert np.max(abs(pixels.astype(int)-np.array([34,204,68])))<12
    source_row('QA responsive browser').select();press('editSource');dialog=window.child_window(title='Edit source',control_type='Window');dialog.wait('visible')
    control('browserWidth',dialog).iface_range_value.SetValue(1280);control('browserHeight',dialog).iface_range_value.SetValue(720)
    dialog.child_window(title='OK',control_type='Button').click_input();time.sleep(5)
    assert track()['browserWidth']==1280 and track()['browserHeight']==720
    canvas=control('previewCanvas').capture_as_image();near(canvas.getpixel((canvas.width//2,canvas.height//2))[:3],(34,68,204))
    window.capture_as_image().save(str(report/'landscape-browser-in-portrait.png'))
    saved=next(s for s in json.loads(show_path.read_text())['sources'] if s['name']=='QA responsive browser')
    assert saved['browserWidth']==1280 and saved['browserHeight']==720
    (report/'portrait-browser.json').write_text(json.dumps({'newBrowserUsesCanvasSize':True,'viewportEditChangesResponsiveLayout':True,'viewportSaved':True,'safeGuideVisibleOnlyInEditor':True,'recordedSize':[720,1280]},indent=2))
finally:
    server.shutdown();server.server_close()
