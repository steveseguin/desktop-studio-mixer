"""Close the packaged app during a large asset copy; no partial collection escapes."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
asset=report/'large image.png'
try:
    Image.new('RGB',(1,1),'red').save(asset)
    with asset.open('r+b') as f:f.truncate(2*1024**3)
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    sid=command('source.add',kind='image',name='Large local asset',locator=str(asset))['id']
    command('show.collect',path=str(report/'Interrupted collection'))
    assert state()['show_collection']['busy']
    began=time.monotonic();api('/commands',{'command':'quit'});root.wait(timeout=30)
    assert not (report/'Interrupted collection').exists()
    assert not list(report.glob('.desktop-studio-*'))
    results={'passed':True,'shutdownSeconds':round(time.monotonic()-began,3),'partialCollectionVisible':False,'temporaryFoldersRemain':False}
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    if asset.exists():asset.unlink()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'collection-shutdown.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
