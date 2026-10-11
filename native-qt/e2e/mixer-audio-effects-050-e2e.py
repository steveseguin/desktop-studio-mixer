"""Measure real recorded PCM while toggling the shipped track effects."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import numpy as np
results={};ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))

def record(name):
    path=report/(name+'.mkv');press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').invoke()
    deadline=time.time()+30
    while time.time()<deadline and not control('recordingStatus').window_text().startswith('Recording:'):time.sleep(.2)
    assert control('recordingStatus').window_text().startswith('Recording:')
    time.sleep(4);press('studioRecord')
    deadline=time.time()+30
    while time.time()<deadline and 'saved' not in control('recordingStatus').window_text().lower():time.sleep(.2)
    assert 'saved' in control('recordingStatus').window_text().lower()
    raw=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-vn','-f','f32le','-ac','1','-ar','48000','pipe:1'])
    pcm=np.frombuffer(raw,dtype=np.float32)[48000:]
    return float(np.sqrt(np.mean(pcm*pcm)))

try:
    for label,signal,effect in [('compress','sine=frequency=997:sample_rate=48000,volume=6','Compress'),('gate','sine=frequency=997:sample_rate=48000,volume=0.016','Gate'),('noise','anoisesrc=color=white:amplitude=0.12:sample_rate=48000','Noise suppression')]:
        clip=report/(label+'.wav')
        subprocess.run([str(ffmpeg),'-v','error','-y','-f','lavfi','-i',signal,'-t','8','-c:a','pcm_s16le',str(clip)],check=True,timeout=20)
        name='QA '+label;add_source('Video / audio file',name,str(clip));time.sleep(1)
        baseline=record(label+'-bypass');assert baseline>1e-4,baseline
        enabled(channel(name,effect),True);time.sleep(2);processed=record(label+'-enabled')
        reduction=20*np.log10(max(processed,1e-12)/baseline)
        assert reduction < (-20 if label=='gate' else -3),(label,baseline,processed,reduction)
        results[label]={'bypassRms':baseline,'enabledRms':processed,'changeDb':float(reduction)}
        print(label,results[label],flush=True);remove(name)
    results['audio_only_files']='decoded, looped and mixed without adding a video layer'
finally:
    (report/'audio-effects.json').write_text(json.dumps(results,indent=2))
