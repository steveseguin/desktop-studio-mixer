"""Operate playback controls and scene-triggered media restart in the packaged GUI."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import urllib.request
def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text());return json.load(urllib.request.urlopen(urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})))
def track():return next(s for s in state()['sources'] if s['name']=='QA clip')
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
add_source('Video / audio file','QA clip',str(media_fixture()));time.sleep(1);press('mediaPlayback')
dialog=window.child_window(title='Media playback',control_type='Window');dialog.wait('visible')
enabled(control('mediaRestartOnTake',dialog),True);enabled(control('mediaLoop',dialog),False);time.sleep(3.5)
assert track()['ended'];assert 2800<track()['durationMs']<3300;assert 'remaining' in control('mediaTime',dialog).window_text()
dialog.capture_as_image().save(str(report/'playback-ended.png'));dialog.type_keys('{ESC}')
scene('Break');time.sleep(.5);scene('Main');time.sleep(.4);current=track();assert not current['ended'] and current['positionMs']<1600,current
press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible');dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(report/'cue.mkv'));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
scene('Break');time.sleep(.5);scene('Main');time.sleep(3.5);press('studioRecord');wait_recording(False)
result=subprocess.run([str(Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()),'-v','error','-i',str(report/'cue.mkv'),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=30);assert result.returncode==0 and not result.stderr
window.capture_as_image().save(str(report/'cue-recorded.png'));(report/'media-cue.json').write_text(json.dumps({'durationMs':current['durationMs'],'restartPositionMs':current['positionMs'],'recording':'fully decoded'},indent=2))
