"""Use the shipped GUI to cue media, save/collect projects and select tracks."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import urllib.request,numpy as np,hashlib
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve();results={}
def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text());return json.load(urllib.request.urlopen(urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']})))
def wait(check):
    for _ in range(120):
        if check():return
        time.sleep(.2)
    raise AssertionError('UI action did not complete')
def menu(button,title,split=False):
    c=control(button)
    if split:c.click_input(coords=(c.rectangle().width()-7,c.rectangle().height()//2))
    else:press(button)
    item=next(x for top in app.windows() for x in top.descendants(control_type='MenuItem') if x.window_text()==title);item.click_input()
def filename(title,path):
    dialog=window.child_window(title=title,control_type='Window');dialog.wait('visible');field=dialog.child_window(title='File name:',control_type='Edit');field.set_edit_text(str(path));field.set_focus();field.type_keys('{ENTER}')
def full_decode(path):
    r=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-map','0','-fps_mode:v','passthrough','-enc_time_base:v','1:1000000','-f','null','-'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=50);assert r.returncode==0 and not r.stderr,r.stderr
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
clip=report/'cue.mp4';r=subprocess.run([str(ffmpeg),'-v','error','-y','-f','lavfi','-i','testsrc2=s=640x360:r=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','15','-c:v','libopenh264','-c:a','aac',str(clip)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=40);assert r.returncode==0,r.stderr
add_source('Video / audio file','QA track',str(clip));sid=next(s['id'] for s in state()['sources'] if s['name']=='QA track')
press('mediaPlayback');dialog=window.child_window(title='Media playback',control_type='Window');dialog.wait('visible')
enabled(control('mediaPaused',dialog),True);slider=control('mediaSeek',dialog);slider.set_focus();slider.type_keys('{HOME}{RIGHT 6}',pause=.02)
wait(lambda:5500<next(s['positionMs'] for s in state()['sources'] if s['id']==sid)<7000)
dialog.capture_as_image().save(str(report/'paused-seek.png'));enabled(control('mediaPaused',dialog),False);dialog.type_keys('{ESC}')
active=report/'active show.mixer.json';menu('exportShow','Save As active show...',True);filename('Save As active show',active)
wait(lambda:Path(state()['project_path'])==active)
press('renameScene');dialog=window.child_window(title='Rename scene',control_type='Window');dialog.wait('visible');dialog.child_window(control_type='Edit').set_edit_text('Episode scene');dialog.child_window(title='OK',control_type='Button').click_input()
wait(lambda:'Episode scene' in active.read_text());assert Path(str(active)+'.backup.mixer.json').is_file()
collection=report/'Collected project';menu('exportShow','Collect show and assets...',True);filename('New folder for collected show',collection)
wait(lambda:not state()['show_collection']['busy']);assert (collection/'show.mixer.json').is_file(),state()['show_collection']
menu('recordingFiles','Recording tracks...');dialog=window.child_window(title='Recording tracks',control_type='Window');dialog.wait('visible')
enabled(control('record_audio_'+sid,dialog),True);enabled(control('record_video_'+sid,dialog),True);dialog.capture_as_image().save(str(report/'recording-tracks.png'));dialog.child_window(title='Save',control_type='Button').click_input()
recording=report/'GUI recording.mkv';press('studioRecord');filename('Record Program',recording);wait_recording(True);wait(lambda:state()['recording']['separate_videos'][0]['active']);time.sleep(4)
press('privateBreak');time.sleep(2);press('privateBreak');time.sleep(3);press('studioRecord');wait(lambda:not state()['recording']['busy'])
full_decode(recording);iso=Path(state()['recording']['separate_videos'][0]['path']);full_decode(iso)
metadata=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(recording)],capture_output=True,text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW);assert 'QA track' in metadata.stderr and 'Program' in metadata.stderr
menu('recordingFiles','Export recording as MP4...');filename('Choose recording to export',recording);mp4=report/'GUI export.mp4';filename('Export MP4 (original recording preserved)',mp4)
wait(lambda:not state()['recording']['exporting']);full_decode(mp4)
window.capture_as_image().save(str(report/'project-recording.png'))
results={'keyboardSeek':True,'activeSaveAsAndBackup':True,'collectAssets':True,'namedAudioTrack':True,'separateVideoDecoded':True,'multiAudioMp4Decoded':True};(report/'project-recording-ui.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
