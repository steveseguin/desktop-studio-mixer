"""Real GUI scene fades, audio-only podcast and a relocated show/assets folder."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import re,shutil,numpy as np
from PIL import Image
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
results={};bundle=report/'show-bundle';assets=bundle/'assets';assets.mkdir(parents=True)

def ff(*options):
    result=subprocess.run([str(ffmpeg),*map(str,options)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=60)
    assert result.returncode==0,result.stderr.decode(errors='replace')[-2000:]
    return result

def record(path):
    press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path))
    dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)

def audio(path):
    decoded=ff('-v','info','-copyts','-i',path,'-vn','-af','aformat=sample_fmts=flt:sample_rates=48000:channel_layouts=mono,asetnsamples=n=480:p=1,ashowinfo','-f','f32le','-')
    pts=np.array([float(x) for x in re.findall(r'\bn:\d+\s+pts:\S+\s+pts_time:([-\d.e+]+)',decoded.stderr.decode(errors='replace'))])
    pcm=np.frombuffer(decoded.stdout,np.float32).reshape(-1,480);assert len(pts)==len(pcm)
    return pts,pcm

def groups(pts,mask):
    indexes=np.flatnonzero(mask)
    return [part for part in np.split(indexes,np.flatnonzero(np.diff(indexes)>1)+1) if len(part)>1]

try:
    for name,fill,tone in [('red','#d02020',500),('green','#20d020',1500)]:
        ff('-v','error','-y','-f','lavfi','-i',f'color=c={fill}:s=640x360:r=30','-f','lavfi','-i',f'sine=frequency={tone}:sample_rate=48000',
           '-t','60','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',assets/(name+'.mp4'))
    ff('-v','error','-y','-f','lavfi','-i','sine=frequency=2000:sample_rate=48000','-t','5',assets/'podcast.wav')
    Image.new('RGB',(640,360),(208,32,32)).save(assets/'cover.png')
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(True)
    scene('Main');add_source('Video / audio file','QA red',str(assets/'red.mp4'));press('takeScene')
    scene('Break');add_source('Video / audio file','QA green',str(assets/'green.mp4'))
    scene('Main');press('takeScene');time.sleep(2);record(report/'fades.mkv');time.sleep(3)
    scene('Break');press('fadeScene');time.sleep(3)
    scene('Main');press('fadeScene');time.sleep(3)
    window.capture_as_image().save(str(report/'scene-fades.png'));press('studioRecord');wait_recording(False)
    decoded=ff('-v','info','-copyts','-i',report/'fades.mkv','-an','-vf','scale=32:18,format=rgb24,showinfo','-fps_mode','passthrough','-f','rawvideo','-')
    vpts=np.array([float(x) for x in re.findall(r'\bn:\s*\d+\s+pts:\s*\S+\s+pts_time:([-\d.e+]+)',decoded.stderr.decode(errors='replace'))])
    rgb=np.frombuffer(decoded.stdout,np.uint8).reshape(-1,18,32,3).mean(axis=(1,2));assert len(rgb)==len(vpts)
    ratio=rgb[:,1]/(rgb[:,0]+rgb[:,1]);vr=(ratio-np.percentile(ratio,5))/(np.percentile(ratio,95)-np.percentile(ratio,5))
    apts,pcm=audio(report/'fades.mkv')
    tones=np.stack([abs(pcm@np.exp(-2j*np.pi*f*np.arange(480)/48000))*2/480 for f in (500,1500)],axis=1)
    tones/=np.percentile(tones,95,axis=0);ar=tones[:,1]/np.maximum(tones.sum(axis=1),.01)
    ag=groups(apts,(ar>.2)&(ar<.8)&(apts>apts[0]+2)&(apts<apts[-1]-1))
    vg=groups(vpts,(vr>.2)&(vr<.8)&(vpts>vpts[0]+2)&(vpts<vpts[-1]-1))
    assert len(ag)==len(vg)==2,([len(x) for x in ag],[len(x) for x in vg])
    fades=[]
    for a,v in zip(ag,vg):
        duration=apts[a[-1]]-apts[a[0]];skew=np.mean(apts[a])-np.mean(vpts[v])
        assert .15<duration<.5 and abs(skew)<.09,(duration,skew)
        fades.append({'audio20To80Ms':round(float(duration)*1000,1),'audioMinusPictureMidpointMs':round(float(skew)*1000,1)})
    results['sceneAudioFades']=fades
    # Replace the picture with a cover and use an audio-only file as the show.
    studio_mode(False);scene('Main');remove_sources(('QA ',))
    add_source('Video / audio file','QA podcast',str(assets/'podcast.wav'))
    add_source('Image','QA cover',str(assets/'cover.png'));near(color('previewCanvas'),(208,32,32))
    press('exportShow');dialog=window.child_window(title='Save show (includes source URLs and invite links)',control_type='Window');dialog.wait('visible')
    show=bundle/'podcast.mixer.json';dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(show));dialog.child_window(title='Save',control_type='Button').click_input();time.sleep(1)
    saved=json.loads(show.read_text());local=[s['locator'] for s in saved['sources'] if s['kind'] in ('image','media')]
    assert local and all(x.startswith('assets/') for x in local),local
    moved=report/'relocated-bundle';shutil.copytree(bundle,moved)
    Image.new('RGB',(640,360),(32,32,208)).save(moved/'assets/cover.png')
    press('importShow');dialog=window.child_window(title='Open show',control_type='Window');dialog.wait('visible')
    filename=dialog.child_window(title='File name:',control_type='Edit');filename.set_edit_text(str(moved/'podcast.mixer.json'));filename.set_focus();filename.type_keys('{ENTER}');time.sleep(3)
    scene('Main');near(color('previewCanvas'),(32,32,208))
    record(report/'podcast-relocated.mkv');time.sleep(5);window.capture_as_image().save(str(report/'podcast-relocated.png'));press('studioRecord');wait_recording(False)
    _,pcm=audio(report/'podcast-relocated.mkv');tone=float(np.median(abs(pcm@np.exp(-2j*np.pi*2000*np.arange(480)/48000))*2/480))
    assert tone>.05,tone
    results['audioOnlyPodcast']={'decoded2000HzAmplitude':tone};results['relocatedShow']={'relativeAssets':local,'relocatedCover':'blue, verified in Program'}
finally:
    (report/'show-types.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results),flush=True)
