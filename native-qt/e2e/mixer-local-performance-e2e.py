"""Measure local-output cost and verify cached static pictures still switch and clock."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text().split('\ntry:\n    target=')[0])

def usage():
    data={}
    for p in [root,*root.children(recursive=True)]:
        try:
            c=p.cpu_times();args=p.cmdline();role='app' if p.pid==root.pid else args[args.index('--studio-worker')+1] if '--studio-worker' in args else p.name()
            data[p.pid]=(c.user+c.system,p.memory_info().private/2**20,role)
        except psutil.Error:pass
    return data

def measure():
    time.sleep(2);before=usage();began=time.monotonic();time.sleep(12);after=usage();elapsed=time.monotonic()-began;cpu={}
    for pid,(seconds,memory,role) in after.items():
        if pid in before:cpu[role]=cpu.get(role,0)+(seconds-before[pid][0])/elapsed*100
    return {'seconds':elapsed,'cpuPercentOneCore':cpu,'treePrivateMiB':sum(row[1] for row in after.values())}

try:
    target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
    results['staticOutputsOff']=measure();command('local.set',spout=True,virtualCamera=True)
    until(lambda:not state()['local_outputs']['configuring'],'registration');results['staticOutputsOn']=measure()
    raw=report/'static-switch.rgb';log=(report/'consumer.log').open('wb')
    receiver=subprocess.Popen([str(ffmpeg),'-v','error','-y','-f','dshow','-video_size','1280x720','-framerate','30','-i','video=Desktop Studio Virtual Camera','-t','8','-vf','scale=32:32','-pix_fmt','rgb24','-f','rawvideo',str(raw)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(3);command('scene.select',id=green);receiver.wait(timeout=25);log.close();assert receiver.returncode==0,(report/'consumer.log').read_text()
    pixels=np.frombuffer(raw.read_bytes(),np.uint8).reshape(-1,32,32,3)[:,16,16,:].astype(int)
    red_frames=int(np.sum((pixels[:,0]>120)&(pixels[:,1]<50)));green_frames=int(np.sum((pixels[:,1]>120)&(pixels[:,0]<50)))
    assert len(pixels)>=230 and red_frames>=20 and green_frames>=60,(len(pixels),red_frames,green_frames)
    clip=report/'motion.mp4';r=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=1280x720:r=30','-t','20','-c:v','libopenh264',clip],timeout=45);assert r.returncode==0
    command('source.add',kind='media',name='Motion',locator=str(clip));results['movingOutputsOn']=measure()
    command('local.set',spout=False,virtualCamera=False);results['movingOutputsOff']=measure()
    results['staticConsumer']={'decodedFrames':len(pixels),'redFrames':red_frames,'greenFrames':green_frames};results['passed']=True
finally:
    if target:
        try:
            until(lambda:not state()['local_outputs']['configuring'],'registration cleanup');command('local.set',spout=False,virtualCamera=False,unregisterCamera=True);until(lambda:not state()['local_outputs']['configuring'],'unregister cleanup')
            cli_call('quit');root.wait(timeout=30)
        except Exception:pass
    for key,value in saved.items():
        if value is None:
            try:winreg.DeleteValue(settings,key)
            except FileNotFoundError:pass
        else:winreg.SetValueEx(settings,key,0,value[1],value[0])
    winreg.CloseKey(settings)
    assert (hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
    (report/'local-performance.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
