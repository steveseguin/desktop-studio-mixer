// Tries real inputs in the packaged app's channel box and reports what a user would see.
// Read-only and isolated. Each case: {input, watchSeconds}. An input may be invalid on purpose.
//   node native-qt/tools/chat-platform-probe.mjs cases.json
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request,screenshot} from './mcp/studio-client.mjs';

const cases=JSON.parse(await fs.readFile(process.argv[2],'utf8'));
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/platform-probe',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const control=path.join(out,'control.json');
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out},windowsHide:true,stdio:'ignore'});
let target;for(let i=0;i<120&&!target;i++){try{target=await discovery(control);}catch{await delay(250);}}
const raw=c=>request(target,'/studio/command',c);
const results=[];
for(const item of cases){
  const row={input:item.input};
  let added;
  try{added=await raw({command:'chat.channel.add',input:item.input});}catch(e){added={error:e.message};}
  if(added.error||!added.channel){row.rejected=String(added.error||JSON.stringify(added)).replace(/^.*Desktop Studio 400: /,'');console.log(JSON.stringify(row));results.push(row);continue;}
  row.detected={platform:added.channel.platform,name:added.channel.name,url:added.channel.url};
  const before=new Set((await raw({command:'chat.messages'})).messages.map(m=>m.id));
  const end=Date.now()+(item.watchSeconds??60)*1000,states=new Set();let fresh=[];
  while(Date.now()<end){
    const s=await raw({command:'chat.status'});
    const ch=s.channels?.find(c=>c.id===added.channel.id);
    if(ch){states.add(ch.status||JSON.stringify(ch));row.resolvedName=ch.name;}row.resources=s.resources;
    fresh=(await raw({command:'chat.messages'})).messages.filter(m=>!before.has(m.id));
    if(fresh.length>=10&&Date.now()>end-(item.watchSeconds??60)*500)break;
    await delay(1000);
  }
  row.statuses=[...states];
  row.messages=fresh.length;
  row.sample=fresh.slice(-2).map(m=>({type:m.type,name:m.chatname?'(present)':'',hasText:!!m.chatmessage}));
  await delay(300);
  await screenshot(target,{target:'window'},path.join(out,row.detected.platform+'-'+results.length+'.png')).catch(()=>{});
  await raw({command:'chat.channel.remove',id:added.channel.id}).catch(e=>row.removeError=e.message);
  console.log(JSON.stringify(row));results.push(row);
}
try{await request(target,'/commands',{command:'quit'});}catch{}
await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
await fs.writeFile(path.join(out,'results.json'),JSON.stringify(results,null,2));console.log('dir',out);
