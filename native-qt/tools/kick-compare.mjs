// Compares Kick comments in the packaged app against Kick's own public chat socket.
// Read-only: nothing is sent to chat. Public reference requests do not require browser debugging.
// --session PATH attaches to an existing isolated QA app and leaves it running.
//   node native-qt/tools/kick-compare.mjs [--channel NAME] [--seconds 300]
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,health,request} from './mcp/studio-client.mjs';

const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const seconds=Number(flag('seconds',300));
if(!Number.isFinite(seconds)||seconds<30)throw Error('Comparison requires at least 30 seconds');
const attached=flag('session')?JSON.parse((await fs.readFile(flag('session'),'utf8')).replace(/^\uFEFF/,'')):null;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=attached?.exe||process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/kick',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const sha=async p=>createHash('sha256').update(await fs.readFile(p)).digest('hex');
await fs.writeFile(path.join(out,'artifact.json'),JSON.stringify({exe,exeSha256:await sha(exe),sourceSha256:await sha(path.join(path.dirname(exe),'ssn/sources/kick.js'))},null,2));
const control=attached?.control||path.join(out,'control.json');
const child=attached?null:spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out},windowsHide:true,stdio:'ignore'});
let target;const startupDeadline=Date.now()+60000;
while(!target&&Date.now()<startupDeadline){try{const candidate=await discovery(control);await health(candidate);target=candidate;}catch{await delay(250);}}
const raw=c=>request(target,'/studio/command',c);
const normal=t=>String(t??'').normalize('NFC').replace(/[​﻿]/g,'').replace(/\s+/gu,' ').trim();
async function publicJson(url){
 const response=await fetch(url,{headers:{Accept:'application/json'},signal:AbortSignal.timeout(15000)});
 if(!response.ok)throw Error(`Public Kick reference HTTP ${response.status}`);
 return response.json();
}
const until=async(fn,ms=60000)=>{const s=Date.now();while(Date.now()-s<ms){const v=await fn().catch(()=>null);if(v)return v;await delay(1000);}return null;};
let summary={},socket;
try{
  let channel=flag('channel');
  if(!channel){
    const listing=(await publicJson('https://kick.com/stream/livestreams/en?page=1&limit=8&sort=desc')).data||[];
    channel=(listing.find(s=>s.viewer_count>=500&&s.viewer_count<=20000)||listing[0])?.channel?.slug;
  }
  if(!/^[a-z0-9_-]{1,50}$/i.test(channel||''))throw Error('Choose a public Kick channel name');
  const existing=(await raw({command:'chat.status'})).channels.find(c=>c.platform==='Kick'&&c.name.toLowerCase()===channel.toLowerCase());
  const added=existing?{channel:existing}:await raw({command:'chat.channel.add',input:`https://kick.com/${channel}`});
  if(!added.channel?.id)throw Error(added.error||'Kick channel was not added');
  const data=await publicJson(`https://kick.com/api/v2/channels/${channel}`);
  const info=data?.chatroom?.id?{id:data.chatroom.id,viewers:data.livestream?.viewer_count??null,live:!!data.livestream}:null;
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
  const initialIds=new Set(seen.keys());
  const started=Date.now();
  while(Date.now()-started<seconds*1000){await delay(3000);await collect();}
  const referenceHeld=!socketLost;
  socket.onclose=null;socket.close();
  const stopped=Date.now();
  const rows=[...seen.values()].filter(r=>!initialIds.has(r.id));
  await fs.writeFile(path.join(out,'window.json'),JSON.stringify({started,stopped,scoredUntil:stopped-15000,deleted:[...deleted],banned:[...banned]},null,2));
  await fs.writeFile(path.join(out,'captured.json'),JSON.stringify(rows,null,1));
  await fs.writeFile(path.join(out,'truth.json'),JSON.stringify(truth,null,1));
  const inWindow=truth.filter(t=>t.at>=started&&t.at<=stopped-15000);
  const texts=new Map();const key=(user,text)=>normal(user).toLowerCase()+'|'+normal(text);
  for(const r of rows){const k=key(r.chatname,r.chatmessage);texts.set(k,(texts.get(k)||0)+1);}
  // Studio exposes emote alternative text. Decode only Kick's explicit emote
  // tokens into their names; preserve the surrounding text and punctuation.
  const plain=inWindow.filter(t=>!/\[emote:/.test(t.text||''));
  const expectedText=t=>String(t.text??'').replace(/\[emote:\d+:([^\]]+)\]/g,(_,name)=>name);
  // Use the full independently observed interval for surplus detection so the
  // unscored warmup/drain messages cannot be mistaken for duplicates. This is
  // an occurrence comparison: separate equal author/text inputs each count.
  const referenceCounts=new Map(),outputCounts=new Map();
  for(const t of truth){const k=key(t.user,expectedText(t));referenceCounts.set(k,(referenceCounts.get(k)||0)+1);}
  for(const r of rows){const k=key(r.chatname,r.chatmessage);outputCounts.set(k,(outputCounts.get(k)||0)+1);}
  let duplicateOccurrences=0,unexpectedOccurrences=0;
  for(const[k,n]of outputCounts){const expected=referenceCounts.get(k)||0;if(!expected)unexpectedOccurrences+=n;else duplicateOccurrences+=Math.max(0,n-expected);}
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
  if(!referenceHeld||!eligible||matched!==eligible||stalled||duplicateOccurrences||unexpectedOccurrences)process.exitCode=1;
  summary={referenceHeld,stalled,channel,viewers:info.viewers,live:info.live,seconds,comparison:'author-text-and-emote-names',firstComment:!!first,referenceTotal:inWindow.length,referencePlainText:plain.length,referenceEmoteMessages:inWindow.length-plain.length,emoteMatched,removedByModerators,matched,missing:eligible-matched,duplicateOccurrences,unexpectedOccurrences,rate:eligible?matched/eligible:null,panelKick:rows.length,status:status.channels?.map(c=>({name:c.name,status:c.status})),missed};
}catch(e){summary.error=e.message;process.exitCode=1;}
finally{
  if(socket){socket.onclose=null;socket.close();}
  console.log('SUMMARY',JSON.stringify(summary));
  await fs.writeFile(path.join(out,'summary.json'),JSON.stringify(summary,null,1));
  if(child){
    try{await request(target,'/commands',{command:'quit'});}catch{}
    if(child.exitCode===null)await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
  }
  console.log('dir',out);
}
