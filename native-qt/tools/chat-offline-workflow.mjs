// Browser-independent workflows in the shipped application. No fake engine or feed.
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request,screenshot} from './mcp/studio-client.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const out=path.join(root,'native-qt/build-chat/offline-workflow',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const children=[],checks=[];
let first,second;
function check(ok,name){checks.push({ok:!!ok,name});console.log(`${ok?'PASS':'FAIL'} ${name}`);if(!ok)throw Error(name);}
async function launch(name){
  const control=path.join(out,name+'.json');
  const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,name+'.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:out},windowsHide:true,stdio:'ignore'});
  const app={child};children.push(app);
  for(let i=0;i<100;i++){try{app.target=await discovery(control);if(app.target.pid!==child.pid)throw Error('Wrong process');return app;}catch{await delay(200);}}
  throw Error('Application startup timeout');
}
const command=(app,command,args={})=>request(app.target,'/studio/command',{command,...args});
async function quit(app){if(app.child.exitCode!==null)return;try{await request(app.target,'/commands',{command:'quit'});}catch{}await Promise.race([new Promise(r=>app.child.once('exit',r)),delay(7000)]);if(app.child.exitCode===null)app.child.kill();}
try{
  first=await launch('first');
  const initial=await command(first,'chat.status');check(initial.available&&!initial.ready,'Verified pack is available and browser stays idle before setup');
  for(const input of ['', 'hello world!', 'https://twitch.tv/directory', 'https://kick.com/categories', 'file:///C:/temp/chat','javascript:alert(1)','https://127.0.0.1/chat','https://[::1]/chat','https://user:pass@twitch.tv/name']){
    let error='';try{const r=await command(first,'chat.channel.add',{input});error=r.error||'';}catch(e){error=e.message;}
    check(error&&!error.includes('Invalid field: input'),'Friendly rejection: '+JSON.stringify(input));
  }
  await command(first,'chat.select');await command(first,'chat.select',{id:''});
  check(!(await command(first,'chat.status')).selected,'Missing and empty selection IDs deselect');
  for(const theme of ['original','graphite','rose','neon','sand']){
    await command(first,'preferences.set',{theme});await delay(500);
    await screenshot(first.target,{target:'window',max_width:1920},path.join(out,theme+'-setup.png'));
  }
  await command(first,'preferences.set',{theme:'original'});
  const added=await command(first,'chat.channel.add',{input:'neonorbit'});check(added.channel?.platform==='Twitch','Channel writes to the isolated settings folder');
  const saved=await fs.readFile(path.join(out,'chat.json'),'utf8');
  second=await launch('second');
  const state=await command(second,'chat.status');check(/another Studio window/.test(state.problem)&&!state.ready&&!state.browser_pid,'Second instance degrades before opening a shared WebView2 profile');
  let refused=false;try{const r=await command(second,'chat.channel.add',{input:'anotherchannel'});refused=!!r.error;}catch{refused=true;}
  check(refused&&(await fs.readFile(path.join(out,'chat.json'),'utf8'))===saved,'Second instance cannot overwrite channels');
  await screenshot(second.target,{target:'window',max_width:1920},path.join(out,'second-instance.png'));
  await command(first,'chat.channel.remove',{id:added.channel.id});
  check((await command(first,'chat.status')).channels.length===0,'Owner still edits channels');
  await quit(second);
  for(const language of ['es','fr']){
    await command(first,'preferences.set',{language});await quit(first);first=await launch('language-'+language);
    const state=await request(first.target,'/studio/state');check(state.preferences?.language===language,'Language applies after restart: '+language);
    await delay(500);await screenshot(first.target,{target:'window',max_width:1920},path.join(out,language+'-setup.png'));
  }
}catch(e){checks.push({ok:false,name:e.stack});process.exitCode=1;}
finally{
  for(const app of children.reverse())await quit(app);
  await fs.writeFile(path.join(out,'results.json'),JSON.stringify({exe,checks},null,2));console.log(out);
}
