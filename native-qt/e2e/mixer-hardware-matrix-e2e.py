"""Packaged API-driven CPU/GPU composition and encoder matrix at 1080p30/60."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
results={'cases':[]};root=None
def workers():
 answer=[]
 for p in root.children():
  try:
   if '--studio-worker' in p.cmdline() and 'encoder' in p.cmdline():answer.extend([p,*p.children(recursive=True)])
  except psutil.Error:pass
 return answer
def usage():
 answer={}
 for p in [root,*root.children(recursive=True)]:
  try:c=p.cpu_times();answer[p.pid]=(c.user+c.system,p.memory_info().private)
  except psutil.Error:pass
 return answer
def decode_case(path):
 result=run([ffmpeg,'-v','error','-i',path,'-an','-vf','scale=64:36','-fps_mode','passthrough','-enc_time_base','1:1000000','-f','framemd5','-'],text=True,timeout=90)
 assert result.returncode==0 and not result.stderr,result.stderr
 rows=[line for line in result.stdout.splitlines() if line and not line.startswith('#')]
 pts=[int(line.split(',')[2]) for line in rows];assert len(pts)>60
 assert all(b>a for a,b in zip(pts,pts[1:])), 'Non-monotonic timestamps'
 sound=run([ffmpeg,'-v','error','-i',path,'-vn','-ac','1','-ar','48000','-f','f32le','-'],timeout=45)
 assert sound.returncode==0 and not sound.stderr,sound.stderr
 samples=np.frombuffer(sound.stdout,dtype='<f4')[48000:96000];assert len(samples)==48000
 tone=float(abs(np.sum(samples*np.exp(-2j*np.pi*731*np.arange(len(samples))/48000)))*2/len(samples));assert tone>.015,tone
 return {'frames':len(pts),'fps':(len(pts)-1)*1e6/(pts[-1]-pts[0]),'distinctFrames':len(set(line.split(',')[-1] for line in rows)),
   'maximumFrameIntervalMs':max(b-a for a,b in zip(pts,pts[1:]))/1000,'tone731Hz':tone,'audioDecode':'clean'}
try:
 clip=report/'moving60.mp4'
 made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=1280x720:r=60','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','8','-c:v','libopenh264','-b:v','8000k','-pix_fmt','yuv420p','-c:a','aac',clip],timeout=50)
 assert made.returncode==0,made.stderr
 target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
 source=command('source.add',kind='media',name='Matrix motion',locator=str(clip))['id']
 command('audio.set',source=source,monitored=False)
 until(lambda:next(s for s in state()['sources'] if s['id']==source)['frames']>30,'Media frames')
 for fps,format in [(30,1),(60,2)]:
  for renderer in ('cpu','gpu'):
   command('canvas.set',format=format,renderer=renderer)
   for encoder in ('auto','nvenc','qsv','software','amf'):
    name=f'{fps}-{renderer}-{encoder}';output=report/(name+'.mkv')
    case={'name':name,'fpsTarget':fps,'renderer':renderer,'requestedEncoder':encoder}
    try:
     command('output.configure',protocol='file',url=str(output),encoder=encoder,bitrate_kbps=18000 if fps==60 else 12000)
     command('output.start')
     if encoder=='amf':
      until(lambda:state()['output']['state']=='error' or state()['output']['live'],'Unavailable encoder result',40)
      failed=state()['output']
      if failed['state']=='error':
       assert not failed['active'] and 'AMD' in failed['status'],failed
       case.update(passed=True,unavailableHardwareRejected=True,status=failed['status'])
       results['cases'].append(case);(report/'matrix.json').write_text(json.dumps(results,indent=2));print(json.dumps(case),flush=True)
       continue
     until(lambda:state()['output']['live'],'File encoder became live',40);time.sleep(2)
     case['canvas']=state()['canvas'];assert case['canvas']['active_renderer']==renderer,case['canvas']
     diag=api('/diagnostics')['video']
     case['encoder']={k:diag.get(k) for k in ['active_encoder','active_encoder_category','hardware_encoder','encoder_input_format','encoder_fallback_reason','encode_failures']}
     pids=[root.pid,*[p.pid for p in workers()]];before=usage();start=time.perf_counter()
     pattern='^pid_('+'|'.join(map(str,pids))+')_'
     query="$v=Get-Counter '\\GPU Engine(*)\\Utilization Percentage' -SampleInterval 1 -MaxSamples 4 -ErrorAction SilentlyContinue; $v.CounterSamples | Where-Object {$_.InstanceName -match '"+pattern+"' -and $_.InstanceName -match 'engtype_(VideoEncode|3D|Copy)'} | Select-Object InstanceName,CookedValue | ConvertTo-Json -Compress"
     counter=run(['powershell','-NoProfile','-Command',query],text=True,timeout=25)
     case['gpuEncodeCounters']=json.loads(counter.stdout) if counter.stdout.strip() else []
     if time.perf_counter()-start<5:time.sleep(5-(time.perf_counter()-start))
     after=usage();elapsed=time.perf_counter()-start
     case['treeCpuPercentOneCore']=100*sum(max(0,c[0]-before[p][0]) for p,c in after.items() if p in before)/elapsed
     case['privateMiB']=sum(c[1] for c in after.values())/2**20
     case['endDiagnostics']= {k:v for k,v in api('/diagnostics')['video'].items() if k in ('frames_dropped','encode_failures','encode_timeouts','frame_rate','hardware_encoder','active_encoder_category','encoder_fallback_reason')}
     command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'File output stopped')
     case.update(decode_case(output));case['passed']=case['fps']>=fps*.90 and not case['encoder']['encode_failures']
     if encoder=='software':assert not case['encoder']['hardware_encoder']
     if encoder in ('nvenc','qsv'):assert case['encoder']['hardware_encoder']
    except Exception as error:
     case['passed']=False;case['error']=str(error)
     try:command('output.stop');until(lambda:not state()['output']['active'] and not state()['output']['stopping'],'Failed output stopped')
     except Exception:pass
    results['cases'].append(case);(report/'matrix.json').write_text(json.dumps(results,indent=2));print(json.dumps(case),flush=True)
finally:
 if target:
  try:cli_call('quit');root.wait(timeout=30)
  except Exception:pass
 for key,value in saved.items():
  if value is None:
   try:winreg.DeleteValue(settings,key)
   except FileNotFoundError:pass
  else:winreg.SetValueEx(settings,key,0,value[1],value[0])
 winreg.CloseKey(settings)
 results['originalShowUnchanged']=(hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
 (report/'matrix.json').write_text(json.dumps(results,indent=2))
assert all(c.get('passed') for c in results['cases']), 'Some matrix cases failed; see report'
