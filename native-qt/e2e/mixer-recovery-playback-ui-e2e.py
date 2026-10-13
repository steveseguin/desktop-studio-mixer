"""Recover an abruptly interrupted recording using visible menus, then seek it."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import urllib.request,hashlib
results={};original=Path(os.environ['STUDIO_RECOVERY_INPUT']).resolve()
original_hash=hashlib.sha256(original.read_bytes()).hexdigest()
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()

def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text())
    return json.load(urllib.request.urlopen(urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})))

def wait(check,label,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if check():return
        time.sleep(.2)
    raise AssertionError(label)

def track(name):return next(s for s in state()['sources'] if s['name']==name)

def filename(title,path):
    dialog=window.child_window(title=title,control_type='Window');dialog.wait('visible')
    field=dialog.child_window(title='File name:',control_type='Edit');field.set_edit_text(str(path));field.set_focus();field.type_keys('{ENTER}')

window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
try:
    add_source('Video / audio file','QA interrupted MKV',str(original))
    wait(lambda:track('QA interrupted MKV')['frames']>10 or track('QA interrupted MKV')['error'],'raw MKV load')
    raw=track('QA interrupted MKV')
    results['rawMkv']={key:raw[key] for key in ('frames','durationMs','error','peak')}
    window.capture_as_image().save(str(report/'raw-mkv.png'))
    if raw['frames']>10 and raw['durationMs']==0:
        press('mediaPlayback');dialog=window.child_window(title='Media playback',control_type='Window');dialog.wait('visible')
        wait(lambda:'Export recording as MP4' in control('mediaTime',dialog).window_text(),'recovery explanation')
        assert not control('mediaSeek',dialog).is_enabled()
        dialog.capture_as_image().save(str(report/'raw-mkv-recovery-hint.png'));dialog.type_keys('{ESC}')
        results['missingDurationExplained']=True
    remove('QA interrupted MKV')
    press('recordingFiles')
    item=next(x for top in app.windows() for x in top.descendants(control_type='MenuItem') if x.window_text()=='Export recording as MP4...');item.click_input()
    filename('Choose recording to export',original)
    exported=report/'Recovered recording.mp4';filename('Export MP4 (original recording preserved)',exported)
    wait(lambda:not state()['recording']['exporting'],'MP4 export')
    assert exported.exists(),state()['recording']['export_status']
    decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(exported),'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=120)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    add_source('Video / audio file','QA recovered MP4',str(exported))
    wait(lambda:track('QA recovered MP4')['frames']>20 and track('QA recovered MP4')['peak']>.01,'recovered playback')
    press('mediaPlayback');dialog=window.child_window(title='Media playback',control_type='Window');dialog.wait('visible')
    enabled(control('mediaPaused',dialog),True)
    slider=control('mediaSeek',dialog);slider.set_focus();slider.type_keys('{HOME}{RIGHT 20}',pause=.025)
    wait(lambda:19000<track('QA recovered MP4')['positionMs']<21500,'middle seek')
    dialog.capture_as_image().save(str(report/'recovered-middle-seek.png'))
    slider.set_focus();slider.type_keys('{END}{LEFT 2}',pause=.05)
    wait(lambda:track('QA recovered MP4')['positionMs']>50000,'near-end seek')
    dialog.capture_as_image().save(str(report/'recovered-end-seek.png'))
    before=track('QA recovered MP4')['frames'];enabled(control('mediaPaused',dialog),False);dialog.type_keys('{ESC}')
    wait(lambda:track('QA recovered MP4')['frames']>before+10,'resume after seek')
    window.capture_as_image().save(str(report/'recovered-playing.png'))
    assert hashlib.sha256(original.read_bytes()).hexdigest()==original_hash
    results.update({'visibleExport':True,'allExportedTracksDecoded':True,'recoveredPictureAndAudio':True,'middleAndEndKeyboardSeek':True,'resumeAfterSeek':True,'originalUnchanged':True,'passed':True})
finally:
    (report/'recovery-playback-ui.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
