// Compares page heap growth between chat-spike configurations on the same public chat.
//   node heap-ab.mjs --video ID [--minutes 3.5] [--ssn-root DIR]
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';

const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const video=flag('video'),minutes=Number(flag('minutes',3.5)),ssn=flag('ssn-root','C:/Users/Steve/code/social_stream');
const here=path.dirname(fileURLToPath(import.meta.url));
const base=path.join(here,'runs','heap-ab-'+Date.now());
await fs.mkdir(base,{recursive:true});
const flags='--disable-background-timer-throttling --disable-backgrounding-occluded-windows --disable-renderer-backgrounding';
const configs=[
  {name:'plain-url',url:`https://www.youtube.com/live_chat?v=${video}`,port:9381,block:false},
  {name:'popout-url',url:`https://www.youtube.com/live_chat?is_popout=1&v=${video}`,port:9382,block:false},
  {name:'popout-images-blocked',url:`https://www.youtube.com/live_chat?is_popout=1&v=${video}`,port:9383,block:true},
];
const procs=configs.map(c=>spawn(path.join(here,'build','chat-spike.exe'),['--ssn-root',ssn,'--source',`url:${c.url}`,'--mode','hidden','--out',path.join(base,c.name),'--duration-min',String(minutes+0.3),`--browser-args=${flags} --remote-debugging-port=${c.port}`],{stdio:'ignore',windowsHide:true}));
async function connect(port){
  const pages=await (await fetch(`http://127.0.0.1:${port}/json`)).json();
  const page=pages.find(p=>p.url.includes('live_chat'));
  const ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise((r,x)=>{ws.onopen=r;ws.onerror=x;});
  let id=0;const pending=new Map();ws.onmessage=e=>{const m=JSON.parse(e.data);if(pending.has(m.id)){pending.get(m.id)(m.result);pending.delete(m.id);}};
  return {call:(method,params={})=>new Promise(r=>{const i=++id;pending.set(i,r);ws.send(JSON.stringify({id:i,method,params}));}),close:()=>ws.close()};
}
await delay(12000);
const links=[];
for(const c of configs){
  const link=await connect(c.port);
  await link.call('Performance.enable');
  // Same effect as Studio's filter: images, media and fonts never load. Set before a reload.
  if(c.block){await link.call('Network.enable');await link.call('Network.setBlockedURLs',{urls:['*.jpg','*.jpeg','*.png','*.webp','*.gif','*.woff','*.woff2','*.mp4','*ggpht.com*','*ytimg.com*','*googleusercontent.com*']});await link.call('Page.reload');}
  links.push({c,link});
}
const start=Date.now();const rows=[];
while(Date.now()-start<minutes*60000){
  await delay(30000);
  const row={t:Math.round((Date.now()-start)/1000)};
  for(const {c,link} of links){const m=(await link.call('Performance.getMetrics')).metrics,get=n=>m.find(x=>x.name===n)?.value??0;row[c.name]={heap:Math.round(get('JSHeapUsedSize')/1048576),total:Math.round(get('JSHeapTotalSize')/1048576),nodes:get('Nodes'),listeners:get('JSEventListeners')};}
  rows.push(row);console.log(JSON.stringify(row));
}
for(const {link} of links)link.close();
await Promise.all(procs.map(p=>new Promise(r=>{p.once('exit',r);setTimeout(r,40000);})));
for(const c of configs){const f=path.join(base,c.name,`messages-url.jsonl`);const files=await fs.readdir(path.join(base,c.name));const m=files.find(x=>x.startsWith('messages-'));const text=m?await fs.readFile(path.join(base,c.name,m),'utf8'):'';console.log(c.name,'captured',text.split('\n').filter(l=>l.includes('"message"')).length);}
await fs.writeFile(path.join(base,'heap.json'),JSON.stringify(rows,null,1));console.log('dir',base);
