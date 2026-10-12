"""Measure real GUI playback/recording using a shared encoded flash/beep clock.

Uses actual decoded audio/video PTS, never assumes both tracks begin at zero.
Reports CPU for the owned process tree (100% = one logical processor).
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import functools,re,psutil,numpy as np,urllib.request
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
root=psutil.Process(args.pid);results={'cases':[]};server=None

def ff(*options):
    result=subprocess.run([str(ffmpeg),*map(str,options)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=120)
    assert result.returncode==0,result.stderr.decode(errors='replace')[-3000:]
    return result

def state():
    d=json.loads(Path(os.environ['STUDIO_E2E_DISCOVERY']).read_text())
    with urllib.request.urlopen(urllib.request.Request(d['base_url']+'/studio/state',headers={'Authorization':'Bearer '+d['token']}),timeout=10) as response:return json.load(response)

def usage():
    out={}
    for process in [root,*root.children(recursive=True)]:
        try:
            args=process.cmdline();role='app' if process.pid==root.pid else process.name()
            if '--studio-worker' in args:role=args[args.index('--studio-worker')+1]
            cpu=process.cpu_times();out[process.pid]=(cpu.user+cpu.system,process.memory_info().private,role)
        except psutil.Error:pass
    return out

def decode_timing(path,width,height,fps):
    # Scale with the full canvas preserved. Locate the fixture's flash after Fit.
    small_h=round(height*96/width/2)*2
    video=ff('-v','info','-copyts','-i',path,'-an','-vf',f'scale=96:{small_h},format=gray,showinfo','-fps_mode','passthrough','-f','rawvideo','-')
    vpts=np.array([float(s) for s in re.findall(r'\bn:\s*\d+\s+pts:\s*\S+\s+pts_time:([-\d.e+]+)',video.stderr.decode(errors='replace'))])
    frames=np.frombuffer(video.stdout,np.uint8).reshape(-1,small_h,96);assert len(vpts)==len(frames)
    scale=min(width/1280,height/720);left=(width-1280*scale)/2;top=(height-720*scale)/2
    x=round((left+64*scale)*96/width);y=round((top+64*scale)*small_h/height)
    bright=frames[:,y-1:y+2,x-1:x+2].mean(axis=(1,2))>180
    visual=vpts[1:][bright[1:]&~bright[:-1]]
    sound=ff('-v','info','-copyts','-i',path,'-vn','-af','aformat=sample_fmts=flt:sample_rates=48000:channel_layouts=mono,asetnsamples=n=480:p=1,ashowinfo','-f','f32le','-')
    apts=np.array([float(s) for s in re.findall(r'\bn:\d+\s+pts:\S+\s+pts_time:([-\d.e+]+)',sound.stderr.decode(errors='replace'))])
    pcm=np.frombuffer(sound.stdout,np.float32).reshape(-1,480);assert len(apts)==len(pcm),(len(apts),len(pcm))
    loud=np.sqrt(np.mean(pcm**2,axis=1))>.035
    audible=apts[1:][loud[1:]&~loud[:-1]]
    begin=max(vpts[0],apts[0])+2;end=min(vpts[-1],apts[-1])-1
    offsets=np.array([audible[np.argmin(abs(audible-v))]-v for v in visual if begin<v<end])
    assert len(offsets)>=10,(visual.tolist(),audible.tolist())
    intervals=np.diff(vpts);steady=intervals[(vpts[1:]>begin)&(vpts[1:]<end)]
    changes=np.max(abs(np.diff(frames.astype(np.int16),axis=0)),axis=(1,2))>8
    run=longest=0
    for changed in changes:
        run=0 if changed else run+1;longest=max(run,longest)
    half=len(offsets)//2
    return {'frames':len(vpts),'averageFps':float(1/steady.mean()),'p95FrameIntervalMs':float(np.percentile(steady,95)*1000),
        'maxFrameIntervalMs':float(steady.max()*1000),'intervalsOverTwoFrames':int(np.sum(steady>2.1/fps)),
        'longestNearIdenticalFrameRun':longest,'matchedPulses':len(offsets),'audioMinusVideoMs':[round(float(o)*1000,1) for o in offsets],
        'medianAudioMinusVideoMs':float(np.median(offsets)*1000),'p95AbsoluteSyncMs':float(np.percentile(abs(offsets),95)*1000),
        'earlyLateSyncChangeMs':float((np.median(offsets[-half:])-np.median(offsets[:half]))*1000)}

try:
    fixture=report/'clock.mp4'
    ff('-v','error','-y','-f','lavfi','-i','testsrc2=size=1280x720:rate=60',
       '-f','lavfi','-i',r'aevalsrc=if(lt(mod(t\,1)\,0.12)\,0.25*sin(2*PI*1000*t)\,0):s=48000',
       '-vf',"drawbox=x=0:y=0:w=128:h=128:color=black:t=fill,drawbox=x=0:y=0:w=128:h=128:color=white:t=fill:enable='lt(mod(t,1),0.12)'",
       '-t','12','-c:v','libopenh264','-b:v','8000k','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k',fixture)
    (report/'player.html').write_text('<!doctype html><style>html,body{margin:0;background:black;width:100%;height:100%;overflow:hidden}video{width:100%;height:100%;object-fit:contain}</style><video src="clock.mp4" autoplay loop playsinline></video>')
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self,*_):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Quiet,directory=str(report)))
    threading.Thread(target=server.serve_forever,daemon=True).start()
    window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');time.sleep(1)
    if os.environ.get('STUDIO_TIMING_RENDERER')=='gpu':choose_dialog(window,'compositorBackend','GPU composition')
    cases=[('media-720p30','Video / audio file',1280,720,30,False),('media-720p60','Video / audio file',1280,720,60,False),('media-1080p30','Video / audio file',1920,1080,30,False),
           ('media-1080p60','Video / audio file',1920,1080,60,False),('media-portrait30','Video / audio file',1080,1920,30,False),
           ('media-portrait720p30','Video / audio file',720,1280,30,False),('media-portrait720p60','Video / audio file',720,1280,60,False),
           ('media-portrait1080p60','Video / audio file',1080,1920,60,False),
           ('browser-video30','Browser /',1280,720,30,False),('browser-overlay15','Browser /',1280,720,30,True)]
    requested=os.environ.get('STUDIO_TIMING_CASES','').split(',')
    if requested!=['']:cases=[c for c in cases if c[0] in requested]
    for name,kind,width,height,fps,transparent in cases:
        remove_sources(('QA ',));choose_dialog(window,'canvasFormat',f'{width}x{height} / {fps} fps')
        value=str(fixture) if kind.startswith('Video') else f'http://127.0.0.1:{server.server_port}/player.html'
        add_source(kind,'QA timing',value,transparent=transparent);time.sleep(3)
        path=report/(name+'.mkv');press('studioRecord');dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
        dialog.child_window(title='File name:',control_type='Edit').set_edit_text(str(path));dialog.child_window(title='Save',control_type='Button').click_input();wait_recording(True)
        before=usage();started=time.perf_counter();samples=[]
        for _ in range(int(os.environ.get('STUDIO_TIMING_SECONDS','25'))):
            time.sleep(1);s=state();samples.append({'seconds':time.perf_counter()-started,'recording':s['recording'],'source':[{k:v[k] for k in ('frames','peak','error')} for v in s['sources'] if v['name']=='QA timing']})
        after=usage();elapsed=time.perf_counter()-started
        window.capture_as_image().save(str(report/(name+'.png')));press('studioRecord');wait_recording(False)
        case={'name':name,'canvas':[width,height,fps],'treeCpuPercentOneCore':100*sum(max(0,value[0]-before[pid][0]) for pid,value in after.items() if pid in before)/elapsed,
              'privateMiBStart':sum(v[1] for v in before.values())/2**20,'privateMiBEnd':sum(v[1] for v in after.values())/2**20,'samples':samples}
        case['cpuByRole']={role:100*sum(max(0,v[0]-before[pid][0]) for pid,v in after.items() if pid in before and v[2]==role)/elapsed for role in {v[2] for v in after.values()}}
        case.update(decode_timing(path,width,height,fps));case['passed']=case['averageFps']>=fps*.95 and case['p95AbsoluteSyncMs']<=(160 if transparent else 120) and abs(case['earlyLateSyncChangeMs'])<60
        results['cases'].append(case);(report/'timing.json').write_text(json.dumps(results,indent=2));print(json.dumps({k:v for k,v in case.items() if k!='samples'}),flush=True)
finally:
    if server:server.shutdown()
    (report/'timing.json').write_text(json.dumps(results,indent=2))
assert results['cases'] and all(c['passed'] for c in results['cases']), 'Timing criteria failed; see timing.json'
