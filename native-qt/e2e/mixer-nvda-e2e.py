"""Real NVDA + keyboard production workflow against a separately launched package.

Requires NVDA debug speech logging and a fresh two-scene show (Opening, Main).
Pass the JSON returned by studio-cli launch as --session; private media/logs stay
in --report. The caller owns application and NVDA startup/shutdown.
"""
import json,time,urllib.request,subprocess,argparse,os
from pathlib import Path
from pywinauto import Application
from pywinauto.keyboard import send_keys
from pywinauto.uia_defines import IUIA
parser=argparse.ArgumentParser()
parser.add_argument('--session',required=True);parser.add_argument('--report',required=True)
parser.add_argument('--nvda-log',required=True);parser.add_argument('--fixture',required=True)
parser.add_argument('--package',default='dist/desktop-studio-portable')
args=parser.parse_args();root=Path(args.report).resolve();root.mkdir(parents=True,exist_ok=True)
log=Path(args.nvda_log).resolve()
session=json.loads(Path(args.session).read_bytes());d=json.loads(Path(session['discovery']).read_bytes())
w=Application(backend='uia').connect(process=session['pid']).window(title='Desktop Studio - Powered by VDO.Ninja')
results={};start_log=log.stat().st_size
def api(route,body=None):
 req=urllib.request.Request(d['base_url']+route,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+d['token'],'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=15) as r:return json.load(r)
def state():return api('/studio/state')
def key(keys):send_keys(keys,pause=.06);time.sleep(.5)
def text(value):send_keys(value,with_spaces=True,pause=.025);time.sleep(.4)
def focus(suffix):
 for n in range(220):
  f=IUIA().iuia.GetFocusedElement()
  if f.CurrentAutomationId.endswith('.'+suffix):time.sleep(.8);return f.CurrentName
  send_keys('{TAB}',pause=.03);time.sleep(.06)
 raise AssertionError('Keyboard cannot reach '+suffix)
def add(kind,name,value):
 focus('addSource');key('{SPACE}');focus('sourceKind');key('{HOME}{DOWN '+str(kind)+'}');key('{TAB}')
 focus('sourceName');text(name);focus('sourceValue');key('^a');text(value);key('{ENTER}');time.sleep(1)
 assert any(s['name']==name for s in state()['sources']),state()['sources']
def until(fn,timeout=30):
 for _ in range(timeout*4):
  if fn():return
  time.sleep(.25)
 raise AssertionError('Timed out')
w.set_focus();time.sleep(1);focus('addSource');key('^2');assert next(s for s in state()['scenes'] if s['id']==state()['preview'])['name']=='Main'
add(8,'NVDA title','Accessible production')
# Submit an unavailable file, hear the reason, then correct it using only keys.
focus('addSource');key('{SPACE}');focus('sourceKind');key('{HOME}{DOWN 6}');key('{TAB}')
focus('sourceName');text('NVDA media');focus('sourceValue');text(str(root/'missing-file.mp4'));key('{ENTER}')
assert not any(s['name']=='NVDA media' for s in state()['sources'])
focus('sourceValue');key('^a');text(str(Path(args.fixture).resolve()));key('{ENTER}');time.sleep(1)
assert any(s['name']=='NVDA media' for s in state()['sources'])
results['keyboardValidationRecovery']=True
source=next(s for s in state()['sources'] if s['name']=='NVDA media')['id']
results['keyboardAddedTitleAndMedia']=True
focus('audio_mute_'+source);key('{SPACE}');assert next(s for s in state()['sources'] if s['id']==source)['muted']
key('{SPACE}');assert not next(s for s in state()['sources'] if s['id']==source)['muted']
results['keyboardMute']=True
focus('audio_gain_'+source);key('{HOME}{RIGHT 50}');assert abs(next(s for s in state()['sources'] if s['id']==source)['gain']-.5)<.02
key('{RIGHT 50}');results['keyboardTrackVolume']=True
focus('masterGain');key('{HOME}{RIGHT 80}');assert abs(state()['master_gain']-.8)<.02
results['keyboardMasterVolume']=True
focus('privateBreak');key('{SPACE}');assert state()['private_break']
assert IUIA().iuia.GetFocusedElement().CurrentName=='Resume show';key('{SPACE}');assert not state()['private_break']
focus('muteMicrophones');key('{SPACE}');assert state()['microphones_muted']
assert IUIA().iuia.GetFocusedElement().CurrentName=='Unmute mics';key('{SPACE}');assert not state()['microphones_muted']
results['productionToggleAccessibleNames']=True
focus('studioMode');key('{SPACE}');assert state()['mode']=='studio';program=state()['program'];key('^1');assert state()['program']==program
key('^{ENTER}');assert state()['program']==state()['preview'];key('^2');key('^{ENTER}')
results['keyboardPreviewAndTake']=True
focus('studioRecord');key('{SPACE}');time.sleep(1)
dialog=w.child_window(title='Record Program',control_type='Window');dialog.wait('visible')
# Windows common dialog starts in its file-name Edit. Reach it with Tab when necessary.
for _ in range(50):
 f=IUIA().iuia.GetFocusedElement()
 if f.CurrentAutomationId=='1001':break
 key('{TAB}')
else:raise AssertionError('Save filename is not keyboard reachable')
key('^a');recording=root/('nvda-recording-'+str(time.time_ns())+'.mkv');text(str(recording));key('{ENTER}')
until(lambda:state()['recording']['active']);time.sleep(3)
focus('masterGain');recording_quiet_start=log.stat().st_size
time.sleep(float(os.environ.get('STUDIO_NVDA_RECORD_QUIET_SECONDS','4')))
recording_quiet=log.read_bytes()[recording_quiet_start:].decode('utf-8',errors='replace')
results['recordingUnsolicitedSpeechLines']=[l for l in recording_quiet.splitlines() if l.startswith('Speaking ')]
assert not results['recordingUnsolicitedSpeechLines'],results['recordingUnsolicitedSpeechLines']
focus('studioRecord');key('{SPACE}');until(lambda:not state()['recording']['busy'])
assert 'saved' in state()['recording']['status'].lower(),state()['recording']
results['keyboardRecording']=True
# Keep a stable focus while meters change, to detect unwanted automatic chatter.
focus('studioRecord');quiet_start=log.stat().st_size;time.sleep(12)
quiet=log.read_bytes()[quiet_start:].decode('utf-8',errors='replace')
results['unfocusedMeterSpeechLines']=[l for l in quiet.splitlines() if l.startswith('Speaking ')]
results['unfocusedMeterBeeps']=quiet.count('IO - tones.beep')
key('^+l')
ffmpeg=(Path(args.package)/'ffmpeg/bin/ffmpeg.exe').resolve()
r=subprocess.run([str(ffmpeg),'-v','error','-i',str(recording),'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=60)
assert r.returncode==0 and not r.stderr,r.stderr
results['recordingDecoded']=True
speech=log.read_bytes()[start_log:].decode('utf-8',errors='replace')
(root/'nvda-studio-transcript.txt').write_text(speech,encoding='utf-8')
results['missingSpokenLabels']=[label for label in ('Add source','Source type','Name','NVDA media','Mute','Master volume','Studio mode','Record Program','Recording saved','dBFS','Choose an existing local media file','Resume show','Unmute mics') if label.lower() not in speech.lower()]
results['nvdaSpokeWorkflowLabels']=not results['missingSpokenLabels']
(root/'nvda-results.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
assert not results['missingSpokenLabels'] and not results['unfocusedMeterBeeps'] and not results['unfocusedMeterSpeechLines'],results
