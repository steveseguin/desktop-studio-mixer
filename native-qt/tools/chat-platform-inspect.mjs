// Observe a real public capture page in an isolated packaged app. No chat posts.
// This is diagnostic evidence; zero messages never qualify platform support.
import fs from 'node:fs/promises';
import path from 'node:path';
import net from 'node:net';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,health,request,screenshot} from './mcp/studio-client.mjs';
const input=process.argv[2],seconds=Number(process.argv[3]||90),pulseScroll=process.argv.includes('--pulse-scroll'),traceDom=process.argv.includes('--trace-dom');
if(!input||!Number.isFinite(seconds)||seconds<10||seconds>600)throw Error('Usage: node chat-platform-inspect.mjs URL [10..600 seconds]');
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/platform-inspect',new Date().toISOString().replaceAll(/[:.]/g,'-')+'-'+new URL(input).hostname);
await fs.mkdir(out,{recursive:true});
const server=net.createServer();await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;await new Promise(r=>server.close(r));
const control=path.join(out,'control.json');
const result={input,exe,sha256:createHash('sha256').update(await fs.readFile(exe)).digest('hex'),seconds,pulseScroll,traceDom,samples:[],pages:[]};
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:`--remote-debugging-port=${port}`},windowsHide:true,stdio:'ignore'});
let target;const seen=new Map();
async function inspect(page,index){
 const ws=new WebSocket(page.webSocketDebuggerUrl);
 try{
  await Promise.race([new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;}),delay(5000).then(()=>{throw Error('Inspector connection timeout');})]);
  let seq=0;const pending=new Map();
  ws.onmessage=e=>{const m=JSON.parse(e.data),p=pending.get(m.id);if(p){clearTimeout(p.timer);pending.delete(m.id);m.error?p.reject(Error(m.error.message)):p.resolve(m.result);}};
  const call=(method,params)=>new Promise((resolve,reject)=>{const id=++seq,timer=setTimeout(()=>{pending.delete(id);reject(Error('Inspector timeout'));},8000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));});
  const dom=await call('Runtime.evaluate',{expression:`JSON.stringify({at:Date.now(),url:location.href,title:document.title,text:document.body?.innerText.slice(0,20000),chatRows:[...document.querySelectorAll('[data-e2e="chat-message"]')].slice(-12).map(e=>({html:e.outerHTML.slice(0,18000),parent:e.parentElement?.outerHTML.slice(0,2000)})),frames:[...document.querySelectorAll('iframe')].map(f=>f.src),links:[...document.querySelectorAll('a[href]')].filter(a=>/live|chat/i.test(a.textContent+' '+a.href)).slice(0,40).map(a=>({text:a.textContent.slice(0,120),url:a.href}))})`,returnByValue:true});
  result.pages.push(JSON.parse(dom.result.value));
  if(pulseScroll&&index==='initial')await call('Runtime.evaluate',{expression:`window.__studioScrollProbe=setInterval(()=>{for(const e of document.querySelectorAll('div'))if(e.scrollHeight>e.clientHeight+20&&e.clientHeight>50&&e.querySelector('[data-e2e="chat-message"]')){e.scrollTop=e.scrollHeight;e.dispatchEvent(new Event('scroll'));}},100)`});
  const shot=await call('Page.captureScreenshot',{format:'png'});await fs.writeFile(path.join(out,`capture-${index}.png`),Buffer.from(shot.data,'base64'));
 }finally{ws.close();}
}
try{
 const startupDeadline=Date.now()+60000;
 while(!target&&Date.now()<startupDeadline){try{const candidate=await discovery(control);await health(candidate);target=candidate;}catch{await delay(250);}}
 if(!target)throw Error('App did not start');
 const cmd=(command,args={})=>request(target,'/studio/command',{command,...args});
 const added=await cmd('chat.channel.add',{input});if(added.error)throw Error(added.error);result.channel=added.channel;
 const stop=Date.now()+seconds*1000;let initialInspected=false,nextTrace=Date.now()+30000;
 while(Date.now()<stop){
  const s=await cmd('chat.status');result.samples.push({at:Date.now(),ready:s.ready,problem:s.problem,channel:s.channels?.find(c=>c.id===added.channel.id),resources:s.resources});
  for(const m of (await cmd('chat.messages')).messages)if(m.channel===added.channel.id&&!seen.has(m.id))seen.set(m.id,{...m,seenMs:Date.now()});
  if(!initialInspected&&Date.now()>stop-(seconds-15)*1000){
   initialInspected=true;
   const pages=await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
   for(const page of pages.filter(p=>p.type==='page'&&new URL(p.url).hostname.replace(/^www\./,'')===new URL(input).hostname.replace(/^www\./,'')))await inspect(page,'initial').catch(e=>result.pages.push({url:page.url,error:e.message}));
  }
  if(traceDom&&Date.now()>=nextTrace){
   nextTrace=Date.now()+30000;
   const pages=await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
   for(const page of pages.filter(p=>p.type==='page'&&new URL(p.url).hostname.replace(/^www\./,'')===new URL(input).hostname.replace(/^www\./,'')))await inspect(page,'trace-'+result.samples.length).catch(e=>result.pages.push({url:page.url,error:e.message}));
  }
  await delay(1000);
 }
 const pages=await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();let index=0;
 for(const page of pages.filter(p=>p.type==='page'&&new URL(p.url).hostname.replace(/^www\./,'')===new URL(input).hostname.replace(/^www\./,'')))await inspect(page,index++).catch(e=>result.pages.push({url:page.url,error:e.message}));
 await screenshot(target,{target:'window'},path.join(out,'studio.png'));
 // Captured items can include system notices, likes and gifts. Manual comparison
 // against the visible page is required before calling any item a viewer comment.
 result.captured_items=seen.size;result.outcome=seen.size?'items_observed_requires_review':'no_items_observed';
 if(seen.size){
  await cmd('chat.show',{id:[...seen.values()].at(-1).id});
  for(let i=0;i<60;i++){const s=await cmd('chat.status');if(s.on_screen){result.composed=true;break;}await delay(100);}
  if(!result.composed)throw Error('Observed comment did not reach Program');
  await screenshot(target,{target:'program'},path.join(out,'comment-program.png'));
 }
}catch(e){result.error=e.stack;process.exitCode=1;}
finally{
 if(target)try{await request(target,'/commands',{command:'quit'});}catch{}
 for(let i=0;i<80&&child.exitCode===null;i++)await delay(100);
 if(child.exitCode===null)child.kill();
 await fs.writeFile(path.join(out,'captured.json'),JSON.stringify([...seen.values()],null,2));
 await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));
 console.log(JSON.stringify({out,outcome:result.outcome,captured_items:result.captured_items,composed:result.composed,error:result.error,states:[...new Set(result.samples.map(s=>s.channel?.status))]}));
}
