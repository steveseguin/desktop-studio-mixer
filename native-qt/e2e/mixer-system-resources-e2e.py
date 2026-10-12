"""Real visible CPU/RAM readout while idle and recording, compared with Windows."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import sys,re
from pywinauto import Application
loads=[];results={'samples':[]}
try:
 target=cli_call('launch','--gui','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
 window=Application(backend='uia').connect(process=target['pid']).window(title='Desktop Studio - Powered by VDO.Ninja');window.wait('visible',timeout=20)
 time.sleep(3)
 meter=next(e for e in window.descendants() if e.element_info.automation_id.endswith('.studioSystemResources'))
 assert meter.is_visible() and re.search(r'CPU \d+(?:\.\d+)?%',meter.element_info.name),meter.element_info.name
 results['idleReadout']=meter.element_info.name;assert not state()['output']['active']
 psutil.cpu_percent()
 for n in range(18):
  if n==5:
   command('record.start',path=str(report/'cpu-monitor.mkv'));until(lambda:state()['recording']['active'],'Independent recording')
   for _ in range(3):
    p=subprocess.Popen([sys.executable,'-B','-c','import time; stop=time.monotonic()+7\nwhile time.monotonic()<stop: sum(i*i for i in range(3000))'],creationflags=subprocess.CREATE_NO_WINDOW);loads.append(p)
  time.sleep(1);system=psutil.cpu_percent();data=state()
  results['samples'].append({'second':n,'windowsCpu':system,'studioCpu':data['system']['cpu_percent'],'windowsMemory':psutil.virtual_memory().percent,'studioMemory':data['system']['memory_percent']})
 image('window','system-readout.png');command('record.stop');until(lambda:not state()['recording']['busy'],'Recording finalized')
 errors=[abs(x['windowsCpu']-x['studioCpu']) for x in results['samples']]
 results['medianCpuDifferencePercentagePoints']=float(np.median(errors));results['maximumCpuDifferencePercentagePoints']=max(errors)
 assert np.median(errors)<5,errors
 assert all(abs(s['windowsMemory']-s['studioMemory'])<2 for s in results['samples'])
 results['recordingWithoutPublishing']=True;results['logicalProcessors']=psutil.cpu_count();results['passed']=True
finally:
 if target:
  try:cli_call('quit');root.wait(timeout=20)
  except Exception:pass
 for p in loads:p.wait(timeout=15)
 for key,value in saved.items():
  if value is None:
   try:winreg.DeleteValue(settings,key)
   except FileNotFoundError:pass
  else:winreg.SetValueEx(settings,key,0,value[1],value[0])
 winreg.CloseKey(settings)
 (report/'system-resources.json').write_text(json.dumps(results,indent=2));print(json.dumps({k:v for k,v in results.items() if k!='samples'},indent=2))
