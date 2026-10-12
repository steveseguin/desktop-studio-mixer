"""Additional real packaged workflows: Unicode, show ownership and preferences."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
try:
    unicode_show=report/'Café-猫.mixer.json';show.replace(unicode_show)
    target=cli_call('launch','--headless','--show',str(unicode_show));owned.append(psutil.Process(target['pid']))
    assert [s['id'] for s in state()['scenes']]==[red,green],state()['scenes']
    command('workspace.set',mode='single');command('scene.select',id=green)
    # A second instance must not steal/autosave the same show.
    duplicate=run(['node',cli,'launch','--headless','--show',unicode_show],text=True,encoding='utf-8',timeout=45)
    assert duplicate.returncode!=0 and state()['preview']==green
    command('scene.rename',id=green,name='Café 猫')
    assert next(s for s in state()['scenes'] if s['id']==green)['name']=='Café 猫'
    original=image('program','program-before.png')
    for theme in ('original','rose','graphite','neon','sand','original'):
        command('preferences.set',theme=theme)
        pixels=image('program',theme+'-program-'+str(time.time_ns())+'.png')
        assert pixels.tobytes()==original.tobytes()
    api('/studio/screenshot',{'target':'program','max_width':50000},400)
    before=state()['output']['protocol']
    api('/studio/command',{'command':'output.configure','protocol':'srt','url':'https://bad.invalid'},400)
    assert state()['output']['protocol']==before
    command('preferences.set',language='fr')
    cli_call('quit');owned[-1].wait(timeout=20);target=None
    target=cli_call('launch','--gui','--show',str(unicode_show));owned.append(psutil.Process(target['pid']))
    from pywinauto import Application
    from pywinauto.keyboard import send_keys
    window=Application(backend='uia').connect(process=target['pid']).window(title='Desktop Studio - Powered by VDO.Ninja')
    window.wait('visible',timeout=20);window.set_focus()
    names=[e.element_info.name for e in window.descendants(control_type='MenuItem')]
    for name in ('Fichier','Affichage','Langue','Apparence','Aide'):assert name in names,(name,names)
    send_keys('%p');time.sleep(.2);send_keys('es',pause=.2);time.sleep(.2)
    assert state()['layout']=='simple'
    image('window','french.png')
    results={'unicodeShowPathAndNames':True,'concurrentShowOwnerRejected':True,
        'themesPreserveProgramPixels':True,'invalidOutputDoesNotMutate':True,'frenchMenusAndKeyboard':True}
finally:
    if target:
        try:cli_call('quit')
        except Exception:pass
    for p in owned:
        try:p.wait(timeout=20)
        except psutil.NoSuchProcess:pass
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    results['originalShowUnchanged']=True
    (report/'edge-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results,indent=2))
