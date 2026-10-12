"""Real encoded recordings and hardware diagnostics from the packaged app."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
helpers=Path(__file__).with_name('mixer-production-e2e.py').read_text()
exec(helpers[helpers.index('def source_row'):helpers.index('\nclass Fixture')])
import urllib.request, psutil, numpy as np

results={};root=psutil.Process(args.pid)
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()
discovery=json.loads(Path('e2e/reports/studio-isolation/control.json').read_text())
def diagnostics():
    return json.load(urllib.request.urlopen(urllib.request.Request(discovery['base_url']+'/diagnostics',headers={'Authorization':'Bearer '+discovery['token']}),timeout=5))
def until(fn,label,timeout=40):
    end=time.time()+timeout
    while time.time()<end:
        value=fn()
        if value:return value
        time.sleep(.3)
    raise AssertionError(label)
def workers():
    return [p for p in root.children() if '--studio-worker' in p.cmdline() and 'encoder' in p.cmdline()]
def decode(path):
    check=subprocess.run([str(ffmpeg),'-v','error','-i',str(path),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,text=True,timeout=60)
    assert check.returncode==0 and not check.stderr,check.stderr
    frames=subprocess.check_output([str(ffmpeg),'-v','error','-i',str(path),'-an','-vf','scale=64:36','-fps_mode','passthrough','-enc_time_base','1:1000000','-f','framemd5','-'],text=True)
    values=[v for v in frames.splitlines() if v and not v.startswith('#')]
    assert len(values)>80,len(values)
    pts=[int(v.split(',')[2]) for v in values]
    assert all(b>a for a,b in zip(pts,pts[1:])), 'non-monotonic recording timestamps'
    return {'decodedFrames':len(values),'videoFps':round((len(pts)-1)*1e6/(pts[-1]-pts[0]),2)}

window.set_focus();window.child_window(title='Studio',control_type='TabItem').click_input();studio_mode(False);scene('Main');remove_sources(('QA ',))
enabled(control('guestProgramReturn'),False);add_source('Video / audio file','QA encoder media',str(media_fixture()))
choose_dialog(window,'canvasFormat','1920x1080 / 60 fps')
try:
    for mode,category in [('Auto (Hardware, then Software)',None),('NVIDIA NVENC','NVIDIA'),('Intel Quick Sync','Intel'),('Software','Software')]:
        path=report/(mode.split()[0].lower()+'.mkv')
        configure('Record to file (MKV)',path);press('mixerOutput');enabled(control('advancedToggle'),True);select('encoderSelect',mode)
        window.child_window(title='Studio',control_type='TabItem').click_input();press('studioStartOutput');wait_output(True)
        until(lambda: diagnostics().get('output',{}).get('state')==2,'encoder output never became live')
        time.sleep(5);data=diagnostics();video=data['video']
        evidence={key:video.get(key) for key in ['active_encoder','active_encoder_category','active_codec','hardware_encoder','encoder_input_format','encoder_fallback_reason','encode_failures']}
        assert video['hardware_encoder'] == (category!='Software'),evidence
        if category: assert video['active_encoder_category']==category,evidence
        pids=[p.pid for w in workers() for p in [w]+w.children(recursive=True)];assert pids,'encoder was not isolated'
        # Kernel engine counters corroborate the selected hardware path when the
        # Windows counter provider is available. Values are scoped to our PIDs.
        pattern='^pid_('+'|'.join(map(str,pids))+')_'
        command="$samples=Get-Counter '\\GPU Engine(*)\\Utilization Percentage' -SampleInterval 1 -MaxSamples 2 -ErrorAction SilentlyContinue; $samples.CounterSamples | Where-Object { $_.InstanceName -match '"+pattern+"' -and $_.InstanceName -match 'engtype_VideoEncode' } | Select-Object InstanceName,CookedValue | ConvertTo-Json -Compress"
        counters=subprocess.run(['powershell','-NoProfile','-Command',command],capture_output=True,text=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
        try:evidence['gpuEncodeCounters']=json.loads(counters.stdout) if counters.stdout.strip() else []
        except ValueError:evidence['gpuEncodeCounters']=[]
        press('studioStartOutput');wait_output(False);evidence.update(decode(path));results[mode]=evidence
        (report/'hardware.json').write_text(json.dumps(results,indent=2));print(mode,evidence,flush=True)
    # An encoder process dying cannot take the main window down. Auto can
    # replace the failed backend and begin a fresh recording explicitly.
    path=report/'encoder-crash.mkv';configure('Record to file (MKV)',path);press('mixerOutput');select('encoderSelect','Auto (Hardware, then Software)')
    window.child_window(title='Studio',control_type='TabItem').click_input();press('studioStartOutput');wait_output(True)
    until(lambda:diagnostics().get('output',{}).get('state')==2,'recording did not start');time.sleep(2)
    victim=workers()[0];old=victim.pid;victim.kill();victim.wait(timeout=8)
    until(lambda:any(p.pid!=old for p in workers()),'encoder helper did not recover',20);time.sleep(8)
    assert root.is_running() and len(window.descendants())>40
    results['encoderTermination']={'appSurvived':True,'state':diagnostics()['output']['state'],'replacementWorkers':len(workers())}
    assert results['encoderTermination']['state']==2,results['encoderTermination']
    if control('studioStartOutput').window_text() in ('STOP','STOP RECORDING'):press('studioStartOutput');wait_output(False)
    results['encoderTermination'].update(decode(path))
finally:
    (report/'hardware.json').write_text(json.dumps(results,indent=2))
    try:
        if control('studioStartOutput').window_text() in ('STOP','STOP RECORDING'):press('studioStartOutput');wait_output(False)
    except Exception:pass
