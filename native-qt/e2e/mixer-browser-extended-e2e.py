"""Real WebView2 decode/audio and fault containment through the packaged API."""
from pathlib import Path
exec(Path(__file__).with_name('mixer-automation-e2e.py').read_text(encoding='utf-8').split('\ntry:\n    target=')[0])
import sys,base64,collections,urllib.parse
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
os.environ['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS']='--remote-debugging-port=0 --remote-debugging-address=127.0.0.1'
clip=report/'browser-fixture.mp4';root=None
made=run([ffmpeg,'-v','error','-y','-f','lavfi','-i','testsrc2=s=1280x720:r=30','-f','lavfi','-i','sine=frequency=731:sample_rate=48000','-t','8','-c:v','libopenh264','-pix_fmt','yuv420p','-c:a','aac',clip],timeout=45)
assert made.returncode==0,made.stderr
class Fixture(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path.startswith('/video.mp4'):body=clip.read_bytes();mime='video/mp4'
  elif self.path.startswith('/fault'):body=b'<!doctype html><style>body{margin:0;background:red}</style><div style="background:green;color:white">Private fault fixture</div>';mime='text/html'
  else:body=b'<!doctype html><style>body{margin:0;background:red}video{width:50vw;height:50vh;object-fit:fill}</style><video id=v autoplay loop playsinline src=/video.mp4></video>';mime='text/html'
  self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 def log_message(self,*_):pass
def track(id):return next(s for s in state()['sources'] if s['id']==id)
def worker(id):
 for p in root.children():
  try:
   if '--studio-worker' in p.cmdline() and 'browser' in p.cmdline() and any(id in ' '.join(c.cmdline()) for c in p.children(recursive=True)):return p
  except psutil.Error:pass
 raise AssertionError('Browser worker not found')
def connect(pw,id,part):
 end=time.monotonic()+40
 while time.monotonic()<end:
  try:
   port=int((Path(os.environ['LOCALAPPDATA'])/'VideoMixer/browser'/id/'EBWebView/DevToolsActivePort').read_text().splitlines()[0])
   browser=pw.chromium.connect_over_cdp(f'http://127.0.0.1:{port}',timeout=1500)
   page=next(p for c in browser.contexts for p in c.pages if part in p.url);return browser,page
  except Exception:time.sleep(.3)
 raise AssertionError('Could not attach QA browser')
def cpu(p):
 total=0
 for child in [p,*p.children(recursive=True)]:
  try:t=child.cpu_times();total+=t.user+t.system
  except psutil.Error:pass
 return total
try:
 target=cli_call('launch','--headless','--show',str(show));root=psutil.Process(target['pid']);owned.append(root)
 browser_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=browser_server.serve_forever,daemon=True).start()
 url=f'http://127.0.0.1:{browser_server.server_port}'
 video=command('source.add',kind='browser',name='Hardware HTML video',locator=url+'/player')['id']
 until(lambda:track(video)['frames']>20 and track(video)['peak']>.01,'Default browser video and audio',45)
 assert track(video)['transparent'] and not track(video)['muted'];assert image('program','default-overlay.png').getpixel((600,330))==(170,17,34)
 with sync_playwright() as pw:
  browser,page=connect(pw,video,'/player');cdp=page.context.new_cdp_session(page);properties={}
  def prop(event):
   for p in event['properties']:properties[p['name']]=p['value']
  cdp.on('Media.playerPropertiesChanged',prop);cdp.send('Media.enable');page.reload()
  page.wait_for_function('v.videoWidth>0 && v.currentTime>1',timeout=20000);page.wait_for_timeout(2000)
  assert properties.get('kIsPlatformVideoDecoder')=='true',properties
  gpu=browser.new_browser_cdp_session().send('SystemInfo.getInfo')['gpu'];assert gpu['featureStatus']['video_decode']=='enabled'
  results['htmlHardwareDecode']=properties;results['gpuFeatures']=gpu['featureStatus']
  assert page.evaluate('getComputedStyle(document.body).backgroundColor')=='rgba(0, 0, 0, 0)'
  results['defaultAudioAndTransparency']=True
  vdo=command('source.add',kind='browser',name='Live VDO hardware viewer',locator=os.environ['VIDEO_MIXER_E2E_VDO_URL'])['id']
  command('layer.set',source=vdo,x=.5,y=.5,w=.5,h=.5);command('audio.set',source=vdo,gain=.05,monitored=False)
  vb,vp=connect(pw,vdo,'vdo.ninja')
  vp.add_init_script('''(() => {const Real=window.RTCPeerConnection;window.__qaPeers=[];window.RTCPeerConnection=new Proxy(Real,{construct(target,args){const p=new target(...args);window.__qaPeers.push(p);return p;}})})()''')
  vp.reload();vp.wait_for_function('Array.from(document.querySelectorAll("video")).some(v=>v.videoWidth>0 && v.currentTime>2)',timeout=50000)
  results['vdoStats']=vp.evaluate('''async()=>{const out=[];for(const p of window.__qaPeers){const stats=await p.getStats();out.push(...[...stats.values()].filter(s=>['inbound-rtp','codec'].includes(s.type)));}return out}''')
  # Screen-sharing publishers can intentionally send one frame per second.
  assert any(s.get('framesDecoded',0)>0 for s in results['vdoStats'])
  # The default VP8 viewer may use software on this GPU. Request H.264 for a
  # separate positive hardware-decoder check, preserving the default evidence.
  parsed=urllib.parse.urlsplit(os.environ['VIDEO_MIXER_E2E_VDO_URL'])
  query=urllib.parse.parse_qsl(parsed.query,keep_blank_values=True)
  query=[(k,v) for k,v in query if k.lower()!='codec']+[('codec','h264')]
  vp.goto(urllib.parse.urlunsplit(parsed._replace(query=urllib.parse.urlencode(query))))
  vp.wait_for_function('Array.from(document.querySelectorAll("video")).some(v=>v.videoWidth>0 && v.currentTime>3)',timeout=50000)
  trace=vb.new_browser_cdp_session();complete={};trace.on('Tracing.tracingComplete',lambda e:complete.update(e))
  trace.send('Tracing.start',{'categories':'gpu,media,webrtc','transferMode':'ReturnAsStream','streamFormat':'json'});vp.wait_for_timeout(10000);trace.send('Tracing.end')
  end=time.time()+20
  while 'stream' not in complete and time.time()<end:vp.wait_for_timeout(100)
  chunks=[];handle=complete['stream']
  while True:
   part=trace.send('IO.read',{'handle':handle,'size':1048576});chunks.append(base64.b64decode(part['data']) if part.get('base64Encoded') else part['data'].encode())
   if part.get('eof'):break
  trace.send('IO.close',{'handle':handle});names=collections.Counter(e.get('name','') for e in json.loads(b''.join(chunks))['traceEvents'])
  results['vdoD3DDecodeTrace']={k:v for k,v in names.items() if 'D3D' in k and 'Decode' in k};assert results['vdoD3DDecodeTrace']
  recording=report/'browser-faults.mkv';command('record.start',path=str(recording));until(lambda:state()['recording']['active'],'Browser recording')
  time.sleep(3);gpu_process=next(p for p in worker(video).children(recursive=True) if '--type=gpu-process' in p.cmdline());old=gpu_process.pid;gpu_process.kill()
  until(lambda:any(p.pid!=old and '--type=gpu-process' in p.cmdline() for p in worker(video).children(recursive=True)),'GPU process replaced',35)
  before=track(video)['frames'];time.sleep(3);assert track(video)['frames']>before and state()['recording']['active'];results['gpuProcessRecovery']=True
  fault=command('source.add',kind='browser',name='CPU fault source',locator=url+'/fault')['id'];command('layer.set',source=fault,x=.7,y=0,w=.3,h=.2)
  fb,fp=connect(pw,fault,'/fault');victim=worker(fault)
  fp.evaluate('''()=>{const url=URL.createObjectURL(new Blob(['while(true){Math.sqrt(Math.random())}'],{type:'text/javascript'}));window.busy=Array.from({length:24},()=>new Worker(url));}''')
  time.sleep(2);start=time.perf_counter();initial=cpu(victim);latencies=[];before=track(video)['frames']
  for _ in range(60):
   now=time.perf_counter();state();latencies.append((time.perf_counter()-now)*1000);time.sleep(.2)
  usage=(cpu(victim)-initial)/(time.perf_counter()-start)*100;assert usage<440,usage
  assert track(video)['frames']>before+80 and state()['recording']['active']
  results['cpuExhaustion']={'percentOneCore':usage,'apiP95Ms':float(np.percentile(latencies,95)),'apiMaximumMs':max(latencies),'healthyVideoContinued':True}
  assert max(latencies)<1500
  command('source.reconnect',id=fault);time.sleep(3)
  command('record.stop');until(lambda:not state()['recording']['busy'],'Browser recording finalized')
  results['recording']=decode(recording,allow_gaps=True);results['passed']=True
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
 results['originalShowUnchanged']=(hashlib.sha256(user_show.read_bytes()).hexdigest() if user_show.exists() else None)==original_hash
 (report/'browser-extended.json').write_text(json.dumps(results,indent=2));print(json.dumps({k:v for k,v in results.items() if k not in ('htmlHardwareDecode','vdoStats','gpuFeatures')},indent=2))
