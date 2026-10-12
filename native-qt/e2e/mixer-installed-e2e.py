"""Install into a new private QA directory, exercise CLI/MCP/recording, uninstall.

Refuses to replace an existing installation or Start menu shortcuts. The report
must be inside native-qt/e2e/reports; no user shows or recordings are removed.
"""
from pathlib import Path
import os,json,subprocess,time,hashlib,zipfile,winreg,urllib.request,psutil,argparse,re
parser=argparse.ArgumentParser();parser.add_argument('--report',required=True);args=parser.parse_args()
base=Path(args.report).resolve();base.mkdir(parents=True,exist_ok=True)
version=re.search(r'project\(video-mixer-native-qt VERSION ([0-9.]+)',Path('CMakeLists.txt').read_text()).group(1)
dest=base/'app';workspace=Path.cwd().resolve()
assert dest.is_relative_to(workspace/'e2e/reports') and not dest.exists()
try:
 with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\VideoMixer') as k:raise RuntimeError('An existing installation must not be replaced by this test')
except FileNotFoundError:pass
shortcuts=Path(os.environ['APPDATA'])/'Microsoft/Windows/Start Menu/Programs/Desktop Studio';assert not shortcuts.exists()
setup=Path(f'dist/desktop-studio-{version}-setup.exe').resolve();package=Path('dist/desktop-studio-portable').resolve()
def run(argv,**opts):return subprocess.run([str(a) for a in argv],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,**opts)
def wait(fn):
 for _ in range(100):
  if fn():return
  time.sleep(.2)
 raise AssertionError('Timed out')

def installer_registered():
 try:
  with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\VideoMixer'):return True
 except FileNotFoundError:return False
target=None;results={}
try:
 installed=run([setup,'/S','/D='+str(dest)],timeout=120);assert installed.returncode==0,installed.stderr
 assert (dest/'tools/mcp/node_modules/@modelcontextprotocol/sdk/package.json').exists()
 executable_hash=hashlib.sha256((dest/'desktop-studio.exe').read_bytes()).hexdigest()
 assert executable_hash==hashlib.sha256((package/'desktop-studio.exe').read_bytes()).hexdigest()
 with zipfile.ZipFile(f'dist/desktop-studio-{version}-windows-x64.zip') as z:
  assert hashlib.sha256(z.read('desktop-studio-portable/desktop-studio.exe')).hexdigest()==executable_hash
 cli=dest/'tools/mcp/studio-cli.mjs'
 result=run(['node',cli,'launch','--headless','--show',base/'installed.mixer.json'],text=True,encoding='utf-8',timeout=50);assert result.returncode==0,result.stderr;target=json.loads(result.stdout)
 proc=psutil.Process(target['pid'])
 d=json.loads(Path(target['discovery']).read_text())
 def api(route,body=None):
  request=urllib.request.Request(d['base_url']+route,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+d['token'],'Content-Type':'application/json'})
  with urllib.request.urlopen(request,timeout=15) as response:return json.load(response)
 assert api('/studio/state')['headless']
 mcp=run(['node','tools/mcp/studio-packaged-e2e.mjs',dest,target['discovery'],base],text=True,encoding='utf-8',timeout=60);assert mcp.returncode==0,mcp.stderr
 rec=base/'installed.mkv';api('/studio/command',{'command':'record.start','path':str(rec)});wait(lambda:api('/studio/state')['recording']['status'].startswith('Recording:'));time.sleep(3)
 descendants=proc.children(recursive=True)
 api('/commands',{'command':'quit'});proc.wait(timeout=25);target=None
 wait(lambda:all(not p.is_running() for p in descendants))
 decoded=run([package/'ffmpeg/bin/ffmpeg.exe','-v','error','-i',rec,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],timeout=30)
 assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
 results={'installedCliAndMcp':True,'recordingDecoded':True,'noOwnedDescendants':True,'portableZipInstalledExeMatch':True,'executableSha256':executable_hash}
finally:
 if target:
  try:
   d=json.loads(Path(target['discovery']).read_text());request=urllib.request.Request(d['base_url']+'/commands',data=b'{"command":"quit"}',headers={'Authorization':'Bearer '+d['token'],'Content-Type':'application/json'});urllib.request.urlopen(request,timeout=10).close();psutil.Process(target['pid']).wait(timeout=25)
  except Exception:pass
 # Delete only through the owned uninstaller, after checking the resolved target.
 assert dest.resolve().is_relative_to(workspace/'e2e/reports')
 if (dest/'uninstall.exe').exists():
  uninstall=run([dest/'uninstall.exe','/S'],timeout=90);assert uninstall.returncode==0
  # NSIS relaunches a temporary uninstaller. Its parent can return while the
  # final shortcuts and registry entries are still being removed.
  wait(lambda:not (dest/'desktop-studio.exe').exists() and not (dest/'tools/mcp').exists()
       and not shortcuts.exists() and not installer_registered())
  results['uninstallRemovedAutomation']=True
  assert not shortcuts.exists()
  try:
   with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\VideoMixer') as k:raise AssertionError('Installer registration remains')
  except FileNotFoundError:pass
 (base/'installed-results.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
