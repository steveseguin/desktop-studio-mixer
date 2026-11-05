// Tracks JS heap, DOM and listener counts per chat page in the packaged app, to locate memory
// growth. DevTools is enabled only for this QA launch. Read-only and isolated.
//   node native-qt/tools/chat-page-metrics.mjs --twitch xqc [--youtube ID] [--minutes 6]
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,health,request} from './mcp/studio-client.mjs';

const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const twitch=flag('twitch'),youtube=flag('youtube'),kick=flag('kick'),minutes=Number(flag('minutes',6)),port=Number(flag('port',9377));
if(!Number.isFinite(minutes)||minutes<1||minutes>240||(!twitch&&!youtube&&!kick))throw Error('Specify channels and --minutes between 1 and 240');
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/page-metrics',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
await fs.writeFile(path.join(out,'artifact.json'),JSON.stringify({exe,sha256:createHash('sha256').update(await fs.readFile(exe)).digest('hex'),twitch,youtube,kick,minutes},null,2));
const control=path.join(out,'control.json');
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:`--remote-debugging-port=${port}`},windowsHide:true,stdio:'ignore'});
let target;
const history=[];
try{
const startupDeadline=Date.now()+60000;
while(!target&&Date.now()<startupDeadline){try{const candidate=await discovery(control);await health(candidate);target=candidate;}catch{await delay(250);}}
if(!target)throw Error('App control did not become ready');
const raw=c=>request(target,'/studio/command',c);
if(twitch)await raw({command:'chat.channel.add',input:`https://www.twitch.tv/${twitch}`});
if(youtube)await raw({command:'chat.channel.add',input:`https://www.youtube.com/watch?v=${youtube}`});
if(kick)await raw({command:'chat.channel.add',input:`https://kick.com/${kick}`});
async function metrics(page){
  const ws=new WebSocket(page.webSocketDebuggerUrl);let openTimer;const pending=new Map();
  try{
  await new Promise((r,x)=>{ws.onopen=r;ws.onerror=x;openTimer=setTimeout(()=>x(Error('CDP connection timeout')),4000);});clearTimeout(openTimer);
  let id=0;ws.onmessage=e=>{const m=JSON.parse(e.data),p=pending.get(m.id);if(p){clearTimeout(p.timer);pending.delete(m.id);m.error?p.reject(Error(m.error.message)):p.resolve(m.result);}};
  const call=(method,params={})=>new Promise((resolve,reject)=>{const i=++id,timer=setTimeout(()=>{pending.delete(i);reject(Error('timeout '+method));},6000);pending.set(i,{resolve,reject,timer});ws.send(JSON.stringify({id:i,method,params}));});
  await call('Performance.enable');
  const list=(await call('Performance.getMetrics')).metrics,get=n=>list.find(m=>m.name===n)?.value??0;
  const probe=await call('Runtime.evaluate',{expression:`JSON.stringify({rows:document.querySelectorAll('.chat-line__message,yt-live-chat-text-message-renderer,[data-chat-entry]').length,all:document.querySelectorAll('*').length,imgs:document.images.length})`,returnByValue:true}).catch(()=>null);
  return {heapMiB:Math.round(get('JSHeapUsedSize')/1048576),heapTotalMiB:Math.round(get('JSHeapTotalSize')/1048576),nodes:get('Nodes'),listeners:get('JSEventListeners'),docs:get('Documents'),dom:probe?.result?.value?JSON.parse(probe.result.value):null};
  }finally{clearTimeout(openTimer);for(const p of pending.values())clearTimeout(p.timer);ws.close();}
}
const start=Date.now();
while(Date.now()-start<minutes*60000){
  await delay(30000);
  const s=await raw({command:'chat.status'}).catch(()=>({}));
  const pages=await (await fetch(`http://127.0.0.1:${port}/json`)).json().catch(()=>[]);
  const row={t:Math.round((Date.now()-start)/1000),privateMiB:Math.round(s.resources?.private_mib||0),count:s.count,pages:{}};
  for(const p of pages.filter(p=>p.type==='page'||p.type==='iframe')){if(p.url.startsWith('devtools'))continue;let key=p.url.replace(/^https?:\/\//,'').slice(0,46);try{row.pages[key]=await metrics(p);}catch(e){row.pages[key]={error:e.message};}}
  history.push(row);
  await fs.writeFile(path.join(out,'metrics.json'),JSON.stringify(history,null,1));
  console.log(row.t,'private',row.privateMiB,'count',row.count,Object.entries(row.pages).map(([k,v])=>`\n   ${k} heap=${v.heapMiB}/${v.heapTotalMiB} nodes=${v.nodes} listeners=${v.listeners} rows=${v.dom?.rows} imgs=${v.dom?.imgs}`).join(''));
}
}catch(e){process.exitCode=1;await fs.writeFile(path.join(out,'failure.json'),JSON.stringify({error:e.stack},null,2));console.error(e.stack);}
finally{
await fs.writeFile(path.join(out,'metrics.json'),JSON.stringify(history,null,1));
try{await request(target,'/commands',{command:'quit'});}catch{}
await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
console.log('dir',out);
}
