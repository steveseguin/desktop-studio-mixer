"""Private RTMP receiver under bandwidth starvation, plus untrusted RTMPS rejection."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])
import ssl
assert args.mediamtx
receiver=None;proxy=None;tls=None;log=None;stop=threading.Event();throttle=threading.Event();connections=[];tls_errors=[]
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def relay(source,dest,limited):
    try:
        while not stop.is_set():
            data=source.recv(16384)
            if not data:break
            if limited and throttle.is_set():time.sleep(.125)
            dest.sendall(data)
    except OSError:pass
    finally:
        for s in (source,dest):
            try:s.shutdown(socket.SHUT_RDWR);s.close()
            except OSError:pass
def accept_proxy():
    while not stop.is_set():
        try:
            client,_=proxy.accept();upstream=socket.create_connection(('127.0.0.1',rtmp));connections.extend([client,upstream])
            for src,dst,limited in [(client,upstream,True),(upstream,client,False)]:threading.Thread(target=relay,args=(src,dst,limited),daemon=True).start()
        except OSError:break
def receive(name):
    path=report/(name+'.mkv')
    for attempt in range(6):
        r=run([ffmpeg,'-v','error','-y','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/qa','-t','5','-c','copy',path],timeout=20)
        if r.returncode==0:return decode(path,allow_gaps=True)
        time.sleep(1)
    raise AssertionError(r.stderr.decode(errors='replace'))
try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    clip=report/'network.mp4';r=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=1280x720:r=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','10','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',clip],timeout=45);assert r.returncode==0,r.stderr
    sid=command('source.add',kind='media',name='Network test',locator=str(clip))['id'];until(lambda:next(s for s in state()['sources'] if s['id']==sid)['frames']>20,'source')
    rtsp,rtmp=port(),port();config=report/'receiver.yml';config.write_text(f'logLevel: warn\nrtspAddress: 127.0.0.1:{rtsp}\nrtspTransports: [tcp]\nrtmpAddress: 127.0.0.1:{rtmp}\nwebrtc: false\nsrt: false\nhls: false\nmoq: false\npaths:\n  all_others:\n')
    log=(report/'receiver.log').open('wb');receiver=subprocess.Popen([args.mediamtx,str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW);time.sleep(1)
    proxy=socket.socket();proxy.bind(('127.0.0.1',0));proxy.listen();threading.Thread(target=accept_proxy,daemon=True).start()
    recording=report/'network-independent.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['active'],'record')
    command('output.configure',protocol='rtmp',url=f'rtmp://127.0.0.1:{proxy.getsockname()[1]}/qa',encoder='auto',bitrate_kbps=6000);command('output.start');until(lambda:state()['output']['live'],'live')
    results['before']=receive('before');throttle.set();samples=[]
    for n in range(35):
        began=time.perf_counter();s=state();samples.append({'queryMs':(time.perf_counter()-began)*1000,'privateMiB':sum(p.memory_info().private for p in [root,*root.children(recursive=True)] if p.is_running())/2**20})
        assert s['recording']['active'];time.sleep(1)
    throttle.clear()
    # Drop only fixture connections, forcing recovery from any stale TCP backlog.
    for s in connections:
        try:s.shutdown(socket.SHUT_RDWR);s.close()
        except OSError:pass
    results['after']=receive('after');results['starvation']={'seconds':35,'p95ApiMs':float(np.percentile([s['queryMs'] for s in samples],95)),'privateMiBRange':[min(s['privateMiB'] for s in samples),max(s['privateMiB'] for s in samples)]}
    command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'stop')
    # Ephemeral self-signed fixture certificate; never alter Windows trust stores.
    r=run(['C:/Program Files/Git/usr/bin/openssl.exe','req','-x509','-newkey','rsa:2048','-nodes','-keyout',report/'key.pem','-out',report/'cert.pem','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost,IP:127.0.0.1'],timeout=20);assert r.returncode==0,r.stderr
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(report/'cert.pem',report/'key.pem')
    tls=socket.socket();tls.bind(('127.0.0.1',0));tls.listen()
    def accept_tls():
        while not stop.is_set():
            try:client,_=tls.accept()
            except OSError:break
            try:
                with context.wrap_socket(client,server_side=True) as stream:tls_errors.append('UNEXPECTED TRUST: '+repr(stream.recv(8)))
            except ssl.SSLError as e:tls_errors.append(str(e))
    threading.Thread(target=accept_tls,daemon=True).start()
    command('output.configure',protocol='rtmp',url=f'rtmps://127.0.0.1:{tls.getsockname()[1]}/qa',encoder='auto',bitrate_kbps=6000);command('output.start')
    until(lambda:bool(tls_errors),'TLS rejection',30);assert not any('UNEXPECTED TRUST' in e for e in tls_errors),tls_errors
    assert state()['recording']['active'];command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'TLS output stopped')
    results['untrustedRtmpsCertificate']=tls_errors
    command('record.stop');until(lambda:not state()['recording']['busy'],'record finalized');results['independentRecording']=decode(recording,allow_gaps=True);results['passed']=True
finally:
    stop.set()
    for s in [*connections,proxy,tls]:
        if s:
            try:s.close()
            except OSError:pass
    if target:
        try:cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    if receiver and receiver.poll() is None:receiver.terminate();receiver.wait(timeout=10)
    if log:log.close()
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'network-080.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2),flush=True)
