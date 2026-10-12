"""Launch an isolated visible Studio and portable NVDA for keyboard workflows."""
import argparse,json,subprocess,sys,time,uuid,psutil
from pathlib import Path
from pywinauto import Application
p=argparse.ArgumentParser();p.add_argument('--report',required=True);p.add_argument('--nvda',required=True);p.add_argument('--package',default='dist/desktop-studio-portable');a=p.parse_args()
report=Path(a.report).resolve();report.mkdir(parents=True,exist_ok=False)
assert not any(p.name().lower() in ('nvda.exe','nvda_noUIAccess.exe','nvda_uiAccess.exe') for p in psutil.process_iter()), 'An NVDA instance is already running'
show=report/'show.mixer.json';show.write_text(json.dumps({'version':1,'sources':[],'scenes':[{'id':str(uuid.uuid4()),'name':name,'background':'#183657','layers':[]} for name in ('Opening','Main')]}))
config=report/'nvda-profile';config.mkdir();(config/'nvda.ini').write_text('''[general]
language = en
saveConfigurationOnExit = False
showWelcomeDialogAtStartup = False
askToExit = False
playStartAndExitSounds = False
[speech]
synth = espeak
[[espeak]]
volume = 0
[update]
autoCheck = False
startupNotification = False
allowUsageStats = False
[mouse]
enableMouseTracking = False
''')
package=Path(a.package).resolve();fixture=report/'tone.mp4'
subprocess.run([str(package/'ffmpeg/bin/ffmpeg.exe'),'-v','error','-y','-f','lavfi','-i','testsrc2=size=640x360:rate=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','3','-c:v','libopenh264','-c:a','aac',str(fixture)],check=True,timeout=30)
cli=package/'tools/mcp/studio-cli.mjs';target=None;nvda=None
try:
    launch=subprocess.run(['node',str(cli),'launch','--gui','--show',str(show)],capture_output=True,text=True,check=True,timeout=40)
    target=json.loads(launch.stdout);(report/'session.json').write_text(json.dumps(target))
    nvda=subprocess.Popen([a.nvda,'--config-path='+str(config),'--log-file='+str(report/'nvda.log'),'--log-level=10','--disable-addons','--minimal'],creationflags=subprocess.CREATE_NO_WINDOW)
    deadline=time.monotonic()+20
    while not (report/'nvda.log').exists() and time.monotonic()<deadline:time.sleep(.2)
    time.sleep(3)
    result=subprocess.run([sys.executable,'-B','e2e/mixer-nvda-e2e.py','--session',str(report/'session.json'),'--report',str(report),'--nvda-log',str(report/'nvda.log'),'--fixture',str(fixture),'--package',str(package)])
    assert result.returncode==0,'Keyboard/NVDA workflow failed; see report'
finally:
    if target:
        window=Application(backend='uia').connect(process=target['pid']).window(title='Desktop Studio - Powered by VDO.Ninja')
        window.capture_as_image().save(str(report/'keyboard-final.png'));window.set_focus();window.type_keys('{ESC 2}');window.type_keys('^q');psutil.Process(target['pid']).wait(30)
    if nvda:
        subprocess.run([a.nvda,'--quit'],timeout=20);nvda.wait(timeout=20)
