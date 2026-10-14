"""Packaged Studio UDP/RIST UI publishing, decoded A/V and muxer isolation."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import base64
from pywinauto import Application
from pywinauto.keyboard import send_keys

logs=[];root=None
results={'cases':[],'sha256':hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest()}
ctypes.windll.kernel32.SetThreadExecutionState(0x80000003)
def launch(argv,name):
    log=(report/(name+'.log')).open('wb');logs.append(log)
    child=subprocess.Popen([str(a) for a in argv],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    owned.append(psutil.Process(child.pid));return child
def ui(name):return next(c for c in window.descendants() if (c.element_info.automation_id or '').endswith('.'+name))
def screenshot(name):
    data=api('/studio/screenshot',{'target':'window','max_width':1600})
    (report/(name+'.png')).write_bytes(base64.b64decode(data['data']))
def receiver(protocol,name):
    url='udp://127.0.0.1:18912?fifo_size=8192&overrun_nonfatal=1' if protocol=='udp' else 'rist://@127.0.0.1:18914'
    opts=['-buffer_size','200'] if protocol=='rist' else []
    return launch([ffmpeg,'-hide_banner','-y',*opts,'-i',url,'-t','8','-map','0','-c','copy',report/(name+'.mkv')],name)
try:
    target=cli_call('launch','--gui','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    window=Application(backend='uia').connect(process=root.pid).window(title='Desktop Studio - Powered by VDO.Ninja')
    window.wait('visible',timeout=15);window.maximize();window.set_focus()
    command('monitor.set',enabled=False)
    fixture=report/'source.mp4'
    made=run([ffmpeg,'-v','error','-f','lavfi','-i','testsrc2=s=640x360:r=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000',
              '-t','4','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',fixture],timeout=30)
    assert made.returncode==0,made.stderr
    source=command('source.add',kind='media',name='Datagram picture and tone',locator=str(fixture))['id']
    until(lambda:state()['sources'][0]['frames']>30,'source playback');command('layer.set',source=source,x=0,y=0,w=1,h=1)
    recording=report/'concurrent-recording.mkv';command('record.start',path=str(recording))
    until(lambda:state()['recording']['status'].startswith('Recording:'),'recording')
    for protocol,index,url in [('udp',7,'udp://127.0.0.1:18912'),('rist',8,'rist://127.0.0.1:18914')]:
        window.child_window(title='Studio',control_type='TabItem').click_input();ui('mixerOutput').click_input();time.sleep(.3)
        label='RIST' if protocol=='rist' else 'UDP / MPEG-TS (LAN)'
        ui('outputSelect').expand()
        def choices():return [c for w in Application(backend='uia').connect(process=root.pid).windows() for c in w.descendants(control_type='ListItem') if c.is_visible() and c.window_text()==label]
        until(lambda:bool(choices()),'protocol choice');choices()[0].click_input()
        assert ui('outputSelect').selected_text()==label,ui('outputSelect').selected_text()
        assert ui('bitrateSelect').selected_text()=='Standard (4000 kbps)'
        ui('outputUrlInput').set_edit_text(url)
        assert not any(c.is_visible() for c in window.descendants() if (c.element_info.automation_id or '').endswith('.outputSecretInput'))
        screenshot(protocol+'-settings')
        child=receiver(protocol,protocol+'-initial')
        window.child_window(title='Studio',control_type='TabItem').click_input();ui('studioStartOutput').click_input()
        until(lambda:state()['output']['live'],protocol+' live',45)
        assert api('/diagnostics')['video']['configured_bitrate_kbps']==4000
        child.wait(timeout=35);assert child.returncode==0
        row={'protocol':protocol,'initial':decode(report/(protocol+'-initial.mkv'))};results['cases'].append(row)
        diag=api('/diagnostics');mux=diag['output']['muxer_pid'];previous=diag['output']['reconnects']
        assert mux in [p.pid for p in root.children(recursive=True)]
        child=receiver(protocol,protocol+'-recovered');psutil.Process(mux).kill()
        until(lambda:api('/diagnostics').get('output',{}).get('reconnects',0)>previous and state()['output']['live'],'muxer recovery',45)
        child.wait(timeout=35);assert child.returncode==0
        row['recovered']=decode(report/(protocol+'-recovered.mkv'));assert state()['recording']['active']
        screenshot(protocol+'-recovered')
        ui('studioStartOutput').invoke();until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'stopped')
    # Explicit API rate choices still override the default, and invalid URLs /
    # unsupported credential fields fail before starting a network worker.
    command('output.configure',protocol='udp',url='udp://127.0.0.1:18912',bitrate_kbps=4100)
    ui('mixerOutput').invoke();time.sleep(.3)
    assert ui('bitrateSelect').selected_text()=='Custom'
    for fields in [dict(protocol='udp',url='udp://127.0.0.1'),dict(protocol='rist',url='udp://127.0.0.1:18914'),
                   dict(protocol='rist',url='rist://127.0.0.1:18914',credential='unsupported')]:
        api('/studio/command',{'command':'output.configure',**fields},expected=400)
    command('record.stop');until(lambda:not state()['recording']['busy'],'recording finalized')
    results['recording']=decode(recording)
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=30);target=None
    _,alive=psutil.wait_procs(children,timeout=10);assert not alive
    results['passed']=True
except BaseException as error:
    results['error']=repr(error)
    raise
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
    (report/'datagram-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
