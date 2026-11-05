import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request,screenshot} from './mcp/studio-client.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/workflows',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const filename=path.join(out,'control.json'),show=path.join(out,'show.mixer.json');
const result={executable:exe,data:out,observations:[],completed:false};
let target,child;
async function until(fn,description,ms=15000){const start=Date.now();while(Date.now()-start<ms){const value=await fn();if(value)return value;await delay(150);}throw Error('Timed out: '+description);}
function assert(value,description){if(!value)throw Error(description);result.observations.push(description);}
async function command(command,args={}){const value=await request(target,'/studio/command',{command,...args});if(value.error)throw Error(value.error);return value;}
async function status(){return command('chat.status');}
try {
  child=spawn(exe,['--studio-headless',`--studio-show=${show}`,'--local-control-port=0',`--local-control-discovery=${filename}`],
    {env:{...process.env,VIDEO_MIXER_DATA_DIR:out},windowsHide:true,stdio:'ignore'});
  let launchError;child.once('error',e=>{launchError=e;});
  await until(async()=>{if(launchError)throw launchError;if(child.exitCode!==null)throw Error('App exited: '+child.exitCode);try{target=await discovery(filename);if(target.pid!==child.pid)return false;await request(target,'/studio/state');return true;}catch{return false;}},'packaged application',20000);
  const initial=await status();assert(initial.available,'Pack is available in the portable app');assert(!initial.ready,'Engine is idle before channels or practice');
  for(const input of ['', 'hello world!', 'https://twitch.tv/directory', 'https://127.0.0.1/chat', 'file:///C:/temp/chat']){let rejected=false;try{await command('chat.channel.add',{input});}catch(e){rejected=!e.message.includes('Invalid field: input');}assert(rejected,'Input has a friendly rejection: '+JSON.stringify(input));}
  await command('chat.select');await command('chat.select',{id:''});assert(!(await status()).selected,'Empty selection is accepted');
  const channel=await command('chat.channel.add',{input:'https://www.twitch.tv/neonorbit'});
  assert(channel.channel.platform==='Twitch','Public channel detection uses Twitch');
  await command('chat.channel.remove',{id:channel.channel.id});
  assert((await status()).channels.length===0,'Removing a channel updates saved app state');
  await command('show.save',{path:path.join(out,'native-show.mixer.json')});
  const nativeShow=JSON.parse(await fs.readFile(path.join(out,'native-show.mixer.json'),'utf8'));
  assert(nativeShow.comments.style==='bar'&&!nativeShow.channels,'Native show save stores Comments independently of channels');
  await command('show.open',{path:path.join(out,'native-show.mixer.json')});
  assert((await status()).layer.style==='bar','Native show load restores Comments defaults');
  await screenshot(target,{target:'window'},path.join(out,'00-chat-panel.png'));
  await command('chat.practice.set',{enabled:true});
  let warned=false;try{await command('record.start',{path:path.join(out,'should-not-record.mkv')});}catch(e){warned=e.message.includes('Pretend comments');}
  assert(warned,'Recording with practice requires explicit acknowledgement');
  try {
    await until(async()=>{const s=await status();result.lastState=s;return s.ready;},'real WebView2 engine readiness',25000);
  } catch(readinessError) {
    await screenshot(target,{target:'window'},path.join(out,'chat-unavailable.png'));
    // Preserve the failure. Still exercise real Program/recording while chat fails.
    try {
      const clip=path.join(out,'recording-with-chat-unavailable.mkv');
      await command('record.start',{path:clip,acknowledgePractice:true});await delay(5000);await command('record.stop');
      await until(async()=>!(await request(target,'/studio/state')).recording.busy,'recording finalization');
      await promisify(execFile)(path.join(path.dirname(exe),'ffmpeg/bin/ffmpeg.exe'),['-v','error','-i',clip,'-frames:v','1',path.join(out,'recorded-frame.png')],{windowsHide:true,timeout:15000});
      assert((await fs.stat(path.join(out,'recorded-frame.png'))).size>0,'Actual recorded video decodes while chat cannot start');
      await command('production.set',{privateBreak:true});await screenshot(target,{target:'program'},path.join(out,'private-break-with-chat-unavailable.png'));
      assert((await request(target,'/studio/state')).private_break,'Private break remains usable while chat cannot start');
    } catch(error){result.isolationObservationError=error.stack;}
    throw readinessError;
  }
  await command('chat.inject',{message:{chatname:'Workflow Viewer',chatmessage:'<b>This must stay plain text</b> & hello'}});
  const comment=await until(async()=>{const m=await command('chat.messages');return m.messages.find(m=>m.chatname==='Workflow Viewer');},'processed engine capture');
  assert(comment.chatmessage==='<b>This must stay plain text</b> & hello','Literal practice message survives the real engine');
  await command('chat.select',{id:comment.id});assert(!(await status()).on_screen,'Selecting only previews privately');
  await command('chat.select',{id:''});assert(!(await status()).selected&&(await status()).pending_comments===0,'Deselect releases buffered comments');await command('chat.select',{id:comment.id});
  const before=await screenshot(target,{target:'program'},path.join(out,'01-before.png'));
  await command('chat.show',{style:'bubble'});
  await until(async()=> (await status()).on_screen,'comment rendered and composed');
  result.showStages=(await status()).timing;result.chatResources=(await status()).resources;
  await command('chat.keep');await screenshot(target,{target:'program'},path.join(out,'02-comment.png'));
  assert(!(await fs.readFile(before.path)).equals(await fs.readFile(path.join(out,'02-comment.png'))),'Program pixels change when the browser comment is composed');
  // Fifty distinct comments per second through the real practice page, engine,
  // host feed, worker IPC and list. Keep a real overlay on Program throughout.
  const burstStart=Date.now(),sent=[];let worstControlMs=0;
  for(let i=0;i<500;i++){
    await delay(Math.max(0,burstStart+i*20-Date.now()));
    const at=Date.now();sent.push(command('chat.inject',{message:{chatname:'Burst viewer',chatmessage:'Burst comment '+i}}).then(()=>{worstControlMs=Math.max(worstControlMs,Date.now()-at);}));
  }
  await Promise.all(sent);
  await until(async()=> (await command('chat.messages')).messages.filter(m=>m.chatname==='Burst viewer').length===500,'all 500 burst comments arrive',20000);
  result.burst={messages:500,scheduledPerSecond:50,elapsedMs:Date.now()-burstStart,worstControlMs,resources:(await status()).resources};
  assert((await status()).on_screen,'Program still composes the comment after the 50/s burst');
  assert(worstControlMs<500,'Control stays responsive during the 50/s burst');
  await screenshot(target,{target:'program'},path.join(out,'02b-after-burst.png'));
  const scene=await command('scene.add',{name:'Chat scene change',background:'#344556'});await command('scene.take');
  assert((await status()).on_screen,'Comments survive scene change and Take');
  await command('production.set',{privateBreak:true});
  await until(async()=> (await status()).hidden_by_private_break,'Private break');
  assert(!(await status()).on_screen,'Private break covers Comments');await delay(300);await screenshot(target,{target:'program'},path.join(out,'03-private-break.png'));
  await command('production.set',{privateBreak:false});await until(async()=> (await status()).on_screen,'resume Comments');
  await command('chat.keep',{enabled:false});await until(async()=> !(await status()).on_screen,'ten second auto-hide',13000);
  assert(!(await status()).on_screen,'Timer removes Comments from composition');
  await command('chat.select',{id:comment.id});
  for(let i=0;i<60;i++){await command('chat.inject',{message:{chatname:'Rate gate',chatmessage:'Unique incoming comment '+i}});await delay(35);}
  const paused=await status();assert(paused.selected===comment.id&&paused.pending_comments>0,'Incoming rows buffer while selection stays stable');
  await command('chat.show',{style:'big'});await until(async()=> (await status()).on_screen,'big comment');await command('chat.hide');
  await command('show.save',{path:path.join(out,'saved.mixer.json')});
  const saved=JSON.parse(await fs.readFile(path.join(out,'saved.mixer.json'),'utf8'));assert(saved.comments.style==='big','Show saves Comments style');assert(!saved.channels,'Channels stay outside show files');
  await command('chat.practice.set',{enabled:false});
  result.completed=true;
} catch(error){result.error=error.stack;process.exitCode=1;}
finally {
  if(target){try{result.lastState=await status();await request(target,'/commands',{command:'quit'});}catch{}}
  if(child&&child.exitCode===null){await Promise.race([new Promise(r=>child.once('exit',r)),delay(4000)]);if(child.exitCode===null)child.kill();}
  await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}
