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
const pollMs=Number(flag('poll-ms',500));
if(!Number.isFinite(pollMs)||pollMs<200||pollMs>3000)throw Error('Poll interval must be 200–3000 ms');
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
let summary={},socket,heartbeat;
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
  const truth=[],ids=new Set(),deleted=new Map(),banned=new Map(),moderationEvents=[];let subscribed=false,socketLost=false;
  socket=new WebSocket('wss://ws-us2.pusher.com/app/34bf7a0ff419a2a775b9?protocol=7&client=js&version=8.4.0&flash=false');
  const topic=`chatrooms.${info.id}.v2`;
  socket.onmessage=e=>{try{
    const m=JSON.parse(e.data);
    if(m.event==='pusher:connection_established')socket.send(JSON.stringify({event:'pusher:subscribe',data:{channel:topic,auth:''}}));
    if(m.event==='pusher:ping')socket.send(JSON.stringify({event:'pusher:pong',data:{}}));
    if(m.event==='pusher:error')socketLost=true;
    if(m.event==='pusher_internal:subscription_succeeded'&&m.channel===topic)subscribed=true;
    if(m.channel!==topic)return;
    if(/ChatMessageEvent/.test(m.event)){const d=JSON.parse(m.data);if(ids.has(d.id))return;ids.add(d.id);truth.push({at:Date.now(),id:d.id,user:d.sender?.username,text:d.content,type:d.type,metadata:d.metadata,badges:d.sender?.identity?.badges?.map(b=>b.type)});}
    if(/MessageDeletedEvent/.test(m.event)){const id=JSON.parse(m.data).message?.id,at=Date.now();deleted.set(id,at);moderationEvents.push({kind:'delete',id,at});}
    if(/UserBannedEvent/.test(m.event)){const user=JSON.parse(m.data).user?.username,at=Date.now();banned.set(user,at);moderationEvents.push({kind:'ban',user,at});}
  }catch{socketLost=true;}};
  socket.onclose=()=>{socketLost=true;};
  socket.onerror=()=>{socketLost=true;};
  heartbeat=setInterval(()=>{if(socket.readyState===WebSocket.OPEN)socket.send(JSON.stringify({event:'pusher:ping',data:{}}));},25000);
  if(!await until(async()=>subscribed,15000))throw Error('Kick reference did not subscribe');
  const first=await until(async()=>(await raw({command:'chat.messages'})).messages.some(m=>m.channel===added.channel.id),60000);
  // The panel keeps only its newest comments, so gather them during the run, and score only those that arrived during it.
  const seen=new Map();
  let lastDiagnostic=0;
  const collect=async()=>{
    const feed=await raw({command:'chat.messages'});
    for(const m of feed.messages)if(m.channel===added.channel.id&&!seen.has(m.id))seen.set(m.id,{...m,seenMs:Date.now()});
    if(Date.now()-lastDiagnostic>=2000){
      lastDiagnostic=Date.now();const state=await raw({command:'chat.status'});
      const diagnostic={at:lastDiagnostic,bufferRows:feed.messages.length,pending:feed.pending,selected:!!state.selected,
        ready:state.ready,browserPid:state.browser_pid,oversized:state.oversized_messages,resources:state.resources,
        channels:state.channels?.map(c=>({id:c.id,platform:c.platform,status:c.status})),
        pageResources:state.page_resources?.pages?.map(p=>({platform:p.platform,role:p.role,stats:p.stats}))};
      await fs.appendFile(path.join(out,'diagnostics.jsonl'),JSON.stringify(diagnostic)+'\n');
    }
  };
  await collect();
  const initialIds=new Set(seen.keys());
  const started=Date.now();
  while(Date.now()-started<seconds*1000){await delay(pollMs);await collect();}
  const referenceHeld=!socketLost;
  socket.onclose=null;socket.close();
  const stopped=Date.now();
  const rows=[...seen.values()].filter(r=>!initialIds.has(r.id));
  await fs.writeFile(path.join(out,'window.json'),JSON.stringify({started,stopped,scoredFrom:started+5000,scoredUntil:stopped-15000,deleted:[...deleted],banned:[...banned],moderationEvents},null,2));
  await fs.writeFile(path.join(out,'captured.json'),JSON.stringify(rows,null,1));
  await fs.writeFile(path.join(out,'truth.json'),JSON.stringify(truth,null,1));
  // Gifts and reward notices are separate platform events. Keep them in the
  // ledger, but score ordinary chat against ChatMessageEvent. A reward-labelled
  // row with a native chat ID still belongs in the ordinary message comparison.
  const chatRows=rows.filter(r=>r.event!=='gift'&&!(r.event==='reward'&&!r.meta?.messageId));
  const nativeIdsCompared=chatRows.length>0&&chatRows.every(r=>/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(r.meta?.messageId||''));
  const giftEventsObserved=rows.filter(r=>r.event==='gift').length;
  const inWindow=truth.filter(t=>t.at>=started+5000&&t.at<=stopped-15000);
  const key=(user,text)=>JSON.stringify([normal(user).toLowerCase(),normal(text)]);
  // Studio exposes emote alternative text. Decode only Kick's explicit emote
  // tokens into their names; preserve the surrounding text and punctuation.
  const plain=inWindow.filter(t=>!/\[emote:/.test(t.text||''));
  const expectedText=t=>String(t.text??'').replace(/\[emote:\d+:([^\]]+)\]/g,(_,name)=>name);
  // Match each occurrence once, within five seconds of its independent receipt.
  // Equal text later in the run must not conceal an earlier dropped message.
  // Include warmup/drain inputs in matching, but score only the central window.
  const referenceKeys=new Set(),available=new Map(),matchedIds=new Set(),matchedRows=new Set();
  for(const r of chatRows){const k=key(r.chatname,r.chatmessage);if(!available.has(k))available.set(k,[]);available.get(k).push(r);}
  const delays=[];let lateOccurrences=0;
  for(const tolerance of [5000,15000])for(const t of truth){
    const k=key(t.user,expectedText(t));referenceKeys.add(k);if(matchedIds.has(t.id))continue;
    const candidates=available.get(k)||[];
    const index=candidates.findIndex(r=>(!nativeIdsCompared||r.meta.messageId.toLowerCase()===t.id.toLowerCase())&&Math.abs(r.seenMs-t.at)<=tolerance);
    if(index>=0){const[r]=candidates.splice(index,1);matchedIds.add(t.id);matchedRows.add(r.id);if(t.at>=started+5000&&t.at<=stopped-15000){delays.push(r.seenMs-t.at);if(tolerance>5000)lateOccurrences++;}}
  }
  // An unmatched copy of known text may be a duplicate or delivery delayed by
  // more than the observation allowance. Do not mislabel late input as a duplicate.
  let unmatchedKnownOccurrences=0,unexpectedOccurrences=0;
  for(const r of chatRows){
    if(matchedRows.has(r.id)||r.seenMs<started+5000||r.seenMs>stopped-15000)continue;
    if(referenceKeys.has(key(r.chatname,r.chatmessage)))unmatchedKnownOccurrences++;else unexpectedOccurrences++;
  }
  let matched=0,emoteMatched=0,removedByModerators=0;const missed=[],buckets={};
  // Moderators can remove a comment before the page shows it. Those are correct omissions, and are counted separately.
  const soonAfter=(at,t)=>at>=t.at&&at-t.at<=5000;
  const removed=t=>moderationEvents.some(e=>soonAfter(e.at,t)&&(e.kind==='delete'?e.id===t.id:e.user===t.user));
  for(const t of inWindow){
    const hit=matchedIds.has(t.id),b=Math.floor((t.at-started)/30000);
    if(!hit&&removed(t)){removedByModerators++;continue;}
    (buckets[b]??=[0,0])[hit?0:1]++;
    if(hit){matched++;if(/\[emote:/.test(t.text||''))emoteMatched++;}else if(missed.length<8)missed.push(t.user+': '+String(t.text).slice(0,120));
  }
  console.log('30s buckets hit/miss',Object.values(buckets).map(b=>b.join('/')).join(' '));
  const status=await raw({command:'chat.status'});
  // A reference with no matches in any 30 seconds means capture stopped, whatever the average says.
  const stalled=Object.values(buckets).filter(([hit,miss])=>miss>=5&&hit===0).length;
  const eligible=inWindow.length-removedByModerators;
  if(!referenceHeld||!eligible||matched!==eligible||lateOccurrences||stalled||unmatchedKnownOccurrences||unexpectedOccurrences)process.exitCode=1;
  summary={referenceHeld,stalled,channel,viewers:info.viewers,live:info.live,seconds,pollMs,scoredSeconds:(stopped-started-20000)/1000,nativeIdsCompared,comparison:(nativeIdsCompared?'native-id-author-text-and-emotes':'author-text-and-emote-occurrences')+'; 5s target, 15s late allowance',maxAbsReceiptDifferenceMs:delays.length?delays.reduce((max,value)=>Math.max(max,Math.abs(value)),0):null,firstComment:!!first,referenceTotal:inWindow.length,referencePlainText:plain.length,referenceEmoteMessages:inWindow.length-plain.length,emoteMatched,removedByModerators,matched,lateOccurrences,missing:eligible-matched,unmatchedKnownOccurrences,unexpectedOccurrences,giftEventsObserved,rate:eligible?matched/eligible:null,panelKick:rows.length,status:status.channels?.map(c=>({name:c.name,status:c.status})),missed};
}catch(e){summary.error=e.message;process.exitCode=1;}
finally{
  clearInterval(heartbeat);
  if(socket){socket.onclose=null;socket.close();}
  console.log('SUMMARY',JSON.stringify(summary));
  await fs.writeFile(path.join(out,'summary.json'),JSON.stringify(summary,null,1));
  if(child){
    try{await request(target,'/commands',{command:'quit'});}catch{}
    if(child.exitCode===null)await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
  }
  console.log('dir',out);
}
