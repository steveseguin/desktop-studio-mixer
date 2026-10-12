"""Capture a real animated native window; change an existing input from 30 to 60."""
from pathlib import Path
import sys,time
if '--fixture' in sys.argv:
    import tkinter
    root=tkinter.Tk();root.title('Desktop Studio QA 60fps');root.geometry('640x360+1240+60')
    canvas=tkinter.Canvas(root,background='#204060',highlightthickness=0);canvas.pack(fill='both',expand=True)
    bar=canvas.create_rectangle(0,0,80,360,fill='#eeee22',outline='');label=canvas.create_text(320,180,text='',fill='white',font=('Arial',24))
    def tick():
        frame=int(time.perf_counter()*120);x=(frame*8)%640
        canvas.coords(bar,x,0,x+80,360);canvas.itemconfig(label,text='Native game fixture / '+str(frame));root.after(4,tick)
    tick();root.mainloop();sys.exit()
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import urllib.request,numpy as np
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve();results={}
fixture=None;pw=None;browser=None;page=None
if os.environ.get('STUDIO_GAME_FIXTURE','edge')=='edge':
    sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
    from playwright.sync_api import sync_playwright
    pw=sync_playwright().start();browser=pw.chromium.launch(channel='msedge',headless=False,args=['--window-position=1240,60','--window-size=640,480','--disable-background-timer-throttling','--disable-backgrounding-occluded-windows','--disable-renderer-backgrounding'])
    page=browser.new_page();page.set_content('''<title>Desktop Studio QA 60fps</title><style>body{margin:0}canvas{width:100vw;height:100vh}</style><canvas id=c width=640 height=360></canvas><script>
    window.qaFrame=0;const x=c.getContext('2d');function draw(){qaFrame++;x.fillStyle='#204060';x.fillRect(0,0,640,360);x.fillStyle='#eeee22';x.fillRect(qaFrame*8%640,0,80,360);x.fillStyle='white';x.font='24px sans-serif';x.fillText('GPU window / '+qaFrame,80,180);requestAnimationFrame(draw)}draw();</script>''')
else:
    fixture=subprocess.Popen([sys.executable,'-B',__file__,'--fixture'],creationflags=subprocess.CREATE_NO_WINDOW)
def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text())
    with urllib.request.urlopen(urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})) as r:return json.load(r)
def count():return next(s['frames'] for s in state()['sources'] if s['name']=='QA game')
try:
    time.sleep(1);window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
    choose_dialog(window,'canvasFormat','1280x720 / 30 fps');add_source('Game / window','QA game',device='Desktop Studio QA 60fps')
    for fps in (30,60,30,60):
        choose_dialog(window,'canvasFormat',f'1280x720 / {fps} fps');time.sleep(3)
        first=count();produced=page.evaluate('qaFrame') if page else None;started=time.perf_counter();time.sleep(6);rate=(count()-first)/(time.perf_counter()-started)
        results.setdefault('sourceRateAfterFormatChange',[]).append({'target':fps,'receivedFps':rate,'fixtureFps':(page.evaluate('qaFrame')-produced)/(time.perf_counter()-started) if page else None})
    path=report/'game-60.mkv';press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
    time.sleep(10);window.capture_as_image().save(str(report/'game.png'));press('studioRecord');wait_recording(False)
    r=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-an','-vf','scale=160:90','-pix_fmt','gray','-f','rawvideo','-'],capture_output=True,timeout=40);assert r.returncode==0,r.stderr
    frames=np.frombuffer(r.stdout,np.uint8).reshape(-1,90,160)
    changed=np.max(abs(np.diff(frames.astype(np.int16),axis=0)),axis=(1,2))>8
    ratio=float(changed[120:-60].mean())
    results['recorded60fps']={'frames':len(frames),'movingFrameFraction':ratio}
    assert ratio>(.90 if page else .72),ratio
    assert all(v['target']*.8<v['receivedFps']<v['target']*1.2 for v in results['sourceRateAfterFormatChange']),results
finally:
    if fixture:fixture.terminate();fixture.wait(10)
    if browser:browser.close()
    if pw:pw.stop()
    (report/'gaming-rate.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results),flush=True)
