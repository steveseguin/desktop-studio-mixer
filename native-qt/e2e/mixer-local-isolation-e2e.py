"""Real GUI local outputs: kill/suspend producers while consumers and recording stay open."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-local-outputs-050-e2e.py').read_text().split('\ntry:\n    enabled')[0])
import psutil,ctypes
root=psutil.Process(args.pid)
ffmpeg=Path('dist/desktop-studio-portable/ffmpeg/bin/ffmpeg.exe').resolve()

def worker(kind):
    for p in root.children():
        try:
            c=p.cmdline()
            if '--studio-worker' in c and c[c.index('--studio-worker')+1]==kind:return p
        except psutil.Error:pass

def wait(check,seconds=25):
    stop=time.monotonic()+seconds
    while time.monotonic()<stop:
        value=check()
        if value:return value
        time.sleep(.15)
    raise AssertionError('Local-output condition timed out')

def recover(kind,suspend=False):
    victim=wait(lambda:worker(kind));before=time.monotonic()
    if suspend=='native':
        api=ctypes.WinDLL('kernel32',use_last_error=True)
        api.OpenThread.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong];api.OpenThread.restype=ctypes.c_void_p
        api.GetThreadDescription.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p)]
        api.SuspendThread.argtypes=[ctypes.c_void_p];api.CloseHandle.argtypes=[ctypes.c_void_p];api.LocalFree.argtypes=[ctypes.c_void_p]
        suspended=False
        for thread in victim.threads():
            handle=api.OpenThread(0x0802,False,thread.id)
            if not handle:continue
            description=ctypes.c_void_p()
            try:
                if api.GetThreadDescription(handle,ctypes.byref(description))>=0 and description.value and ctypes.wstring_at(description.value)=='Desktop Studio native local output':
                    assert api.SuspendThread(handle)!=0xffffffff;suspended=True
            finally:
                if description.value:api.LocalFree(description)
                api.CloseHandle(handle)
        assert suspended,'Native output thread was not identified'
    elif suspend:victim.suspend()
    else:victim.kill();victim.wait(timeout=10)
    replacement=wait(lambda:(p if p and p.pid!=victim.pid else None) if (p:=worker(kind)) else None)
    return {'kind':kind,'failure':'native thread suspended' if suspend=='native' else 'suspended' if suspend else 'terminated','recoverySeconds':round(time.monotonic()-before,2),'replacementPid':replacement.pid}

def spout_picture():
    probe=subprocess.run([str(Path('build-mixer/qa/studio_spout_output_probe.exe').resolve())],capture_output=True,text=True,timeout=25)
    assert probe.returncode==0,probe.stdout+probe.stderr
    data=json.loads(next(s for s in probe.stdout.splitlines() if s.startswith('{')))
    assert data['width']==width and data['height']==height,data
    return data

measure='''async()=>{const canvas=document.createElement('canvas');canvas.width=64;canvas.height=36;const c=canvas.getContext('2d',{willReadFrequently:true});let frames=0;const hashes=new Set();
await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error('Camera picture did not recover')),20000);function frame(){c.drawImage(v,0,0,64,36);let hash=0;for(const x of c.getImageData(0,0,64,36).data)hash=(hash*31+x)|0;hashes.add(hash);if(++frames>=60){clearTimeout(timeout);resolve();}else v.requestVideoFrameCallback(frame)}v.requestVideoFrameCallback(frame)});
return {frames,distinctFrames:hashes.size,width:v.videoWidth,height:v.videoHeight};}'''
browser=None
try:
    recording=report/'local-failures.mkv';press('studioRecord')
    dialog=window.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
    field=dialog.child_window(title='File name:',control_type='Edit');field.set_edit_text(str(recording));field.set_focus();field.type_keys('{ENTER}');wait_recording(True)
    enabled(control('spoutOutput'),True);enabled(control('virtualCamera'),True)
    wait(lambda:worker('spout-output'));wait(lambda:worker('virtual-output'));time.sleep(2)
    results['spoutInitial']=spout_picture()
    with sync_playwright() as pw:
        browser=pw.chromium.launch(channel='msedge',headless=False,args=['--autoplay-policy=no-user-gesture-required','--use-fake-ui-for-media-stream'])
        page=browser.new_page(permissions=['camera']);page.goto(f'http://127.0.0.1:{server.server_port}/')
        page.evaluate('''async()=>{const d=(await navigator.mediaDevices.enumerateDevices()).find(x=>x.kind==='videoinput'&&x.label==='Desktop Studio Virtual Camera');window.qaCamera=await navigator.mediaDevices.getUserMedia({video:{deviceId:{exact:d.deviceId}},audio:false});v.srcObject=qaCamera;await v.play()}''')
        def camera_picture():
            result=page.evaluate(measure);assert result['distinctFrames']>20 and result['width']==width and result['height']==height,result;return result
        results['cameraInitial']=camera_picture()
        camera_pid=worker('virtual-output').pid
        results['spoutCrash']=recover('spout-output');results['spoutAfterCrash']=spout_picture()
        assert worker('virtual-output').pid==camera_pid
        results['cameraAfterSpoutCrash']=camera_picture();wait_recording(True)
        spout_pid=worker('spout-output').pid
        results['cameraCrash']=recover('virtual-output');results['cameraAfterCrash']=camera_picture()
        assert worker('spout-output').pid==spout_pid
        results['cameraHang']=recover('virtual-output',suspend=True);results['cameraAfterHang']=camera_picture();wait_recording(True)
        results['cameraThirdFailure']=recover('virtual-output',suspend='native');camera_picture()
        victim=worker('virtual-output');victim.kill();victim.wait(timeout=10)
        window.set_focus();wait(lambda:'paused after repeated failures' in control('mixerStatus').window_text())
        time.sleep(3);assert worker('virtual-output') is None
        assert worker('spout-output').pid==spout_pid;results['spoutWhileCameraPaused']=spout_picture();wait_recording(True)
        enabled(control('virtualCamera'),False);enabled(control('virtualCamera'),True);wait(lambda:worker('virtual-output'))
        results['manualCameraRecovery']=camera_picture();page.screenshot(path=str(report/'recovered-camera.png'));browser.close();browser=None
    window.set_focus();press('studioRecord');wait_recording(False)
    decoded=subprocess.run([str(ffmpeg),'-v','error','-i',str(recording),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=120)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    results['independentRecording']='fully decoded';results['passed']=True
finally:
    if browser:
        try:browser.close()
        except Exception:pass
    window.set_focus();enabled(control('spoutOutput'),False);enabled(control('virtualCamera'),False)
    wait(lambda:control('unregisterCamera').is_enabled());press('unregisterCamera');wait(lambda:control('unregisterCamera').is_enabled())
    server.shutdown();(report/'local-isolation.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2),flush=True)
