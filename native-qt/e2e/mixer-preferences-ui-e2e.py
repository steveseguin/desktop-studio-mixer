"""Normal-profile mode/language restart checks, with show/settings backups.

Run alone after other GUI workflows. --studio-show intentionally avoids global
mode persistence, so it cannot establish normal launch behavior.
"""
import argparse,json,os,subprocess,time,hashlib,psutil,winreg
from pathlib import Path
from pywinauto import Application
p=argparse.ArgumentParser();p.add_argument('--report',required=True);a=p.parse_args()
report=Path(a.report).resolve();report.mkdir(parents=True,exist_ok=False)
show=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/studio.mixer.json'
original=show.read_bytes();(report/'original-show.mixer.json').write_bytes(original)
settings=report/'settings.reg'
subprocess.run(['reg','export',r'HKCU\Software\VDO.Ninja\Video Mixer',str(settings),'/y'],check=True,capture_output=True)
settings_root=r'Software\VDO.Ninja\Video Mixer'
def registry_names(path):
    """Snapshot names only; the private .reg backup retains their values."""
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path) as key:
        children,values,_=winreg.QueryInfoKey(key)
        result={path:{winreg.EnumValue(key,i)[0] for i in range(values)}}
        for i in range(children):result.update(registry_names(path+'\\'+winreg.EnumKey(key,i)))
        return result
original_names=registry_names(settings_root)
exe=Path('dist/desktop-studio-portable/desktop-studio.exe').resolve()
results={};process=None;window=None
def launch():
    global process,window
    process=subprocess.Popen([str(exe)],creationflags=subprocess.CREATE_NO_WINDOW)
    app=Application(backend='uia').connect(process=process.pid,timeout=25)
    window=app.window(title='Desktop Studio - Powered by VDO.Ninja');window.wait('visible',timeout=25);window.set_focus();time.sleep(.5)
def quit():
    global process
    window.set_focus();window.type_keys('{ESC 2}');window.type_keys('^q');process.wait(timeout=25);process=None
def mode():
    return next(c for c in window.descendants(control_type='CheckBox') if (c.element_info.automation_id or '').endswith('.studioMode'))
try:
    launch()
    for wanted in (True,False):
        if bool(mode().get_toggle_state())!=wanted:mode().toggle()
        assert bool(mode().get_toggle_state())==wanted
        quit();launch();assert bool(mode().get_toggle_state())==wanted
        window.capture_as_image().save(str(report/(('studio' if wanted else 'single')+'-reopened.png')))
    results['normalLaunchModePersistence']='both modes restored after process restart'
    current='Language'
    for label,menu,file in [('Fran\u00e7ais','Langue','french'),('Espa\u00f1ol','Idioma','spanish'),('English','Language','english')]:
        window.menu_select(current+'->'+label);quit();launch()
        assert any(c.window_text().replace('&','')==menu for c in window.descendants(control_type='MenuItem')),menu
        window.capture_as_image().save(str(report/(file+'.png')));current=menu
    results['languageMenuPersistence']='French, Spanish and English applied on restart'
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\VDO.Ninja\Video Mixer\ui') as key:
        try:checked=str(winreg.QueryValueEx(key,'minimizeToTrayOnClose')[0]).lower() not in ('false','0')
        except FileNotFoundError:checked=True
    if checked:window.menu_select('View->Minimize To Tray On Close')
    window.type_keys('{ESC 2}');window.close();process.wait(timeout=25);process=None
    results['explicitClosePreference']='X exits when tray-on-close is disabled'
finally:
    if process and process.poll() is None:quit()
    show.write_bytes(original)
    # Import merges values; explicitly remove preferences created by this run.
    current_names=registry_names(settings_root)
    for path in sorted(current_names,key=len,reverse=True):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path,0,winreg.KEY_SET_VALUE) as key:
            for name in current_names[path]-original_names.get(path,set()):winreg.DeleteValue(key,name)
        if path not in original_names:winreg.DeleteKey(winreg.HKEY_CURRENT_USER,path)
    subprocess.run(['reg','import',str(settings)],check=True,capture_output=True)
    assert registry_names(settings_root)==original_names
    results['originalShowRestored']=hashlib.sha256(show.read_bytes()).hexdigest()==hashlib.sha256(original).hexdigest()
    (report/'preferences.json').write_text(json.dumps(results,indent=2));print(json.dumps(results),flush=True)
