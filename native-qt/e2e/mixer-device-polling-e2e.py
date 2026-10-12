"""Measure real recording cadence/control latency while clients enumerate devices."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import concurrent.futures
results={'phases':[]};root=None
try:
 clip=report/'source.mp4'
 made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=1280x720:r=60','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','4','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',clip],timeout=40)
 assert made.returncode==0,made.stderr
 target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid'])
 command('canvas.set',format=3,renderer='cpu');command('source.add',kind='media',name='Device-query motion',locator=str(clip));time.sleep(3)
 recording=report/'device-polling.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['status'].startswith('Recording:'),'Recording start');time.sleep(2)
 def enumerate_route(route):
  started=time.perf_counter();response=api(route);assert response['ok'];return {'route':route,'ms':(time.perf_counter()-started)*1000,'devices':len(response['sources'])}
 for polling in (False,True):
  start=time.monotonic();before=state()['recording']['dropped_frames'];latencies=[];tasks=[];next_scan=0
  with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
   while time.monotonic()-start<20:
    elapsed=time.monotonic()-start
    if polling and elapsed>=next_scan:
     tasks.extend(pool.submit(enumerate_route,route) for route in ('/sources/cameras','/sources/audio-inputs'));next_scan+=2
    tick=time.perf_counter();s=state();latencies.append((time.perf_counter()-tick)*1000);assert s['recording']['active'];time.sleep(.1)
  results['phases'].append({'devicePolling':polling,'p95StateMs':float(np.percentile(latencies,95)),'maximumStateMs':max(latencies),
    'recordingDropped':state()['recording']['dropped_frames']-before,'enumerations':[t.result() for t in tasks]})
 command('record.stop');until(lambda:not state()['recording']['busy'],'Recording finalized');results['recording']=decode(recording,allow_gaps=True)
 results['responsiveUnderPolling']=results['phases'][1]['maximumStateMs']<150
 if os.environ.get('STUDIO_E2E_EXPECT_RESPONSIVE')=='1':assert results['responsiveUnderPolling'],results
 # Quit while multiple clients have device queries outstanding. They may finish
 # or disconnect, but the native process must exit without retaining a worker.
 with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
  pending=[pool.submit(enumerate_route,'/sources/cameras') for _ in range(8)];time.sleep(.02)
  descendants=root.children(recursive=True);tick=time.monotonic();cli_call('quit');root.wait(timeout=20)
  results['quitDuringDeviceScanMs']=(time.monotonic()-tick)*1000
  for task in pending:
   try:task.result()
   except (OSError,AssertionError):pass
 time.sleep(.5);assert not any(p.is_running() for p in descendants)
 results['ownedWorkersExited']=True;results['passed']=True
finally:
 if target and root and root.is_running():
  try:cli_call('quit');root.wait(timeout=20)
  except Exception:pass
 for key,value in saved.items():
  if value is None:
   try:winreg.DeleteValue(settings,key)
   except FileNotFoundError:pass
  else:winreg.SetValueEx(settings,key,0,value[1],value[0])
 winreg.CloseKey(settings)
 results['originalShowUnchanged']=(hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
 (report/'device-polling.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
