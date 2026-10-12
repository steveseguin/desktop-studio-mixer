"""Measure steady 4K playback through the visible source and canvas controls."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import psutil,re,numpy as np
results={};root=psutil.Process(args.pid)
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
short=report/'4k-short.mp4';clip=report/'4k-long.mp4'
subprocess.run([str(ffmpeg),'-v','error','-y','-f','lavfi','-i','testsrc2=size=4096x2160:rate=30','-t','3','-c:v','libopenh264','-b:v','24000000',str(short)],check=True,timeout=120)
subprocess.run([str(ffmpeg),'-v','error','-y','-stream_loop','19','-i',str(short),'-c','copy',str(clip)],check=True,timeout=30)
window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main')
try:
    add_source('Video / audio file','QA steady 4K',str(clip));time.sleep(4)
    for compositor in ('CPU composition','GPU composition'):
        choose_dialog(window,'compositorBackend',compositor);time.sleep(2)
        fps=[];owned=[root,*root.children(recursive=True)]
        for p in owned:p.cpu_percent()
        for _ in range(8):
            time.sleep(1.1);health=control('sourceHealth').window_text()
            match=re.search(r': ([\d.]+) fps',health);assert match,health;fps.append(float(match[1]))
        cpu=sum(p.cpu_percent() for p in owned if p.is_running())
        # A received-frame count alone cannot prove that video is visible.
        pixels=np.asarray(control('previewCanvas').capture_as_image().convert('RGB'))
        assert np.count_nonzero(pixels[:,:,1]>180)>pixels.shape[0]*pixels.shape[1]*.05, 'Decoded video is missing from canvas'
        results[compositor]={'receivedFps':fps,'medianReceivedFps':float(np.median(fps)),'treeCpuPercentOneCore':cpu}
        window.capture_as_image().save(str(report/(compositor.split()[0]+'.png')))
    assert all(min(v['receivedFps'])>0 for v in results.values()),results
finally:
    (report/'media-performance.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
