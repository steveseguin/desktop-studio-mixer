// Forces a capture page past the chat memory budget during a recording, in the packaged app.
// Output must carry on, only chat may restart. DevTools is enabled only for this QA launch.
// Read-only towards the platform: nothing is ever sent to a chat.
//   node native-qt/tools/chat-overload-e2e.mjs --twitch xqc
import fs from 'node:fs/promises';
import {existsSync,statSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,health,request} from './mcp/studio-client.mjs';

const exec=promisify(execFile);
const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const twitch=flag('twitch','xqc'),port=Number(flag('port',9391));
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const dist=path.dirname(exe);
const out=path.join(root,'native-qt/build-chat/overload',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
await fs.writeFile(path.join(out,'artifact.json'),JSON.stringify({exe,sha256:createHash('sha256').update(await fs.readFile(exe)).digest('hex'),startedUtc:new Date().toISOString()},null,2));
const control=path.join(out,'control.json');
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:`--remote-debugging-port=${port}`},windowsHide:true,stdio:'ignore'});
let target;
const raw=c=>request(target,'/studio/command',c);
const status=()=>raw({command:'chat.status'});
const results=[];let failed=0;
const check=(ok,name,detail)=>{results.push({ok:!!ok,name,detail});if(!ok)failed++;console.log((ok?'PASS ':'FAIL ')+name+(detail===undefined?'':' :: '+JSON.stringify(detail)));};
const until=async(fn,what,ms=60000)=>{const s=Date.now();while(Date.now()-s<ms){const v=await fn();if(v)return v;await delay(500);}throw Error('Timed out: '+what);};
try{
  const startupDeadline=Date.now()+60000;
  while(!target&&Date.now()<startupDeadline){try{const candidate=await discovery(control);await health(candidate);target=candidate;}catch{await delay(250);}}
  if(!target)throw Error('App control did not become ready');
  await raw({command:'chat.channel.add',input:`https://www.twitch.tv/${twitch}`});
  await until(async()=>(await raw({command:'chat.messages'})).messages.length>0,'first comment',90000);
  const before=await status();
  check(true,'Chat is running before the overload',{private:Math.round(before.resources.private_mib),limit:before.resources.memory_limit_mib,pid:before.browser_pid});
  const clip=path.join(out,'overload.mkv');
  await raw({command:'record.start',path:clip});
  const recordingStarted=Date.now();
  await delay(2000);
  const page=(await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(p=>p.url.includes('popout'));
  const ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise(r=>ws.onopen=r);
  // Allocates and touches 200 MiB a second until the budget stops it.
  ws.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression:`window.__hog=[];setInterval(()=>{for(let i=0;i<4;i++){const a=new Uint8Array(50*1048576);for(let j=0;j<a.length;j+=4096)a[j]=1;window.__hog.push(a);}},1000);1`}}));
  const started=Date.now();let peak=0,slowest=0,restarted=null,problem='';
  while(Date.now()-started<90000){
    const t=Date.now();const s=await status();slowest=Math.max(slowest,Date.now()-t);
    peak=Math.max(peak,s.resources?.private_mib||0);
    if(s.problem)problem=s.problem;
    if(s.browser_pid&&s.browser_pid!==before.browser_pid&&s.ready){restarted=s;break;}
    await delay(500);
  }
  ws.close?.();
  check(child.exitCode===null,'Studio survives chat going over budget');
  check(!!restarted,'Chat restarts by itself after going over budget',{seconds:(Date.now()-started)/1000,peakMiB:Math.round(peak),limit:before.resources.memory_limit_mib,problem});
  check(peak<=before.resources.memory_limit_mib*1.1,'Chat memory stays near its budget',{peakMiB:Math.round(peak)});
  check(/resources/i.test(problem),'The user is told in plain language',problem);
  check(slowest<1000,'Control stays responsive throughout',{worstMs:slowest});
  await until(async()=>(await raw({command:'chat.messages'})).messages.length>0&&(await status()).channels?.[0]?.status==='Reading comments','comments flow after recovery',90000).then(()=>check(true,'Comments flow again after recovery')).catch(e=>check(false,e.message));
  await delay(3000);
  await raw({command:'record.stop'});
  const recordedFor=(Date.now()-recordingStarted)/1000;
  await until(()=>existsSync(clip)&&statSync(clip).size>0,'recording finished',20000);await delay(1500);
  const ffmpeg=['ffmpeg.exe','ffmpeg/bin/ffmpeg.exe','bin/ffmpeg.exe','ffmpeg/ffmpeg.exe'].map(c=>path.join(dist,c)).find(existsSync);
  const probe=await exec(ffmpeg,['-hide_banner','-i',clip,'-f','null','-'],{maxBuffer:1<<26}).catch(e=>e);
  const log=String(probe.stderr||'');const time=[...log.matchAll(/time=(\d+):(\d+):([\d.]+)/g)].at(-1);const seconds=time?Number(time[1])*3600+Number(time[2])*60+Number(time[3]):0;
  // The whole recording must decode and cover the overload, so output that stopped early fails.
  check(!probe.code&&seconds>=recordedFor*0.9,'Recording stays playable across the overload',{seconds,recordedFor,exitCode:probe.code??0,size:statSync(clip).size});
}catch(e){check(false,'Stopped: '+e.message);}
finally{
  try{await request(target,'/commands',{command:'quit'});}catch{}
  await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
  await fs.writeFile(path.join(out,'results.json'),JSON.stringify(results,null,2));
  console.log(`${results.length-failed} passed, ${failed} failed\n${out}`);if(failed)process.exitCode=1;
}
