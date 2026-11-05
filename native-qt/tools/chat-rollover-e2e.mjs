// Controlled rollover in the packaged app, using two real live broadcasts on one channel.
// Only the local old-page status is changed to ended; no platform request posts content.
// This does NOT qualify real broadcaster-driven rollover.
// node tools/chat-rollover-e2e.mjs --from VIDEO_ID --to VIDEO_ID
import fs from 'node:fs/promises';
import path from 'node:path';
import net from 'node:net';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,health,request,screenshot} from './mcp/studio-client.mjs';
const flag=name=>{const i=process.argv.indexOf('--'+name);return i<0?undefined:process.argv[i+1];};
const from=flag('from'),to=flag('to');
if(![from,to].every(v=>/^[a-zA-Z0-9_-]{11}$/.test(v||''))||from===to)throw Error('Use distinct --from and --to video IDs on one live channel');
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/rollover',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const control=path.join(out,'control.json');
const result={kind:'controlled-local-ended-signal',realBroadcastRollover:false,exe,sha256:createHash('sha256').update(await fs.readFile(exe)).digest('hex'),from,to,checks:[],states:[]};
let target,child;
const server=net.createServer();await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;await new Promise(r=>server.close(r));
const check=(ok,name,detail)=>{result.checks.push({ok:!!ok,name,detail});console.log((ok?'PASS ':'FAIL ')+name+(detail?': '+JSON.stringify(detail):''));if(!ok)throw Error(name);};
const until=async(fn,name,ms=120000)=>{const end=Date.now()+ms;while(Date.now()<end){const value=await fn();if(value)return value;await delay(500);}throw Error('Timed out: '+name);};
const cmd=async(command,args={})=>{const reply=await request(target,'/studio/command',{command,...args});if(reply.error)throw Error(reply.error);return reply;};
const pages=async()=>(await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).filter(p=>p.type==='page');
// DevTools starts after local control and disappears briefly on full restart.
const pageFor=async video=>{try{return (await pages()).find(p=>p.url.includes('/live_chat')&&new URL(p.url).searchParams.get('v')===video);}catch{return null;}};
async function evaluate(page,expression){
 const ws=new WebSocket(page.webSocketDebuggerUrl);let timer;
 try{
  await Promise.race([new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;}),delay(5000).then(()=>{throw Error('CDP connection timeout');})]);
  const answer=await new Promise((resolve,reject)=>{timer=setTimeout(()=>reject(Error('CDP response timeout')),8000);ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id===1)m.error?reject(Error(m.error.message)):resolve(m.result);};ws.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression,returnByValue:true}}));});
  if(answer.exceptionDetails)throw Error(answer.exceptionDetails.text);return answer.result?.value;
 }finally{clearTimeout(timer);ws.close();}
}
async function launch(){
 await fs.rm(control,{force:true});
 child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:`--remote-debugging-port=${port}`},windowsHide:true,stdio:'ignore'});
 target=await until(async()=>{try{const t=await discovery(control);await health(t);return t;}catch{return false;}},'control readiness',60000);
}
async function quit(){
 try{if(target)await request(target,'/commands',{command:'quit'});}catch{}
 if(child?.exitCode===null)await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);
 if(child?.exitCode===null)child.kill();
}
try{
 await launch();
 const added=await cmd('chat.channel.add',{input:`https://www.youtube.com/watch?v=${from}`});const id=added.channel.id;
 await until(async()=>(await cmd('chat.messages')).messages.some(m=>m.channel===id),'real comments on old broadcast');
 const oldPage=await until(()=>pageFor(from),'old chat page');
 const oldState=await until(()=>evaluate(oldPage,'window.__studioVideoProbe?.data?.microformat?.playerMicroformatRenderer?.liveBroadcastDetails?.isLiveNow === true ? window.__studioVideoProbe.data : null'),'confirmed old live broadcast');
 result.oldState=oldState;check(!!oldState.videoDetails.channelId,'Old live broadcast exposes a channel resolver');
 await delay(11000); // Let native state observe the confirmed-live response and reset backoff.
 await evaluate(oldPage,`(()=>{const c=window.__studioVideoProbe;if(!c?.data)throw Error('Missing observed state');c.data.videoDetails.isLive=false;c.data.microformat.playerMicroformatRenderer.liveBroadcastDetails={isLiveNow:false,endTimestamp:new Date().toISOString()};c.at=Date.now();window.ytInitialPlayerResponse=c.data;return true;})()`);
 const start=Date.now();
 const newPage=await until(async()=>{const s=await cmd('chat.status');const row=s.channels.find(c=>c.id===id);if(result.states.at(-1)?.status!==row?.status)result.states.push({seconds:(Date.now()-start)/1000,status:row?.status});return pageFor(to);},'native channel resolution to the second live broadcast',180000);
 check(true,'Native recovery reaches the actual second broadcast',{seconds:(Date.now()-start)/1000,url:newPage.url});
 const newState=await until(()=>evaluate(newPage,'window.__studioVideoProbe?.data?.microformat?.playerMicroformatRenderer?.liveBroadcastDetails?.isLiveNow === true ? window.__studioVideoProbe.data : null'),'confirmed replacement live broadcast');
 check(newState.videoDetails.channelId===oldState.videoDetails.channelId,'Replacement broadcast belongs to the same channel');
 const beforeIds=new Set((await cmd('chat.messages')).messages.map(m=>m.id));
 const comment=await until(async()=>(await cmd('chat.messages')).messages.find(m=>m.channel===id&&!beforeIds.has(m.id)),'new actual comments after rollover');
 check((await cmd('chat.status')).channels.length===1,'Rollover preserves one channel and its identity');
 await cmd('chat.show',{id:comment.id});await until(async()=>(await cmd('chat.status')).on_screen,'replacement comment on Program');
 await screenshot(target,{target:'program'},path.join(out,'after-rollover.png'));
 check(true,'Replacement broadcast comment can be shown on Program');
 await quit();
 result.saved=JSON.parse(await fs.readFile(path.join(out,'chat.json'),'utf8'));
 await launch();
 const restored=await until(async()=>{const s=await cmd('chat.status');return s.channels?.length?s:null;},'saved channel');
 check(restored.channels.length===1&&restored.channels[0].id===id,'Saved channel identity survives full restart');
 // The source broadcast is still genuinely live. The local simulated ending
 // intentionally disappears on restart, so either saved original or replacement
 // live page is valid here. A real ended source must be covered by a real rollover.
 const restoredPage=await until(async()=>(await pageFor(from))||(await pageFor(to)),'restart restores a live broadcast',180000);
 result.restoredVideo=new URL(restoredPage.url).searchParams.get('v');
 await until(async()=>(await cmd('chat.messages')).messages.some(m=>m.channel===id),'real comments after restart');
 check(true,'Restart restores the channel and receives real comments',{video:result.restoredVideo});
 result.completed=true;
}catch(e){result.error=e.stack;process.exitCode=1;console.error(e.stack);}
finally{await quit();await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log('dir',out);}
