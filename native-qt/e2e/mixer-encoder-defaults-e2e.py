"""Packaged visible Studio: real moving media, software/hardware UDP publishing,
process-tree CPU, independent recording, responsive control and decoded output.
Uses the common isolated show/registry cleanup helpers; no public destinations.
"""
from pathlib import Path
source=Path(__file__).with_name('mixer-automation-e2e.py').read_text()
helpers=source[:source.index('\ntry:\n    target=cli_call')]
helpers=helpers.replace('args=parser.parse_args()',"parser.add_argument('--fixture',help='Moving 60 fps video with continuous stereo 440/880 Hz tones')\nparser.add_argument('--transport-only',action='store_true')\nparser.add_argument('--audio-only',action='store_true')\nparser.add_argument('--soak-seconds',type=int,default=0)\nparser.add_argument('--load-workers',type=int,default=0)\nargs=parser.parse_args()")
exec(helpers)
import base64,statistics,sys,re
from pywinauto import Application

game=Path(__file__).resolve().parents[3]/'game-capture/native-qt'
qa=Path(sys.executable)
fixture=Path(args.fixture).resolve() if args.fixture else report/'source.mp4'
logs=[];root=None;results={'sha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest(),'cases':[]}
ctypes.windll.kernel32.SetThreadExecutionState(0x80000003)
def launch(argv,name):
    log=(report/(name+'.log')).open('wb');logs.append(log)
    child=subprocess.Popen([str(a) for a in argv],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    owned.append(psutil.Process(child.pid));return child
try:
    if not args.fixture:
        # A received AAC/MPEG-TS clip lacks file encoder-delay metadata. Use
        # a purpose-built MP4, which carries the AAC edit list for trimming
        # priming, instead of repeating transport startup silence.
        made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=1280x720:r=60',
                  '-f','lavfi','-i','aevalsrc=0.04*sin(2*PI*440*t)|0.04*sin(2*PI*880*t):s=48000',
                  '-t','12','-c:v','libopenh264','-b:v','8000k','-threads','2','-g','120',
                  '-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-movflags','+faststart',fixture],timeout=120)
        assert made.returncode==0,made.stderr
    target=cli_call('launch','--gui','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    window=Application(backend='uia').connect(process=root.pid).window(title='Desktop Studio - Powered by VDO.Ninja')
    window.wait('visible',timeout=15);window.maximize();window.set_focus()
    def output_button():return next(c for c in window.descendants() if (c.element_info.automation_id or '').endswith('.studioStartOutput'))
    command('monitor.set',enabled=False)
    source_id=command('source.add',kind='media',name='Moving detail and stereo audio',locator=str(fixture))['id']
    if not args.audio_only:command('layer.set',source=source_id,x=0,y=0,w=1,h=1)
    until(lambda:state()['master_peak']>.01 if args.audio_only else state()['sources'][0]['frames']>30,'source playback')
    matrix=[('software-720p30',0,'software',False),('software-1080p60',2,'software',False),
            ('auto-720p30-record',0,'auto',True),('qsv-720p60',3,'qsv',False)]
    if args.soak_seconds:
        assert 60<=args.soak_seconds<=3600
        matrix=[('software-720p30-soak',0,'software',True)]
    if args.audio_only:matrix=matrix[:1]
    if args.transport_only:matrix=[]
    seconds=args.soak_seconds or 30
    if args.load_workers:assert args.soak_seconds>=180,'CPU contention needs at least a three-minute soak'
    for name,fmt,encoder,recording in matrix:
        command('canvas.set',format=fmt)
        command('output.configure',protocol='udp',url='udp://127.0.0.1:18932',encoder=encoder)
        row={'id':name,'canvasFormat':fmt,'encoder':encoder};results['cases'].append(row)
        if recording:
            command('record.start',path=str(report/(name+'-local.mkv')))
            until(lambda:state()['recording']['status'].startswith('Recording:'),'recording')
        receiver=launch([ffmpeg,'-hide_banner','-y','-i','udp://127.0.0.1:18932?fifo_size=8192&overrun_nonfatal=1',
                         '-t',str(seconds),'-map','0','-c','copy',report/(name+'.mkv')],name+'-receiver')
        time.sleep(.5);window.child_window(title='Studio',control_type='TabItem').click_input()
        output_button().invoke();until(lambda:state()['output']['live'],'publishing',45)
        monitor=launch([qa,game/'e2e/quality-resource-monitor.py','--pid',str(root.pid),'--output',report/(name+'-resources.jsonl'),'--no-hooks'],name+'-monitor')
        row['samples']=[]
        began=time.monotonic();load=None
        deadline=time.time()+seconds+30
        while receiver.poll() is None and time.time()<deadline:
            start=time.perf_counter();d=api('/diagnostics');latency=(time.perf_counter()-start)*1000
            elapsed=time.monotonic()-began
            if args.load_workers and load is None and elapsed>=seconds/3:
                load=launch([qa,game/'e2e/encoder-contention.py','--workers',str(args.load_workers),'--seconds','90'],name+'-cpu-load')
                row['load']={'startSeconds':elapsed,'requestedSeconds':90,'workers':args.load_workers}
            current=state()
            assert current['output']['live'],current['output']
            if recording:assert current['recording']['program_active'],current['recording']
            row['samples'].append({'wall':time.time(),'seconds':elapsed,'apiMs':latency,'diagnostics':d,'recordingDrops':current['recording']['dropped_frames']})
            if len(row['samples'])%30==0:
                progress={'case':name,'seconds':round(elapsed,1),'sourceFrames':current['sources'][0]['frames'],'recordingDrops':current['recording']['dropped_frames']}
                (report/'progress.json').write_text(json.dumps(progress));print(json.dumps(progress),flush=True)
            time.sleep(1)
        assert receiver.poll()==0,'Receiver did not finish'
        if load:load.wait(timeout=100);assert load.returncode==0
        row['diagnostics']=api('/diagnostics');assert row['diagnostics']['video']['configured_bitrate_kbps']==4000
        row['sources']=state()['sources']
        for child in psutil.Process(monitor.pid).children(recursive=True):child.terminate()
        monitor.terminate();monitor.wait(timeout=5)
        output_button().invoke();until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'output stopped')
        if recording:
            command('record.stop');until(lambda:not state()['recording']['busy'],'recording finalized')
        data=api('/studio/screenshot',{'target':'window','max_width':1280})
        (report/(name+'.png')).write_bytes(base64.b64decode(data['data']))
        (report/'results.json').write_text(json.dumps(results,indent=2))
    if args.transport_only:
        def track():return next(s for s in state()['sources'] if s['id']==source_id)
        pictures=[]
        for cue in (3000,5500):
            command('media.set',id=source_id,paused=True,seekMs=cue)
            until(lambda:abs(track()['positionMs']-cue)<80,'paused cue')
            pos=track()['positionMs'];time.sleep(.4);assert track()['positionMs']==pos
            data=api('/studio/screenshot',{'target':'program','max_width':640})
            raw=base64.b64decode(data['data']);(report/('cue-'+str(cue)+'.png')).write_bytes(raw)
            pictures.append(np.asarray(Image.open(io.BytesIO(raw)).convert('RGB')).astype(float))
        assert np.mean(abs(pictures[0]-pictures[1]))>.5,'Paused seek did not update the picture'
        command('media.set',id=source_id,paused=False,loop=False,seekMs=11000)
        until(lambda:track()['ended'],'non-looping EOF',5)
        command('media.set',id=source_id,restart=True,loop=True)
        until(lambda:not track()['ended'] and track()['positionMs']<2000,'restart')
        before=track()['frames'];command('media.set',id=source_id,seekMs=11999)
        until(lambda:track()['frames']>before+30 and track()['positionMs']<2000,'end cue loops safely',5)
        results['transport']={'pausedSeek':True,'pauseHoldsPosition':True,'eof':True,'restart':True,'endCueLoop':True}
    cli_call('quit');root.wait(timeout=30);target=None
    # Decode only after CPU measurements finish.
    for file in (f for f in report.glob('*.mkv') if f!=fixture):
        r=run([ffmpeg,'-v','error','-xerror','-i',file,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=120)
        assert r.returncode==0 and not r.stderr,(file,r.stderr)
        r=run([ffmpeg,'-v','error','-i',file,'-vn','-ac','2','-ar','48000','-f','f32le','pipe:1'],timeout=60)
        assert r.returncode==0 and len(r.stdout)>48000*8,(file,'Missing recorded audio')
        pcm=np.frombuffer(r.stdout,dtype=np.float32).reshape(-1,2)
        blocks=pcm[:len(pcm)//4800*4800].reshape(-1,4800,2)
        tones=[float(np.max(np.abs(np.sum(blocks[:,:,ch]*np.exp(-2j*np.pi*hz*np.arange(4800)/48000),axis=1))*2/4800))
               for ch,hz in [(0,440),(1,880)]]
        assert min(tones)>.01,(file,tones)
        steady=pcm[48000:-48000];steady=steady[:len(steady)//480*480].reshape(-1,480,2)
        minimum_rms=float(np.min(np.sqrt(np.mean(steady*steady,axis=1))))
        assert minimum_rms>.005,(file,'Audio gap',minimum_rms)
        results.setdefault('decodedFiles',[]).append({'file':file.name,'clean':True,'stereoTonePeaks':tones,'minimum10msRms':minimum_rms})
    for row in results['cases']:
        samples=[json.loads(s) for s in (report/(row['id']+'-resources.jsonl')).read_text().splitlines()][5:-3]
        row['cpuMachineMean']=statistics.mean(s['treeCpuPercentOneCore']/s['logicalCpus'] for s in samples)
        row['treeRssMiB']=statistics.mean(s['treeRss']/1048576 for s in samples)
        row['apiP95Ms']=float(np.percentile([s['apiMs'] for s in row['samples']],95))
        row['cleanDecode']=True
    for row in results['cases']:
        decoded=run([ffmpeg,'-hide_banner','-i',report/(row['id']+'.mkv'),'-an','-vf','scale=160:90,showinfo',
                     '-fps_mode','passthrough','-pix_fmt','rgb24','-f','rawvideo','pipe:1'],timeout=120)
        assert decoded.returncode==0
        frames=np.frombuffer(decoded.stdout,np.uint8).reshape(-1,90,160,3)
        times=np.array([float(x) for x in re.findall(r'Parsed_showinfo[^\n]*?\bn:\s*\d+\s+pts:.*?pts_time:([-\d.]+)',decoded.stderr.decode(errors='replace'))])
        assert len(frames)==len(times) and np.all(np.diff(times)>0)
        seconds=times[-1]-times[0];fps=30 if row['canvasFormat']==0 else 60
        distinct=float(np.count_nonzero(np.any(frames[1:]!=frames[:-1],axis=(1,2,3)))/seconds)
        row['quality']={'decodedFps':float((len(frames)-1)/seconds),'distinctFramesPerSecond':distinct,
                        'p95FrameGapMs':float(np.percentile(np.diff(times)*1000,95))}
        assert (args.audio_only or distinct>fps*.9) and row['quality']['p95FrameGapMs']<1500/fps,(row['id'],row['quality'])
    results['passed']=True
except BaseException as error:
    results['error']=repr(error);raise
finally:
    ctypes.windll.kernel32.SetThreadExecutionState(0x80000000)
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    for proc in reversed(owned):
        try:
            for child in proc.children(recursive=True):child.kill()
            if proc.is_running():proc.kill();proc.wait(timeout=10)
        except psutil.NoSuchProcess:pass
    for log in logs:log.close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'results.json').write_text(json.dumps(results,indent=2))
