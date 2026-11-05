// End-to-end check of the packaged app against real public chat. Read-only: it never sends
// anything to a chat. All data stays in a fresh folder under build-chat/live-e2e.
//   node native-qt/tools/chat-live-e2e.mjs --twitch xqc --youtube VIDEO_ID [--minutes 2]
import fs from 'node:fs/promises';
import {existsSync,readdirSync,statSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request,screenshot} from './mcp/studio-client.mjs';

const exec=promisify(execFile);
const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const twitch=flag('twitch','xqc'),youtube=flag('youtube'),minutes=Number(flag('minutes',2));
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const dist=path.dirname(exe);
const out=path.join(root,'native-qt/build-chat/live-e2e',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const control=path.join(out,'control.json'),show=path.join(out,'show.mixer.json');
const result={out,checks:[],measurements:{},completed:false};
let target,child,truth;

const until=async(fn,description,ms=30000)=>{const start=Date.now();while(Date.now()-start<ms){const v=await fn();if(v)return v;await delay(250);}throw Error('Timed out: '+description);};
const check=(ok,name,detail)=>{result.checks.push({name,ok:!!ok,detail});console.log((ok?'PASS ':'FAIL ')+name+(detail===undefined?'':' :: '+JSON.stringify(detail)));return !!ok;};
const command=async(c,args={})=>{const v=await request(target,'/studio/command',{command:c,...args});if(v.error)throw Error(v.error);return v;};
const status=()=>command('chat.status');
const shot=async(name,args={target:'program'})=>{await delay(400);return screenshot(target,args,path.join(out,name));};
// Preserve case, punctuation, emoji and emote names. Removing these can turn
// different messages into the same identity and hide missing/duplicate delivery.
const normal=t=>String(t??'').normalize('NFC').replace(/[\u200B\uFEFF]/g,'').replace(/\s+/gu,' ').trim();
const launch=async()=>{
  await fs.rm(control,{force:true});
  child=spawn(exe,['--studio-headless',`--studio-show=${show}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out},windowsHide:true,stdio:'ignore'});
  target=await until(async()=>{try{return await discovery(control);}catch{return false;}},'control server');
};
const quit=async()=>{try{await request(target,'/commands',{command:'quit'});}catch{}await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null){child.kill();return false;}return true;};
const tree=async()=>{
  const {stdout}=await exec('powershell',['-NoProfile','-Command','Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,@{n="mem";e={$_.PrivatePageCount}} | ConvertTo-Json -Compress'],{maxBuffer:1<<26});
  const all=JSON.parse(stdout),ids=new Set([child.pid]);
  for(let grew=true;grew;){grew=false;for(const p of all)if(ids.has(p.ParentProcessId)&&!ids.has(p.ProcessId)){ids.add(p.ProcessId);grew=true;}}
  const rows=all.filter(p=>ids.has(p.ProcessId));
  const sum=f=>Math.round(rows.filter(f).reduce((a,p)=>a+p.mem,0)/1048576);
  return {processes:rows.length,totalMiB:sum(()=>true),webviewMiB:sum(p=>/msedgewebview2/i.test(p.Name)),studioMiB:sum(p=>/desktop-studio/i.test(p.Name))};
};
const findFfmpeg=()=>{for(const c of ['ffmpeg.exe','ffmpeg/bin/ffmpeg.exe','bin/ffmpeg.exe']){const p=path.join(dist,c);if(existsSync(p))return p;}return null;};

try{
  await launch();
  check((await status()).available,'SSN pack is available in the packaged app');

  // Real channels, added the way the paste box adds them.
  const tw=await command('chat.channel.add',{input:`https://www.twitch.tv/${twitch}`});
  const yt=youtube?await command('chat.channel.add',{input:`https://www.youtube.com/watch?v=${youtube}`}):null;
  check(tw.channel?.platform==='twitch'||/twitch/i.test(JSON.stringify(tw.channel)),'Twitch link detected',tw.channel);
  if(yt)check(/youtube/i.test(JSON.stringify(yt.channel)),'YouTube link detected',yt.channel);
  truth=spawn('node',[path.join(root,'native-qt/tools/chat-spike/twitch-truth.mjs'),'--channel',twitch,'--out',path.join(out,'truth'),'--duration-min',String(minutes+0.5)],{stdio:'ignore',windowsHide:true});

  const firstAt=Date.now();
  const first=await until(async()=>(await command('chat.messages')).messages.find(m=>/twitch/i.test(m.type||m.channel||'')),'first real Twitch comment',90000);
  result.measurements.firstMessageSeconds=(Date.now()-firstAt)/1000;
  check(true,'Real Twitch chat reaches the panel',{seconds:result.measurements.firstMessageSeconds});

  // Let it run, then score the panel's feed against anonymous IRC.
  const seen=new Map(),latencies=[];let polls=0,slowest=0;const stop=Date.now()+minutes*60000;
  while(Date.now()<stop){const t=Date.now();const m=await command('chat.messages');slowest=Math.max(slowest,Date.now()-t);polls++;for(const r of m.messages)if(!seen.has(r.id))seen.set(r.id,{...r,seenMs:Date.now()});await delay(500);}
  result.measurements.controlLatencyWorstMs=slowest;
  const rows=[...seen.values()];
  const counts={};for(const r of rows){const k=String(r.type||r.channel||'?').split(':')[0];counts[k]=(counts[k]||0)+1;}
  result.measurements.messagesByType=counts;
  console.log('messages',counts,'worst control latency ms',slowest);
  if(youtube)check(rows.some(r=>/youtube/i.test(r.type||r.channel||'')),'Real YouTube chat reaches the panel',{count:counts.youtube||0});
  if(truth.exitCode===null)await new Promise(r=>truth.once('exit',r));
  await fs.writeFile(path.join(out,'captured.json'),JSON.stringify(rows,null,2));
  const truthFile=path.join(out,'truth',`truth-twitch-${twitch}.jsonl`);
  const reference=(await fs.readFile(truthFile,'utf8')).trim().split('\n').map(l=>JSON.parse(l)).filter(r=>r.payload?.message?.chatmessage!==undefined);
  const startMs=firstAt+30000,endMs=stop-15000;
  // Only comments that arrived during the scored time can satisfy it. Allow for the panel beating the reference by a moment.
  const captured=new Map();for(const r of rows.filter(r=>/twitch/i.test(r.type||r.channel||'')&&r.seenMs>=startMs-5000)){const k=normal(r.chatname)+'|'+normal(r.chatmessage);captured.set(k,(captured.get(k)||0)+1);}
  // A moderator can remove a comment before the page shows it. Those are correct omissions, and are counted separately.
  const moderation=(await fs.readFile(path.join(out,'truth',`truth-twitch-${twitch}-events.jsonl`),'utf8').catch(()=>'')).trim().split('\n').filter(Boolean).map(l=>JSON.parse(l)).map(e=>({at:e.receivedMs,raw:e.payload?.rawIRC||''}));
  const removedIds=new Map(moderation.map(e=>[e.raw.match(/target-msg-id=([^; ]+).* CLEARMSG /)?.[1],e.at]).filter(([id])=>id));
  const removed=r=>(removedIds.get(r.payload.message.id)>=r.receivedMs&&removedIds.get(r.payload.message.id)-r.receivedMs<=5000)||moderation.some(e=>e.at>=r.receivedMs&&e.at-r.receivedMs<=2000&&e.raw.toLowerCase().endsWith(` clearchat #${twitch.toLowerCase()} :${r.payload.message.username}`));
  let matched=0,refs=0,removedByModerators=0,miss=[];
  for(const r of reference){if(r.receivedMs<startMs||r.receivedMs>endMs)continue;refs++;const k=normal(r.payload.message.chatname)+'|'+normal(r.payload.message.chatmessage);if(captured.get(k)>0){matched++;captured.set(k,captured.get(k)-1);}else if(removed(r)){refs--;removedByModerators++;}else if(miss.length<6)miss.push(r.payload.message.chatname+': '+String(r.payload.message.chatmessage).slice(0,50));}
  result.measurements.twitch={reference:refs,removedByModerators,matched,rate:refs?matched/refs:null,missedSample:miss};
  const coverage=JSON.parse(await fs.readFile(path.join(out,'truth',`truth-twitch-${twitch}-meta.json`),'utf8'));
  check(coverage.intervals.some(i=>i.startMs<=startMs&&i.endMs>=endMs),'IRC reference covers the complete observation interval');
  check(refs>20&&matched===refs,'Every eligible IRC text occurrence reaches the panel',result.measurements.twitch);
  result.measurements.chatTree=(await status()).resources;result.measurements.memoryLive=await tree();console.log('memory',result.measurements.memoryLive);

  // Selecting is private: output pixels must not change, whatever is selected.
  const scene=await command('scene.add',{name:'Live test',background:'#2a3b4c'});await command('scene.take');
  await delay(600);
  const quiet=await shot('01-program-before.png');
  const live=(await command('chat.messages')).messages.filter(r=>r.chatmessage&&String(r.chatmessage).length<80).at(-1);
  await command('chat.select',{id:live.id});
  const selected=await shot('02-program-selected.png');
  check((await fs.readFile(quiet.path)).equals(await fs.readFile(selected.path)),'Selecting a real comment does not change Program output');
  check((await status()).selected===live.id&&!(await status()).on_screen,'Selection is reported as private');
  const pausedA=(await status()).pending_comments;await delay(6000);const pausedB=(await status()).pending_comments;
  check(pausedB>pausedA,'Incoming comments buffer while one is selected',{pausedA,pausedB});

  // Record, show a real comment, then kill the chat browser in the middle of the recording.
  const clip=path.join(out,'live-comment.mkv');
  await command('record.start',{path:clip});
  await delay(2000);
  const shownAt=Date.now();await command('chat.show',{id:live.id,style:'bar'});
  await until(async()=>(await status()).on_screen,'real comment composed');
  result.measurements.showToComposedMs=Date.now()-shownAt;result.measurements.showStages=(await status()).timing;
  check(true,'Real comment reaches composition',{ms:result.measurements.showToComposedMs,comment:String(live.chatname)});
  await command('chat.keep');await shot('03-real-comment.png');
  await delay(5000);

  const before=await status();const browserPid=before.browser_pid;
  check(browserPid>0,'Chat browser process is reported',{browserPid});
  await exec('taskkill',['/PID',String(browserPid),'/T','/F']).catch(e=>console.log('taskkill',e.message));
  const killedAt=Date.now();
  await delay(1500);
  const mid=await request(target,'/studio/command',{command:'record.status'}).catch(()=>null);
  await shot('04-program-after-kill.png');
  check(child.exitCode===null,'Studio survives the chat browser being killed');
  const recovered=await until(async()=>{const s=await status();return s.ready&&s.browser_pid&&s.browser_pid!==browserPid?s:false;},'chat engine restarts',60000).catch(e=>{check(false,e.message);return null;});
  if(recovered){
    result.measurements.recoverySeconds=(Date.now()-killedAt)/1000;
    check(true,'Chat engine restarts by itself',{seconds:result.measurements.recoverySeconds});
    const count=(await command('chat.messages')).messages.length;
    await command('chat.hide');
    await until(async()=>(await command('chat.messages')).messages.length>count,'comments flow again after restart',60000).then(()=>check(true,'Comments flow again after restart')).catch(e=>check(false,e.message));
  }
  await delay(3000);
  await command('record.stop');
  await until(async()=>existsSync(clip)&&statSync(clip).size>0,'recording finished',20000);
  await delay(1500);
  const ff=findFfmpeg();
  if(ff){
    const probe=await exec(ff,['-hide_banner','-i',clip,'-f','null','-'],{maxBuffer:1<<26}).catch(e=>e);
    const log=String(probe.stderr||'');const time=[...log.matchAll(/time=(\d+):(\d+):([\d.]+)/g)].at(-1);
    const seconds=time?Number(time[1])*3600+Number(time[2])*60+Number(time[3]):0;
    result.measurements.recordingSeconds=seconds;
    check(seconds>10,'Recording stays playable across the chat browser kill',{seconds,size:statSync(clip).size});
    await exec(ff,['-hide_banner','-y','-ss','8','-i',clip,'-frames:v','1',path.join(out,'05-recorded-frame.png')]).catch(e=>console.log('frame',String(e.stderr||e.message).slice(-200)));
  }else check(false,'Packaged FFmpeg not found',readdirSync(dist).slice(0,30));

  // Channels come back after a restart, without being asked.
  check(await quit(),'Studio exits cleanly with chat running');
  await delay(1500);
  await launch();
  const back=await until(async()=>{const s=await status();return s.channels?.length>=1?s:false;},'saved channels load');
  check(back.channels.length===(youtube?2:1),'Channels persist across restart',back.channels.map(c=>c.id));
  await until(async()=>(await command('chat.messages')).messages.length>0,'comments flow after restart',90000).then(()=>check(true,'Comments flow after a full restart')).catch(e=>check(false,e.message));
  const app=await shot('06-app-window.png',{target:'window'}).catch(async e=>{console.log('window shot',e.message);return null;});
  const saved=await fs.readFile(path.join(out,'chat.json'),'utf8').catch(()=>'');
  check(!/token|password|cookie|secret/i.test(saved),'Chat settings hold no credentials',saved.slice(0,200));
  result.completed=true;
}catch(error){result.error=error.stack;console.log('ERROR',error.stack);process.exitCode=1;}
finally{
  try{if(target)result.lastState=await status();}catch{}
  try{if(target&&child?.exitCode===null)await quit();}catch{}
  if(truth&&truth.exitCode===null)truth.kill();
  const failed=result.checks.filter(c=>!c.ok).length;
  console.log(`\n${result.checks.length-failed} passed, ${failed} failed, completed=${result.completed}\n${out}`);
  if(failed)process.exitCode=1;
  await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));
}
