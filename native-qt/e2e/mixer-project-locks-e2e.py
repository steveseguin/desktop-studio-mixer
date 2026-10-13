"""Two packaged Studio processes must never autosave/export into each other's project."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
second=None
try:
    target=cli_call('launch','--headless','--show',str(show));first=target;owned.append(psutil.Process(first['pid']))
    other=report/'second.mixer.json';target=None
    second=cli_call('launch','--headless','--show',str(other));owned.append(psutil.Process(second['pid']))
    target=second;command('scene.add',name='Second protected');command('show.save',path=str(other))
    protected=other.read_bytes();target=first
    for operation in ('show.save','show.saveAs','show.open'):
        payload={'command':operation,'path':str(other)}
        if operation=='show.open':payload['activate']=True
        api('/studio/command',payload,400)
    assert other.read_bytes()==protected
    active=report/'New active.mixer.json';command('show.saveAs',path=str(active));assert Path(state()['project_path'])==active
    target=second
    api('/studio/command',{'command':'show.save','path':str(active)},400)
    # The old workspace becomes available once Save As successfully commits.
    command('show.open',path=str(show),activate=True);assert Path(state()['project_path'])==show
    target=first;api('/studio/command',{'command':'show.saveAs','path':str(show)},400)
    # Invalid Open must retain the active lease and current state.
    bad=report/'bad.mixer.json';bad.write_text('{broken')
    api('/studio/command',{'command':'show.open','path':str(bad),'activate':True},400)
    assert Path(state()['project_path'])==active
    target=second;api('/studio/command',{'command':'show.open','path':str(active),'activate':True},400)
    target=first;cli_call('quit');psutil.Process(first['pid']).wait(timeout=30);first=None
    target=second;command('show.open',path=str(active),activate=True);assert Path(state()['project_path'])==active
    results={'passed':True,'snapshotSaveAsAndOpenRespectOtherInstance':True,'saveAsTransfersLease':True,'failedOpenKeepsLease':True,'quitReleasesLease':True}
finally:
    for session in (locals().get('first'),second):
        if session:
            target=session
            try:cli_call('quit');psutil.Process(session['pid']).wait(timeout=30)
            except Exception:pass
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'project-locks.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
