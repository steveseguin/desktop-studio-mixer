// Compares Kick comments in the packaged app against Kick's own public chat socket.
// Read-only and isolated: nothing is sent to a chat. DevTools is enabled only for this QA launch.
//   node native-qt/tools/kick-compare.mjs [--channel NAME] [--seconds 300]
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request} from './mcp/studio-client.mjs';

const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const seconds=Number(flag('seconds',300)),port=Number(flag('port',9395));
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/kick',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const control=path.join(out,'control.json');
const flags=`--remote-debugging-port=${port} --disable-background-timer-throttling --disable-backgrounding-occluded-windows --disable-renderer-backgrounding`;
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:flags},windowsHide:true,stdio:'ignore'});
let target;for(let i=0;i<120&&!target;i++){try{target=await discovery(control);}catch{await delay(250);}}
const raw=c=>request(target,'/studio/command',c);
const normal=t=>String(t??'').normalize('NFC').replace(/[​﻿]/g,'').replace(/\s+/gu,' ').trim();
const kickPage=async()=>(await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(p=>/kick\.com/.test(p.url)&&p.type==='page');
async function inPage(page,expression){
  const ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise((r,x)=>{ws.onopen=r;ws.onerror=x;});
  const result=await new Promise((r,x)=>{ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id===1)r(m.result);};ws.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression,awaitPromise:true,returnByValue:true}}));setTimeout(()=>x(Error('page timeout')),20000);});
  ws.close();return result?.result?.value;
}
const until=async(fn,ms=60000)=>{const s=Date.now();while(Date.now()-s<ms){const v=await fn().catch(()=>null);if(v)return v;await delay(1000);}return null;};
let summary={},socket;
try{
  let channel=flag('channel');
  let added=await raw({command:'chat.channel.add',input:`https://kick.com/${channel||'kick'}`});
  if(!added.channel?.id)throw Error(added.error||'Kick channel was not added');
  let page=await until(kickPage);
  if(!page)throw Error('no Kick page opened');
  if(!channel){
    const listing=await until(()=>inPage(page,`fetch('/stream/livestreams/en?page=1&limit=8&sort=desc').then(r=>r.json()).then(j=>JSON.stringify((j.data||[]).map(s=>({slug:s.channel?.slug,viewers:s.viewer_count})))).catch(e=>'')`).then(v=>{const a=v?JSON.parse(v):[];return a.length?a:null;}),30000);
    if(!listing)throw Error('could not list live Kick channels');
    console.log('live channels',JSON.stringify(listing));
    channel=listing.find(s=>s.viewers>=500&&s.viewers<=20000)?.slug||listing[0].slug;
    added=await raw({command:'chat.channel.add',input:`https://kick.com/${channel}`});
    if(!added.channel?.id)throw Error(added.error||'Live Kick channel was not added');
    page=await until(async()=>(await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(p=>p.url.includes(`/${channel}/`)||p.url.endsWith(`/${channel}`)));
  }
  const info=await until(()=>inPage(page,`fetch('/api/v2/channels/${channel}').then(r=>r.json()).then(j=>JSON.stringify({id:j.chatroom?.id,viewers:j.livestream?.viewer_count??null,live:!!j.livestream})).catch(e=>'')`).then(v=>{const o=v?JSON.parse(v):null;return o?.id?o:null;}),30000);
  if(!info)throw Error('could not read the chatroom id');
  if(!info.live)throw Error('Selected Kick channel is not live');
  console.log('channel',channel,JSON.stringify(info));
  const truth=[],ids=new Set(),deleted=new Map(),banned=new Map();let subscribed=false,socketLost=false;
  socket=new WebSocket('wss://ws-us2.pusher.com/app/34bf7a0ff419a2a775b9?protocol=7&client=js&version=8.4.0&flash=false');
  socket.onmessage=e=>{
    const m=JSON.parse(e.data);
    if(m.event==='pusher:connection_established')for(const name of [`chatrooms.${info.id}.v2`,`chatrooms.${info.id}`,`chatroom_${info.id}`])socket.send(JSON.stringify({event:'pusher:subscribe',data:{channel:name,auth:''}}));
    if(m.event==='pusher_internal:subscription_succeeded')subscribed=true;
    if(/ChatMessageEvent/.test(m.event)){const d=JSON.parse(m.data);if(ids.has(d.id))return;ids.add(d.id);truth.push({at:Date.now(),id:d.id,user:d.sender?.username,text:d.content,badges:d.sender?.identity?.badges?.map(b=>b.type)});}
    if(/MessageDeletedEvent/.test(m.event))deleted.set(JSON.parse(m.data).message?.id,Date.now());
    if(/UserBannedEvent/.test(m.event))banned.set(JSON.parse(m.data).user?.username,Date.now());
  };
  socket.onclose=()=>{socketLost=true;};
  socket.onerror=()=>{socketLost=true;};
  if(!await until(async()=>subscribed,15000))throw Error('Kick reference did not subscribe');
  const first=await until(async()=>(await raw({command:'chat.messages'})).messages.some(m=>m.channel===added.channel.id),60000);
  // The panel keeps only its newest comments, so gather them during the run, and score only those that arrived during it.
  const seen=new Map();
  const collect=async()=>{for(const m of (await raw({command:'chat.messages'})).messages)if(m.channel===added.channel.id&&!seen.has(m.id))seen.set(m.id,{...m,seenMs:Date.now()});};
  await collect();
  const started=Date.now();
  while(Date.now()-started<seconds*1000){await delay(3000);await collect();}
  const referenceHeld=!socketLost;
  socket.onclose=null;socket.close();
  const stopped=Date.now();
  const rows=[...seen.values()].filter(r=>r.seenMs>=started);
  await fs.writeFile(path.join(out,'captured.json'),JSON.stringify(rows,null,1));
  await fs.writeFile(path.join(out,'truth.json'),JSON.stringify(truth,null,1));
  const inWindow=truth.filter(t=>t.at>=started&&t.at<=stopped-15000);
  const texts=new Map();const key=(user,text)=>normal(user).toLowerCase()+'|'+normal(text);
  for(const r of rows){const k=key(r.chatname,r.chatmessage);texts.set(k,(texts.get(k)||0)+1);}
  // Studio exposes emote alternative text. Decode only Kick's explicit emote
  // tokens into their names; preserve the surrounding text and punctuation.
  const plain=inWindow.filter(t=>!/\[emote:/.test(t.text||''));
  const expectedText=t=>String(t.text??'').replace(/\[emote:\d+:([^\]]+)\]/g,(_,name)=>name);
  let matched=0,emoteMatched=0,removedByModerators=0;const missed=[],buckets={};
  // Moderators can remove a comment before the page shows it. Those are correct omissions, and are counted separately.
  const soonAfter=(at,t)=>at>=t.at&&at-t.at<=5000;
  const removed=t=>soonAfter(deleted.get(t.id),t)||soonAfter(banned.get(t.user),t);
  for(const t of inWindow){
    const k=key(t.user,expectedText(t)),hit=texts.get(k)>0,b=Math.floor((t.at-started)/30000);
    if(!hit&&removed(t)){removedByModerators++;continue;}
    (buckets[b]??=[0,0])[hit?0:1]++;
    if(hit){matched++;if(/\[emote:/.test(t.text||''))emoteMatched++;texts.set(k,texts.get(k)-1);}else if(missed.length<8)missed.push(t.user+': '+String(t.text).slice(0,120));
  }
  console.log('30s buckets hit/miss',Object.values(buckets).map(b=>b.join('/')).join(' '));
  const status=await raw({command:'chat.status'});
  // A reference with no matches in any 30 seconds means capture stopped, whatever the average says.
  const stalled=Object.values(buckets).filter(([hit,miss])=>miss>=5&&hit===0).length;
  const eligible=inWindow.length-removedByModerators;
  if(!referenceHeld||!eligible||matched!==eligible||stalled)process.exitCode=1;
  summary={referenceHeld,stalled,channel,viewers:info.viewers,live:info.live,seconds,comparison:'author-text-and-emote-names',firstComment:!!first,referenceTotal:inWindow.length,referencePlainText:plain.length,referenceEmoteMessages:inWindow.length-plain.length,emoteMatched,removedByModerators,matched,rate:eligible?matched/eligible:null,panelKick:rows.length,status:status.channels?.map(c=>({name:c.name,status:c.status})),missed};
}catch(e){summary.error=e.message;process.exitCode=1;}
finally{
  if(socket){socket.onclose=null;socket.close();}
  console.log('SUMMARY',JSON.stringify(summary));
  await fs.writeFile(path.join(out,'summary.json'),JSON.stringify(summary,null,1));
  try{await request(target,'/commands',{command:'quit'});}catch{}
  await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
  console.log('dir',out);
}
