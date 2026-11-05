// Compares YouTube comments in the packaged app against YouTube's own "Live chat" feed.
// Read-only and isolated: nothing is sent to a chat. DevTools is enabled only for this QA launch.
//   node native-qt/tools/youtube-compare.mjs --video ID [--seconds 600] [--port 9397]
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request} from './mcp/studio-client.mjs';

const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const video=flag('video'),seconds=Number(flag('seconds',600)),port=Number(flag('port',9397));
if(!/^[a-zA-Z0-9_-]{11}$/.test(video||''))throw Error('--video needs a YouTube video ID');
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/youtube',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const entities={'&amp;':'&','&lt;':'<','&gt;':'>','&quot;':'"','&#39;':"'",'&#x27;':"'"};
const normal=t=>String(t??'').replace(/&(?:amp|lt|gt|quot|#39|#x27);/g,e=>entities[e]).normalize('NFC').replace(/[​﻿]/g,'').replace(/\s+/gu,' ').trim();

// Reads the unfiltered feed the way the popout does, so the reference does not depend on the app.
const browser={'user-agent':'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36','accept-language':'en-US,en;q=0.9'};
async function openFeed(){
  const html=await (await fetch(`https://www.youtube.com/live_chat?is_popout=1&v=${video}`,{headers:browser})).text();
  const key=html.match(/"INNERTUBE_API_KEY":"([^"]+)"/)?.[1];
  const context=JSON.parse(html.match(/"INNERTUBE_CONTEXT":(\{.*?\}),"INNERTUBE_CONTEXT_CLIENT_NAME"/s)?.[1]||'null');
  const data=JSON.parse(html.match(/ytInitialData"\]\s*=\s*(\{.*?\});\s*<\/script>/s)?.[1]||html.match(/ytInitialData\s*=\s*(\{.*?\});\s*<\/script>/s)?.[1]||'null');
  const items=data?.contents?.liveChatRenderer?.header?.liveChatHeaderRenderer?.viewSelector?.sortFilterSubMenuRenderer?.subMenuItems||[];
  const live=items.find(i=>/live chat/i.test(i.title))||items.at(-1);
  const continuation=live?.continuation?.reloadContinuationData?.continuation;
  if(!key||!context||!continuation)throw Error('could not open the reference feed');
  return {key,context,continuation,views:items.map(i=>i.title)};
}
const truth=new Map();let feedStopped=false,feedError='',lastFeedAt=0,longestFeedGap=0;
async function readFeed(feed){
  let continuation=feed.continuation;
  while(!feedStopped){
    let wait=5000;
    try{
      const reply=await (await fetch(`https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?key=${feed.key}&prettyPrint=false`,{method:'POST',headers:{...browser,'content-type':'application/json'},body:JSON.stringify({context:feed.context,continuation})})).json();
      const chat=reply.continuationContents?.liveChatContinuation;
      if(!chat){feedError='reference feed returned no chat';break;}
      if(lastFeedAt)longestFeedGap=Math.max(longestFeedGap,Date.now()-lastFeedAt);
      lastFeedAt=Date.now();
      for(const action of chat.actions||[]){
        const row=action.addChatItemAction?.item?.liveChatTextMessageRenderer;
        if(!row||truth.has(row.id))continue;
        const runs=row.message?.runs||[];
        truth.set(row.id,{at:Number(row.timestampUsec)/1000,received:Date.now(),user:row.authorName?.simpleText,text:runs.map(r=>r.text??'').join(''),plain:runs.every(r=>r.text!==undefined)});
      }
      const next=chat.continuations?.[0];const timed=next?.timedContinuationData||next?.invalidationContinuationData||next?.reloadContinuationData;
      if(timed?.continuation)continuation=timed.continuation;
      wait=Math.min(5000,timed?.timeoutMs||5000);
    }catch(e){feedError=e.message;}
    await delay(wait);
  }
}

const control=path.join(out,'control.json');
const flags=`--remote-debugging-port=${port} --disable-background-timer-throttling --disable-backgrounding-occluded-windows --disable-renderer-backgrounding`;
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:flags},windowsHide:true,stdio:'ignore'});
let target;for(let i=0;i<120&&!target;i++){try{target=await discovery(control);}catch{await delay(250);}}
const raw=c=>request(target,'/studio/command',c);
async function inPage(page,expression){
  const ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise((r,x)=>{ws.onopen=r;ws.onerror=x;});
  const result=await new Promise((r,x)=>{ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id===1)r(m.result);};ws.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression,returnByValue:true}}));setTimeout(()=>x(Error('page timeout')),10000);});
  ws.close();return result?.result?.value;
}
const PROBE=`JSON.stringify({loaded:Math.round(performance.timeOrigin),rows:document.querySelector('yt-live-chat-item-list-renderer #items')?.children.length??null,view:document.querySelector('#trigger #label-text')?.textContent?.trim()??null})`;
const chatPage=async()=>(await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(p=>p.type==='page'&&p.url.includes('/live_chat')&&p.url.includes(video));
let summary={};
try{
  const feed=await openFeed();
  console.log('reference views',JSON.stringify(feed.views));
  const reading=readFeed(feed);
  await raw({command:'chat.channel.add',input:`https://www.youtube.com/watch?v=${video}`});
  const started=Date.now();const states={},loads=new Set(),samples=[];let lastTime=started;
  // The panel keeps only its newest comments, so gather them during the run.
  const seen=new Map();
  const collect=async()=>{for(const m of (await raw({command:'chat.messages'})).messages)if(m.type==='youtube'&&!seen.has(m.id))seen.set(m.id,{...m,seenMs:Date.now()});};
  while(Date.now()-started<seconds*1000){
    await delay(5000);
    await collect();
    const status=await raw({command:'chat.status'}).catch(()=>null);
    const state=status?.channels?.[0]?.status||'none';states[state]=(states[state]||0)+1;
    if(Date.now()-lastTime>=30000){
      lastTime=Date.now();
      const page=await chatPage().catch(()=>null);
      const probe=page?JSON.parse(await inPage(page,PROBE).catch(()=>'{}')||'{}'):{};
      if(probe.loaded)loads.add(probe.loaded);
      samples.push({t:Math.round((Date.now()-started)/1000),state,...probe});
    }
  }
  feedStopped=true;await Promise.race([reading,delay(6000)]);
  const stopped=Date.now();
  await collect();
  const rows=[...seen.values()];
  await fs.writeFile(path.join(out,'captured.json'),JSON.stringify(rows,null,1));
  await fs.writeFile(path.join(out,'truth.json'),JSON.stringify([...truth.values()],null,1));
  await fs.writeFile(path.join(out,'samples.json'),JSON.stringify(samples,null,1));
  // Allow time to attach at the start, and to be delivered at the end.
  const inWindow=[...truth.values()].filter(t=>t.at>=started+45000&&t.at<=stopped-15000).sort((a,b)=>a.at-b.at);
  const plain=inWindow.filter(t=>t.plain&&normal(t.text));
  // Only comments that arrived during the scored time can satisfy it.
  const key=(user,text)=>normal(user).toLowerCase()+'|'+normal(text);
  const texts=new Map();for(const r of rows.filter(r=>r.seenMs>=started+40000)){const k=key(r.chatname,r.chatmessage);texts.set(k,(texts.get(k)||0)+1);}
  let matched=0;const missed=[],buckets={};
  for(const t of plain){
    const k=key(t.user,t.text),hit=texts.get(k)>0,b=Math.floor((t.at-started)/60000);
    (buckets[b]??=[0,0])[hit?0:1]++;
    if(hit){matched++;texts.set(k,texts.get(k)-1);}else if(missed.length<8)missed.push(t.user+': '+t.text.slice(0,120));
  }
  console.log('60s buckets hit/miss',Object.values(buckets).map(b=>b.join('/')).join(' '));
  const stalled=Object.values(buckets).filter(([hit,miss])=>miss>=5&&hit===0).length;
  // A reference that stopped proves nothing about the time it was away.
  const referenceHeld=!feedError&&longestFeedGap<=30000&&stopped-lastFeedAt<=30000;
  if(!referenceHeld||!plain.length||matched/plain.length<0.9||stalled)process.exitCode=1;
  summary={video,seconds,referenceHeld,longestFeedGap,feedError,referenceTotal:inWindow.length,referencePlainText:plain.length,matched,rate:plain.length?matched/plain.length:null,stalled,panelYouTube:rows.length,pageLoads:loads.size,view:samples.at(-1)?.view??null,states,missed};
}catch(e){summary.error=e.message;process.exitCode=1;}
finally{
  feedStopped=true;
  console.log('SUMMARY',JSON.stringify(summary));
  await fs.writeFile(path.join(out,'summary.json'),JSON.stringify(summary,null,1));
  try{await request(target,'/commands',{command:'quit'});}catch{}
  await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
  console.log('dir',out);
}
