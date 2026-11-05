// Measures the packaged app's chat cost over time on real public chat. Read-only and isolated.
//   node native-qt/tools/chat-soak.mjs --twitch xqc [--youtube VIDEO_ID] [--minutes 15]
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,health,request} from './mcp/studio-client.mjs';

const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const twitch=flag('twitch','xqc'),youtube=flag('youtube'),kick=flag('kick'),minutes=Number(flag('minutes',15)),maxSilence=Number(flag('max-silence',300));
if(!Number.isFinite(minutes)||minutes<1||minutes>240||!Number.isFinite(maxSilence)||maxSilence<1)throw Error('Invalid soak duration/silence limit');
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/soak',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const control=path.join(out,'control.json');
const artifact={exe,sha256:createHash('sha256').update(await fs.readFile(exe)).digest('hex'),startedUtc:new Date().toISOString(),twitch,youtube,kick};
await fs.writeFile(path.join(out,'artifact.json'),JSON.stringify(artifact,null,2));
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out},windowsHide:true,stdio:'ignore'});
let target;
try{
const startupDeadline=Date.now()+60000;
while(!target&&Date.now()<startupDeadline){try{const candidate=await discovery(control);await health(candidate);target=candidate;}catch{await delay(250);}}
if(!target)throw Error('App control did not become ready');
const raw=c=>request(target,'/studio/command',c);
for(const input of [`https://www.twitch.tv/${twitch}`,...(youtube?[`https://www.youtube.com/watch?v=${youtube}`]:[]),...(kick?[`https://kick.com/${kick}`]:[])]){
 const added=await raw({command:'chat.channel.add',input});if(!added.channel?.id)throw Error(added.error||'Channel was not added');
}
const samples=[],start=Date.now(),pids=new Set();let notReady=0;
// The panel keeps 2,000 comments, so its count can't show flow. Track new comment ids per platform.
const wanted=[...(twitch?['twitch']:[]),...(youtube?['youtube']:[]),...(kick?['kick']:[])];
const flow=Object.fromEntries(wanted.map(type=>[type,{total:0,firstAt:null,lastAt:null,longestSilence:0,seen:new Set()}]));
const reading={};
while(Date.now()-start<minutes*60000){
  const s=await raw({command:'chat.status'});
  const r=s.resources||{};
  const t=Math.round((Date.now()-start)/1000);
  if(s.browser_pid)pids.add(s.browser_pid);
  if(!s.ready&&t>120)notReady++;
  for(const c of s.channels||[]){const row=reading[c.name]??={reading:0,other:{}};if(c.status==='Reading comments')row.reading++;else row.other[c.status]=(row.other[c.status]||0)+1;}
  if(samples.length%6===0) {
    const newest={};
    const reply=await raw({command:'chat.messages'}).catch(()=>null);
    for(const m of reply?.messages||[])(newest[m.type]??=new Set()).add(m.id);
    // A failed read says nothing about comments, and must not make old ones look new.
    if(reply)for(const[type,f]of Object.entries(flow)){
      const ids=newest[type]||new Set();let fresh=0;for(const id of ids)if(!f.seen.has(id))fresh++;
      f.seen=ids;
      if(fresh){f.total+=fresh;f.firstAt??=t;f.longestSilence=Math.max(f.longestSilence,t-(f.lastAt??0));f.lastAt=t;}
    }
  }
  samples.push({t,private:Math.round(r.private_mib||0),peak:Math.round(r.peak_job_mib||0),cpu:r.cpu_cores,ready:s.ready,count:s.count,oversized:s.oversized_messages,browserPid:s.browser_pid,fresh:Object.fromEntries(Object.entries(flow).map(([k,f])=>[k,f.total]))});
  if(samples.length%12===1){console.log(JSON.stringify(samples.at(-1)));await fs.writeFile(path.join(out,'checkpoint.json'),JSON.stringify({artifact,samples},null,1));}
  await delay(5000);
}
const elapsedSeconds=Math.round((Date.now()-start)/1000);
const channels=Object.fromEntries(Object.entries(flow).map(([k,f])=>[k,{comments:f.total,firstAt:f.firstAt,lastAt:f.lastAt,longestSilence:Math.max(f.longestSilence,elapsedSeconds-(f.lastAt??0))}]));
const privates=samples.filter(s=>s.ready&&s.private>0).map(s=>s.private).sort((a,b)=>a-b);
const pick=p=>privates[Math.floor((privates.length-1)*p)];
const early=samples.filter(s=>s.t>120&&s.t<300&&s.private).map(s=>s.private),late=samples.slice(-36).filter(s=>s.private).map(s=>s.private);
const mean=a=>a.length?Math.round(a.reduce((x,y)=>x+y,0)/a.length):null;
const cpus=samples.map(s=>s.cpu).filter(c=>typeof c==='number');
const summary={minutes,p50:pick(.5),p95:pick(.95),max:privates.at(-1),meanMinute2to5:mean(early),meanLast3Minutes:mean(late),peakJob:Math.max(...samples.map(s=>s.peak)),cpuCoresMean:cpus.length?cpus.reduce((a,b)=>a+b,0)/cpus.length:null,browserPids:pids.size,notReadySamples:notReady,samples:samples.length,finalCount:samples.at(-1)?.count,channels,reading};
console.log('SUMMARY',JSON.stringify(summary));
const failures=[];
for(const[type,c]of Object.entries(channels))if(!c.comments||c.longestSilence>maxSilence)failures.push(`${type}: ${c.comments} comments, longest silence ${c.longestSilence}s (limit ${maxSilence}s)`);
if(pids.size!==1)failures.push(`chat browser changed ${pids.size-1} times`);
if(notReady)failures.push(`chat not ready in ${notReady} samples after the first two minutes`);
for(const f of failures)console.log('FAIL '+f);
if(failures.length)process.exitCode=1;
await fs.writeFile(path.join(out,'soak.json'),JSON.stringify({artifact,summary,failures,samples},null,1));
}catch(e){process.exitCode=1;await fs.writeFile(path.join(out,'failure.json'),JSON.stringify({error:e.stack,artifact},null,2));console.error(e.stack);}
finally{
try{if(target)await request(target,'/commands',{command:'quit'});}catch{}
if(child.exitCode===null)await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
console.log('dir',out);
}
