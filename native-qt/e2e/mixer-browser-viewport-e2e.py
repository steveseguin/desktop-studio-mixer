"""Actual portrait browser output at both canvas sizes and both capture modes."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])

class Page(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'<!doctype html><style>body{margin:0;background:white}#fill{position:fixed;inset:0;background:#22cc44}#edge{position:fixed;right:0;bottom:0;width:15vw;height:15vh;background:#2244cc}#tick{position:fixed;left:0;bottom:0;width:4px;height:4px;background:white}</style><div id=fill></div><div id=edge></div><i id=tick></i><script>let n=0;setInterval(()=>tick.style.left=(n++%10)+"px",50)</script>'
        self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*args):pass

try:
    browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Page);threading.Thread(target=browser_server.serve_forever,daemon=True).start()
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    for width,height,fmt in ((720,1280,5),(1080,1920,4)):
        command('canvas.set',format=fmt)
        for transparent in (True,False):
            sid=command('source.add',kind='browser',name='Viewport check',locator=f'http://127.0.0.1:{browser_server.server_port}/',browserWidth=width,browserHeight=height,transparent=transparent)['id']
            until(lambda:next(s for s in state()['sources'] if s['id']==sid)['frames']>20,'browser frames',45)
            name=f'{width}x{height}-'+('transparent' if transparent else 'opaque');frame=image('program',name+'.png')
            for x,y,rgb in ((.05,.05,(34,204,68)),(.5,.5,(34,204,68)),(.95,.95,(34,68,204))):
                actual=frame.getpixel((int(frame.width*x),int(frame.height*y)));assert max(abs(a-b) for a,b in zip(actual,rgb))<8,(name,x,y,actual)
            source=next(s for s in state()['sources'] if s['id']==sid)
            results[name]={'fullViewportEdgesCorrect':True,'transparent':source['transparent'],'frames':source['frames']}
            command('scene.select',id=green);command('source.remove',id=sid);command('scene.select',id=red)
    results['passed']=True
finally:
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    if browser_server:browser_server.shutdown();browser_server.server_close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'browser-viewport.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
