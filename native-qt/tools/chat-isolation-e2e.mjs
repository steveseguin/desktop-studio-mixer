// Adversarial workflows against the real portable app, in an isolated profile.
// CDP is enabled only for this QA launch. No public chat messages are sent.
import fs from 'node:fs/promises';
import path from 'node:path';
import net from 'node:net';
import {fileURLToPath} from 'node:url';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request,screenshot} from './mcp/studio-client.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const out=path.join(root,'native-qt/build-chat/isolation-review',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const reserve=net.createServer();await new Promise(r=>reserve.listen(0,'127.0.0.1',r));const port=reserve.address().port;await new Promise(r=>reserve.close(r));
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const control=path.join(out,'control.json');
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:`--remote-debugging-port=${port}`},windowsHide:true,stdio:'ignore'});
const result={out,checks:[],metrics:{}};let target,ws;
const check=(ok,name,detail)=>{result.checks.push({ok:!!ok,name,detail});console.log((ok?'PASS ':'FAIL ')+name+' '+JSON.stringify(detail??''));};
const until=async(fn,label,ms=30000)=>{const end=Date.now()+ms;while(Date.now()<end){const v=await fn();if(v)return v;await delay(100);}throw Error('Timed out '+label);};
const c=async(command,args={})=>{const r=await request(target,'/studio/command',{command,...args});if(r.error)throw Error(r.error);return r;};
async function connect(page){ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise((r,j)=>{ws.addEventListener('open',r,{once:true});ws.addEventListener('error',j,{once:true});});let seq=0;const pending=new Map();
  ws.addEventListener('message',e=>{const m=JSON.parse(e.data);const p=pending.get(m.id);if(p){clearTimeout(p.timer);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}});
  return async expression=>new Promise((resolve,reject)=>{const id=++seq;const timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timeout'));},8000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method:'Runtime.evaluate',params:{expression,awaitPromise:true,returnByValue:true}}));});}
try {
  target=await until(async()=>{try{return await discovery(control);}catch{return false;}},'app');
  await c('chat.channel.add',{input:'https://www.twitch.tv/xqc'});
  const page=await until(async()=>{try{return (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(p=>p.type==='page'&&p.url.includes('twitch.tv/popout/'));}catch{return false;}},'owned capture inspector',60000);
  const evaluate=await connect(page);await until(async()=>(await c('chat.status')).ready,'engine ready');await delay(5000);
  const token='QA'+Date.now();
  // A sandboxed advertisement may call parent.postMessage but must not acquire
  // the enclosing source's channel identity at Studio's native boundary.
  const forged={kind:'rpc',id:token,frame:token,method:'runtime.sendMessage',url:page.url,args:[{message:{chatname:'Impersonated viewer',chatmessage:token+'-forged',textonly:true}}]};
  const attack=`parent.postMessage(${JSON.stringify({engineSpikeUp:forged})},'*')`;
  await evaluate(`(()=>{const f=document.createElement('iframe');f.sandbox='allow-scripts';f.srcdoc=${JSON.stringify('<script>'+attack+'</script>')};document.body.append(f);return true})()`);
  await delay(2500);check(!(await c('chat.messages')).messages.some(m=>m.chatmessage===token+'-forged'),'Opaque ad frame cannot impersonate a channel');
  const beforeOversize=(await c('chat.status')).oversized_messages||0;
  for(const message of [
    {chatname:'Oversize QA',chatmessage:'x'.repeat(8001),textonly:true},
    {chatname:'Oversize QA',chatmessage:'short parent',reply:'x'.repeat(8001),textonly:true}
  ])await evaluate(`chrome.runtime.sendMessage({message:${JSON.stringify(message)}}).catch(e=>e.message)`);
  // Exercise the native envelope limit, beyond the shim's field filtering.
  await evaluate(`chrome.webview.postMessage({kind:'rpc',id:'qa-oversize',frame:'qa-oversize',method:'runtime.sendMessage',args:[{message:{chatname:'Oversize QA',chatmessage:'x'.repeat(33000),textonly:true}}]});true`);
  await until(async()=>(await c('chat.status')).oversized_messages>=beforeOversize+3,'oversize diagnostics');
  check(!(await c('chat.messages')).messages.some(m=>m.chatname==='Oversize QA'),'Oversized payloads are counted and rejected without truncated comments');
  const badged={chatname:'Badge QA',chatmessage:token+'-large-badge',textonly:true,chatbadges:'data:image/png;base64,'+'A'.repeat(40000)};
  await evaluate(`chrome.runtime.sendMessage({message:${JSON.stringify(badged)}})`);
  check(!!await until(async()=>(await c('chat.messages')).messages.find(m=>m.chatmessage===badged.chatmessage),'large badge comment'),'Large unused badges do not drop otherwise valid comments');
  // Exercise the actual capture -> SSN -> host -> native plain-text conversion.
  const rich={chatname:'Markup Viewer',chatmessage:`${token} before <img alt="Kappa" src="https://invalid.example/emote.png"> after`,textonly:false};
  await evaluate(`chrome.runtime.sendMessage({message:${JSON.stringify(rich)}})`);
  const row=await until(async()=>(await c('chat.messages')).messages.find(m=>m.chatname==='Markup Viewer'),'rich capture delivered');
  check(row.chatmessage===token+' before Kappa after','Emote alternative text survives real engine delivery',{actual:row.chatmessage});
  await c('chat.show',{id:row.id});await until(async()=>(await c('chat.status')).on_screen,'show');await c('chat.keep');
  await screenshot(target,{target:'window'},path.join(out,'01-capture.png'));
  const companion={chatname:'Markup Viewer',chatmessage:token+'-keep-this',textonly:true};
  await evaluate(`chrome.runtime.sendMessage({message:${JSON.stringify(companion)}})`);
  await until(async()=>(await c('chat.messages')).messages.some(m=>m.chatmessage===companion.chatmessage),'second occurrence');
  await evaluate(`chrome.runtime.sendMessage({delete:${JSON.stringify({id:row.engineId,chatname:row.chatname,type:'twitch'})}})`);
  await delay(1500);
  const afterDelete=(await c('chat.messages')).messages;
  check(!afterDelete.some(m=>m.id===row.id)&&!(await c('chat.status')).on_screen,'Source deletion removes the displayed comment');
  check(afterDelete.some(m=>m.chatmessage===companion.chatmessage),'Deleting one comment preserves other comments from its author');
  if(process.argv.includes('--stress')) {const next=afterDelete.find(m=>m.chatmessage===companion.chatmessage);await c('chat.show',{id:next.id});await until(async()=>(await c('chat.status')).on_screen,'show for stress');await c('chat.keep');}
  if(process.argv.includes('--stress')) {
    const clip=path.join(out,'memory-budget.mkv');await c('record.start',{path:clip});await delay(2500);
    const before=await c('chat.status'),samples=[];
    // Touch each page of each allocation so committed usage is observable.
    await evaluate(`window.__qaMemory=[];window.__qaAllocator=setInterval(()=>{const b=new Uint8Array(32*1024*1024);for(let i=0;i<b.length;i+=4096)b[i]=123;window.__qaMemory.push(b)},150);true`);
    const deadline=Date.now()+20000;let recovered=false,limited=false;
    while(Date.now()<deadline){const s=await c('chat.status');samples.push({at:Date.now(),ready:s.ready,pid:s.browser_pid,problem:s.problem,resources:s.resources});limited ||= !!s.resources?.limit_exceeded||/too many resources/.test(s.problem||'');if(s.ready&&s.browser_pid&&s.browser_pid!==before.browser_pid){recovered=true;break;}await delay(200);}
    // The old inspector can only touch the QA app; release if no budget fired.
    if(!recovered)await evaluate('clearInterval(window.__qaAllocator);window.__qaMemory=[];true').catch(()=>{});
    result.metrics.resourceSamples=samples;
    check(limited,'Memory budget reports a resource recovery');check(recovered,'Resource overrun restarts the chat browser');
    check((await request(target,'/studio/state')).recording.active,'Memory overrun leaves recording running');
    await delay(2500);await c('record.stop');await until(async()=>!(await request(target,'/studio/state')).recording.busy,'record finalize');
    const ff=path.join(path.dirname(exe),'ffmpeg/bin/ffmpeg.exe');const decoded=await promisify(execFile)(ff,['-v','error','-i',clip,'-f','null','-'],{windowsHide:true,timeout:60000});check(!decoded.stderr.trim(),'Recording decodes after actual memory exhaustion');
    await screenshot(target,{target:'program'},path.join(out,'02-after-memory-recovery.png'));
  }
  result.completed=true;
}catch(e){result.error=e.stack;console.error(e.stack);process.exitCode=1;
  try{result.lastState=await c('chat.status');result.pages=[];const pages=await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    for(const p of pages.filter(p=>p.type==='page'&&!p.url.includes('twitch.tv'))){ws?.close();const evaluate=await connect(p);result.pages.push({url:p.url,inspection:await evaluate(`JSON.stringify({url:location.href,output:document.querySelector('#output')?.outerHTML,opacity:document.querySelector('#output')&&getComputedStyle(document.querySelector('#output')).opacity,card:document.documentElement.dataset.studioCard,iframes:[...document.querySelectorAll('iframe')].map(f=>f.src),feed:window.feed,peers:window.peers})`)});}
  }catch(error){result.inspectionError=error.message;}
}
finally {ws?.close();try{await request(target,'/commands',{command:'quit'});}catch{}await until(()=>child.exitCode!==null,'exit',10000).catch(()=>child.kill());if(result.checks.some(x=>!x.ok))process.exitCode=1;await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log(out);}
