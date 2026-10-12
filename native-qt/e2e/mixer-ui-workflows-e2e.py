"""Run isolated, visible GUI workflows in the packaged app, one at a time.

This runner only launches/closes apps. Workflow actions use Windows UI Automation
and mouse/keyboard input, never Studio API commands. A failure leaves the owned
window available for investigation. Private media and screenshots stay in reports.
"""
import argparse,json,os,pathlib,subprocess,sys,time
import psutil
from pywinauto import Application
p=argparse.ArgumentParser();p.add_argument('--report',required=True);p.add_argument('--mediamtx',default=os.environ.get('MEDIAMTX_PATH',''));p.add_argument('workflows',nargs='+');a=p.parse_args()
base=pathlib.Path(a.report).resolve();base.mkdir(parents=True,exist_ok=True)
executable=pathlib.Path('dist/desktop-studio-portable/desktop-studio.exe').resolve();results=[]
for name in a.workflows:
    script=pathlib.Path('e2e')/('mixer-'+name+'-e2e.py');assert script.is_file(),script
    report=base/name;report.mkdir(exist_ok=False);show=report/'show.mixer.json'
    env=dict(os.environ,STUDIO_E2E_SHOW=str(show),STUDIO_E2E_POINTER='1',
             MEDIAMTX_PATH=a.mediamtx,STUDIO_E2E_DISCOVERY=str(report/'control.json'),
             WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS='--remote-debugging-port=0 --remote-debugging-address=127.0.0.1')
    log=(report/'app.log').open('wb')
    process=subprocess.Popen([str(executable),'--studio-show='+str(show),'--local-control-discovery='+str(report/'control.json')],env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    app=Application(backend='uia').connect(process=process.pid,timeout=30)
    window=app.window(title='Desktop Studio - Powered by VDO.Ninja');window.wait('visible',timeout=30);window.set_focus()
    (report/'session.json').write_text(json.dumps({'pid':process.pid,'show':str(show),'discovery':str(report/'control.json')}))
    print('RUNNING '+name+' PID '+str(process.pid),flush=True);started=time.monotonic()
    with (report/'workflow.log').open('wb') as output:
        workflow=subprocess.run([sys.executable,'-B',str(script),'--pid',str(process.pid),'--report',str(report)],env=env,stdout=output,stderr=output)
    current_pid=int((report/'app.pid').read_text()) if (report/'app.pid').exists() else process.pid
    current=psutil.Process(current_pid)
    app=Application(backend='uia').connect(process=current_pid,timeout=20)
    window=app.window(title='Desktop Studio - Powered by VDO.Ninja')
    window.set_focus();window.capture_as_image().save(str(report/'final-window.png'))
    result={'workflow':name,'exitCode':workflow.returncode,'seconds':round(time.monotonic()-started,2)};results.append(result)
    (base/'ui-workflows.json').write_text(json.dumps(results,indent=2));print(json.dumps(result),flush=True)
    if workflow.returncode:
        log.close();raise SystemExit(workflow.returncode)
    # X may intentionally minimize to the tray. File > Quit has Ctrl+Q and
    # releases devices regardless of that user preference.
    descendants=current.children(recursive=True);window.type_keys('^q');current.wait(timeout=30);log.close()
    deadline=time.monotonic()+10
    while any(x.is_running() for x in descendants) and time.monotonic()<deadline:time.sleep(.2)
    assert not any(x.is_running() for x in descendants),'Owned workers survived UI close'
