"""Real bounded ENOSPC via a private WSL tmpfs exposed to Windows over UNC.

Uses an existing Ubuntu WSL distribution, no host disk filling or admin changes.
All mount/file operations target a newly-created unique /tmp directory.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import shutil
mount='/tmp/desktop-studio-disk-'+uuid.uuid4().hex
assert mount.startswith('/tmp/desktop-studio-disk-') and len(mount.rsplit('/',1)[1])==52
share=Path('\\\\wsl.localhost\\Ubuntu'+mount.replace('/','\\'));mounted=False;mtx=None;log=None;keepalive=None
def linux(*argv,check=True):
 p=run(['wsl','--distribution','Ubuntu','--user','root','--exec',*argv],timeout=45)
 if check:assert p.returncode==0,p.stderr.decode(errors='replace')
 return p
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def receive(name):
 p=report/(name+'.mkv')
 r=run([ffmpeg,'-v','error','-y','-rtsp_transport','tcp','-i',f'rtsp://127.0.0.1:{rtsp}/disk','-t','4','-c','copy',p],timeout=30)
 assert r.returncode==0,r.stderr
 r=run([ffmpeg,'-v','error','-i',p,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=30)
 assert r.returncode==0 and not r.stderr,r.stderr
 return {'file':p.name,'clean':True,'bytes':p.stat().st_size}
try:
 # Keep the disposable mount alive even when Windows briefly closes its UNC
 # handles; WSL otherwise shuts an idle distribution down between commands.
 keepalive=subprocess.Popen(['wsl','--distribution','Ubuntu','--user','root','--exec','python3','-c','import sys; sys.stdin.read()'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
 assert linux('test','-e',mount,check=False).returncode==1
 linux('mkdir','--',mount);linux('mount','-t','tmpfs','-o','size=256m,noexec,nodev,nosuid','tmpfs',mount);mounted=True
 assert linux('findmnt','-n','-o','FSTYPE','--target',mount).stdout.strip()==b'tmpfs'
 results['boundedFilesystemBytes']=shutil.disk_usage(share).total
 assert results['boundedFilesystemBytes']<=300*1024**2
 target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
 clip=report/'source.mp4'
 made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=640x360:r=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','4','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',clip],timeout=30)
 assert made.returncode==0,made.stderr
 command('source.add',kind='media',name='Disk failure motion and tone',locator=str(clip));time.sleep(2)
 rtsp,rtmp,whip,udp,srt=[port() for _ in range(5)]
 config=report/'receiver.yml';config.write_text(f'''logLevel: warn
rtspAddress: 127.0.0.1:{rtsp}
rtspTransports: [tcp]
rtmpAddress: 127.0.0.1:{rtmp}
webrtcAddress: 127.0.0.1:{whip}
webrtcLocalUDPAddress: 127.0.0.1:{udp}
webrtcIPsFromInterfaces: false
webrtcAdditionalHosts: [127.0.0.1]
webrtcICEServers2: []
srtAddress: 127.0.0.1:{srt}
hls: false
moq: false
paths:
  all_others:
''')
 log=(report/'receiver.log').open('wb');mtx=subprocess.Popen([args.mediamtx,str(config)],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW);time.sleep(1);assert mtx.poll() is None
 command('output.configure',protocol='rtmp',url=f'rtmp://127.0.0.1:{rtmp}/disk',encoder='auto');command('output.start');until(lambda:state()['output']['live'],'Publisher live')
 low=share/'low-space.mkv';command('record.start',path=str(low));until(lambda:state()['recording']['status'].startswith('Recording:'),'Record to bounded disk');time.sleep(4)
 linux('dd','if=/dev/zero','of='+mount+'/filler','bs=1M','count=150','status=none')
 results['freeAfterLowSpaceFill']=shutil.disk_usage(share).free
 until(lambda:not state()['recording']['busy'],'Low-space stop',20)
 results['lowSpaceGuard']=state()['recording'];assert '128 MB' in results['lowSpaceGuard']['status'],results['lowSpaceGuard']
 assert state()['output']['live'];results['publishingSurvivedLowSpace']=receive('after-low-space')
 shutil.copy2(low,report/low.name)
 command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'Publisher stop')
 linux('rm','--',mount+'/filler')
 # The main file publisher has no ProgramRecorder low-space preflight. Fill its
 # filesystem while it writes, exercising a real native muxer write failure.
 backup=report/'unaffected-independent.mkv';command('record.start',path=str(backup));until(lambda:state()['recording']['active'],'Independent recording')
 full=share/'full-disk.mkv';command('output.configure',protocol='file',url=str(full),encoder='auto');command('output.start');until(lambda:state()['output']['live'],'File publisher');time.sleep(3)
 fill=linux('dd','if=/dev/zero','of='+mount+'/filler','bs=1M','count=300','status=none',check=False)
 assert fill.returncode!=0 and shutil.disk_usage(share).free<1024**2,fill.stderr
 until(lambda:state()['output']['state']=='error','Disk-full publishing error',30)
 results['fullDiskError']=state()['output'];assert state()['recording']['active'];time.sleep(3)
 assert not results['fullDiskError']['status'].startswith('Publishing'),results['fullDiskError']
 command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'Failed file output stop')
 command('record.stop');until(lambda:not state()['recording']['busy'],'Independent recording finalize')
 shutil.copy2(full,report/full.name)
 for path in (report/low.name,backup):
  check=run([ffmpeg,'-v','error','-i',path,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=40)
  assert check.returncode==0 and not check.stderr,check.stderr
 results['unaffectedRecordingsDecoded']=True;results['passed']=True
finally:
 if target:
  try:cli_call('quit');root.wait(timeout=25)
  except Exception:pass
 if mtx and mtx.poll() is None:mtx.terminate();mtx.wait(timeout=10)
 if log:log.close()
 if mounted:
  if linux('findmnt','-n','-o','FSTYPE','--target',mount,check=False).stdout.strip()==b'tmpfs':linux('umount','--',mount)
  if linux('test','-d',mount,check=False).returncode==0:linux('rmdir','--',mount)
  results['temporaryMountRemoved']=True
 if keepalive:keepalive.stdin.close();keepalive.wait(timeout=15)
 for key,value in saved.items():
  if value is None:
   try:winreg.DeleteValue(settings,key)
   except FileNotFoundError:pass
  else:winreg.SetValueEx(settings,key,0,value[1],value[0])
 winreg.CloseKey(settings)
 results['originalShowUnchanged']=(hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
 (report/'disk-failure.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
