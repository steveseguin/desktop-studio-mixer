// Real packaged-app regression workflows. Only isolated pretend chat is injected;
// no messages are ever sent to a public platform. Also accepts an installed exe.
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn, execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';
import {createHash} from 'node:crypto';
import {discovery,request,screenshot as capture} from './mcp/studio-client.mjs';
const screenshot=async(...args)=>{await delay(230);return capture(...args);};

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const option=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const exe=path.resolve(option('exe',path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe')));
const out=path.resolve(option('out',path.join(root,'native-qt/build-chat/review-e2e',new Date().toISOString().replaceAll(/[:.]/g,'-'))));
await fs.mkdir(out,{recursive:true});
const result={exe,sha256:createHash('sha256').update(await fs.readFile(exe)).digest('hex'),out,checks:[],measurements:{}};
const instances=[];
const check=(ok,name,detail)=>{result.checks.push({ok:!!ok,name,detail});console.log(`${ok?'PASS':'FAIL'} ${name}${detail===undefined?'':' '+JSON.stringify(detail)}`);};
const until=async(fn,label,ms=30000)=>{const end=Date.now()+ms;while(Date.now()<end){const v=await fn();if(v)return v;await delay(40);}throw Error('Timed out: '+label);};
const launch=async(name,data=out)=>{
  const file=path.join(out,`${name}-control.json`);
  const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,name+'.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${file}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:data},windowsHide:true,stdio:'ignore'});
  const instance={child};instances.push(instance);let error;child.on('error',e=>error=e);
  instance.target=await until(async()=>{if(error)throw error;if(child.exitCode!==null)throw Error('App exited '+child.exitCode);try{const t=await discovery(file);return t.pid===child.pid?t:false;}catch{return false;}},name+' startup');
  instance.command=async(command,args={})=>{const r=await request(instance.target,'/studio/command',{command,...args});if(r.error)throw Error(r.error);return r;};
  instance.status=()=>instance.command('chat.status');return instance;
};
async function stop(instance){if(instance.child.exitCode!==null)return;try{await request(instance.target,'/commands',{command:'quit'});}catch{}
  await until(()=>instance.child.exitCode!==null,'clean exit',12000).catch(()=>instance.child.kill());}
const exec=promisify(execFile);
try {
  const app=await launch('first'),c=app.command;
  check((await app.status()).available,'Verified chat pack available');
  for(const input of ['javascript:alert(1)','file:///C:/Windows/win.ini','https://127.0.0.1/chat','https://www.twitch.tv/directory','https://www.twitch.tv/popout/directory/chat','hello world!','']) {
    let error;try{const r=await c('chat.channel.add',{input});if(r.channel)await c('chat.channel.remove',{id:r.channel.id});}catch(e){error=e.message;}
    check(!!error,'Reject invalid channel: '+JSON.stringify(input),error);
  }
  // Ownership is exercised through two independent real application instances.
  const other=await launch('second');
  check(/another Studio window/i.test((await other.status()).problem||''),'Second instance reports chat ownership instead of sharing the browser profile');
  let refused=false;try{await other.command('chat.practice.set',{enabled:true});}catch{refused=true;}
  check(refused,'Second instance cannot mutate owned chat settings');await stop(other);
  await c('chat.practice.set',{enabled:true});await until(async()=>(await app.status()).ready,'real chat engine',60000);
  await c('scene.add',{name:'Review background',background:'#243746'});await c('scene.take');await delay(250);
  const token='Review'+Date.now();
  await c('chat.inject',{message:{chatname:'Review Viewer',chatmessage:token+' <b>literal text</b> & hello'}});
  const chosen=await until(async()=>(await c('chat.messages')).messages.find(m=>m.chatmessage.includes(token)),'engine-delivered selection');
  const before=await screenshot(app.target,{target:'program'});
  await c('chat.select',{id:chosen.id});await delay(200);
  const selected=await screenshot(app.target,{target:'program'});
  check(before.data===selected.data&&!(await app.status()).on_screen,'Selecting a comment leaves actual Program pixels unchanged');
  await screenshot(app.target,{target:'window'},path.join(out,'01-selected.png'));
  let cleared=false;try{await c('chat.select',{id:''});cleared=!(await app.status()).selected;}catch{}
  check(cleared,'Empty selection clears the private tray and unfreezes comments');
  for(const style of ['bar','bubble','big']) {
    const start=performance.now();await c('chat.show',{id:chosen.id,style});
    await until(async()=>{const s=await app.status();return s.on_screen&&s.on_air?.id===chosen.id&&!s.pending_show;},style+' composed');
    const ms=Math.round(performance.now()-start);result.measurements[style+'ShowMs']=ms;
    check(ms<500,style+' reaches Program within 500 ms including command time',{ms});
    await c('chat.keep');await screenshot(app.target,{target:'program'},path.join(out,`02-${style}-program.png`));
    await screenshot(app.target,{target:'window'},path.join(out,`03-${style}-window.png`));await c('chat.hide');
  }
  // Fixed, independent fixture ledger; repeated author/text and punctuation are
  // intentional. Every numbered occurrence must survive exactly once.
  await c('chat.select',{id:''});
  const total=500,expected=Array.from({length:total},(_,i)=>`${token}-burst-${i.toString().padStart(4,'0')}`);
  const start=performance.now();let worst=0;
  for(let i=0;i<total;i++) {
    const t=performance.now();await c('chat.inject',{message:{chatname:'Burst viewer',chatmessage:expected[i]}});worst=Math.max(worst,performance.now()-t);
    await delay(Math.max(0,start+(i+1)*20-performance.now()));
  }
  await delay(2500);
  const delivered=(await c('chat.messages')).messages.filter(m=>m.chatmessage.startsWith(token+'-burst-'));
  const counts=new Map();for(const m of delivered)counts.set(m.chatmessage,(counts.get(m.chatmessage)||0)+1);
  const missing=expected.filter(x=>!counts.has(x)),duplicate=[...counts].filter(([,n])=>n>1),unexpected=[...counts.keys()].filter(x=>!expected.includes(x));
  result.measurements.burst={expected:total,received:delivered.length,missing,duplicate,unexpected,injectDurationMs:Math.round(performance.now()-start-2500),worstCommandMs:Math.round(worst)};
  check(!missing.length&&!duplicate.length&&!unexpected.length,'500 comments at 50/sec delivered exactly once',result.measurements.burst);
  check(worst<500,'Control remains responsive during the burst',{worstMs:Math.round(worst)});
  const recent=delivered.at(-1);await c('chat.show',{id:recent.id,style:'bar'});await until(async()=>(await app.status()).on_screen,'show before recording');await c('chat.keep');
  for(const theme of ['original','graphite','rose','neon','sand']) {await c('preferences.set',{theme});await delay(180);await screenshot(app.target,{target:'window'},path.join(out,`04-theme-${theme}.png`));}
  await c('preferences.set',{theme:'graphite'});
  const clip=path.join(out,'chat-recovery.mkv');await c('record.start',{path:clip,acknowledgePractice:true});await delay(2500);
  const beforeKill=await screenshot(app.target,{target:'program'}),state=await app.status();result.measurements.resources=state.resources;
  await exec('taskkill',['/PID',String(state.browser_pid),'/T','/F'],{windowsHide:true});const killed=Date.now();
  await until(async()=>{const s=await app.status();return s.ready&&s.browser_pid&&s.browser_pid!==state.browser_pid;},'chat recovery',60000);
  result.measurements.recoveryMs=Date.now()-killed;
  const afterKill=await screenshot(app.target,{target:'program'});
  check(beforeKill.data===afterKill.data,'Displayed comment remains stable through browser termination and recovery');
  check((await request(app.target,'/studio/state')).recording.active,'Recording remains active after chat recovery');
  await c('production.set',{privateBreak:true});await delay(250);check((await app.status()).hidden_by_private_break&&!(await app.status()).on_screen,'Private break hides comment');
  await screenshot(app.target,{target:'window'},path.join(out,'05-private-break.png'));
  await c('production.set',{privateBreak:false});await delay(2000);await c('record.stop');
  await until(async()=>!(await request(app.target,'/studio/state')).recording.busy,'recording finalized');
  const ff=path.join(path.dirname(exe),'ffmpeg/bin/ffmpeg.exe');
  const decoded=await exec(ff,['-v','error','-i',clip,'-f','null','-'],{windowsHide:true,timeout:60000,maxBuffer:1<<23});
  check(!decoded.stderr.trim(),'Recorded output fully decodes without errors',decoded.stderr);
  await exec(ff,['-v','error','-y','-ss','1','-i',clip,'-frames:v','1',path.join(out,'06-recorded-comment.png')],{windowsHide:true});
  await c('chat.hide');await c('chat.practice.set',{enabled:false});
  await stop(app);result.completed=true;
} catch(e) {result.error=e.stack;console.error(e.stack);process.exitCode=1;}
finally {
  for(const app of instances)await stop(app);
  if(result.checks.some(x=>!x.ok))process.exitCode=1;
  await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log(out);
}
