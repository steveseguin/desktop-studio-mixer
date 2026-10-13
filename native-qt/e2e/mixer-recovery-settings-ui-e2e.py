"""Use the packaged GUI to configure recovery, publish, stop and reopen settings."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import sys
from pywinauto import Application
from pywinauto.keyboard import send_keys
root=receiver=None;receiver_log=None;results={}
project_settings=winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\VDO.Ninja\Video Mixer\mixer')
try:previous_project=winreg.QueryValueEx(project_settings,'activeProject')
except FileNotFoundError:previous_project=None

def ui(name):
    return next(c for c in window.descendants() if (c.element_info.automation_id or '').endswith('.'+name))

def input_spin(name,value):
    control=ui(name);control.click_input(coords=(25,control.rectangle().height()//2))
    send_keys('^a'+str(value)+'{TAB}',pause=.04)

def screenshot(name):
    path=report/name
    picture=api('/studio/screenshot',{'target':'window','max_width':1600})
    import base64
    path.write_bytes(base64.b64decode(picture['data']))

def settings_page():
    window.child_window(title='Studio',control_type='TabItem').click_input()
    ui('mixerOutput').click_input();time.sleep(.4)

def stop_app():
    children=root.children(recursive=True);cli_call('quit');root.wait(timeout=35)
    _,alive=psutil.wait_procs(children,timeout=10)
    assert not alive,[p.pid for p in alive]

try:
    # --studio-show deliberately disables publisher preference persistence.
    # Select only this private show for the normal GUI launch instead.
    winreg.SetValueEx(project_settings,'activeProject',0,winreg.REG_SZ,str(show))
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    receiver_log=(report/'receiver.log').open('wb')
    receiver=subprocess.Popen([sys.executable,'-B',str(Path(__file__).with_name('whip-reference-receiver.py')),
        '--port',str(port),'--report',str(report/'receiver')],stdout=receiver_log,stderr=receiver_log,creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(2);assert receiver.poll() is None
    target=cli_call('launch','--gui');root=psutil.Process(target['pid'])
    window=Application(backend='uia').connect(process=root.pid).window(title='Desktop Studio - Powered by VDO.Ninja')
    window.wait('visible',timeout=15);window.maximize();window.set_focus()
    command('monitor.set',enabled=False)
    command('source.add',kind='media',name='Recovery settings clock',locator=os.environ['STUDIO_CLOCK_FIXTURE'])
    until(lambda:state()['sources'][0]['frames']>30,'media')
    command('output.configure',protocol='whip',url=f'http://127.0.0.1:{port}/whip/whip',encoder='auto',bitrate_kbps=3500)
    settings_page()
    assert ui('outputWhipRedCheck').get_toggle_state()==1
    assert state()['output']['whip_history_ms']==1500 and state()['output']['whip_playout_ms']==500
    input_spin('outputWhipHistorySpin',2000);input_spin('outputWhipPlayoutSpin',250)
    ui('outputWhipRedCheck').click_input();time.sleep(.4)
    assert state()['output']['whip_history_ms']==2000 and state()['output']['whip_playout_ms']==250
    assert not state()['output']['whip_audio_red_requested']
    screenshot('advanced-recovery.png')
    # Invalid automation values must not silently clamp or change the active profile.
    for fields in ({'whip_history_ms':249},{'whip_history_ms':3001},{'whip_playout_ms':1001},{'whip_audio_red':'yes'}):
        api('/studio/command',{'command':'output.configure','protocol':'whip','url':f'http://127.0.0.1:{port}/whip/whip',**fields},expected=400)
    assert state()['output']['whip_history_ms']==2000
    window.child_window(title='Studio',control_type='TabItem').click_input()
    ui('studioStartOutput').click_input();until(lambda:state()['output']['live'],'GUI publishing',40);time.sleep(4)
    live=state()['output'];assert live['video_rtx'] and live['video_nack'] and live['video_pli'];assert not live['audio_red']
    results['live']=live
    settings_page()
    for name in ('outputWhipHistorySpin','outputWhipPlayoutSpin','outputWhipRedCheck'):assert not ui(name).is_enabled()
    screenshot('live-recovery-status.png')
    window.child_window(title='Studio',control_type='TabItem').click_input()
    ui('studioStartOutput').click_input();until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'GUI stop')
    stop_app();target=None
    target=cli_call('launch','--gui');root=psutil.Process(target['pid'])
    window=Application(backend='uia').connect(process=root.pid).window(title='Desktop Studio - Powered by VDO.Ninja')
    window.wait('visible',timeout=15);window.maximize();settings_page()
    restored=state()['output'];results['persisted']=restored
    assert restored['whip_history_ms']==2000 and restored['whip_playout_ms']==250 and not restored['whip_audio_red_requested']
    results['persisted']=restored;screenshot('reopened-settings.png');results['passed']=True
finally:
    if target:
        try:stop_app()
        except Exception:
            if root.is_running():root.kill();root.wait(timeout=10)
    if receiver and receiver.poll() is None:receiver.terminate();receiver.wait(timeout=10)
    if receiver_log:receiver_log.close()
    if previous_project is None:
        try:winreg.DeleteValue(project_settings,'activeProject')
        except FileNotFoundError:pass
    else:winreg.SetValueEx(project_settings,'activeProject',0,previous_project[1],previous_project[0])
    winreg.CloseKey(project_settings)
    winreg.CloseKey(settings)
    assert hashlib.sha256(user_show.read_bytes()).hexdigest()==original_hash
    (report/'recovery-settings.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
assert results.get('passed')
