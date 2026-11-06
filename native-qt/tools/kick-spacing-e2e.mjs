// Local DOM fixtures in a real packaged capture page. Nothing is posted to Kick.
// Exercises full SSN capture -> engine -> Studio panel -> composed output.
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,health,request,screenshot} from './mcp/studio-client.mjs';
const flag=(name,fallback)=>{const i=process.argv.indexOf('--'+name);return i<0?fallback:process.argv[i+1];};
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const exe=process.env.VIDEO_MIXER_CHAT_EXE||path.join(root,'native-qt/dist/desktop-studio-portable/desktop-studio.exe');
const channel=flag('channel','zullyy_cs'),port=Number(flag('port',9385));
const out=path.join(root,'native-qt/build-chat/kick-spacing',new Date().toISOString().replaceAll(/[:.]/g,'-'));
await fs.mkdir(out,{recursive:true});
const sha=async p=>createHash('sha256').update(await fs.readFile(p)).digest('hex');
const result={exe,exeSha256:await sha(exe),sourceSha256:await sha(path.join(path.dirname(exe),'ssn/sources/kick.js')),checks:[]};
const check=(ok,name,detail)=>{result.checks.push({ok:!!ok,name,detail});console.log(ok?'PASS':'FAIL',name,JSON.stringify(detail??''));};
const control=path.join(out,'control.json');
const child=spawn(exe,['--studio-headless',`--studio-show=${path.join(out,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{windowsHide:true,stdio:'ignore',env:{...process.env,VIDEO_MIXER_DATA_DIR:out,WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS:`--remote-debugging-port=${port}`}});
let target,ws,seq=0;const pending=new Map();
const until=async(fn,label,ms=60000)=>{const end=Date.now()+ms;while(Date.now()<end){const value=await fn();if(value)return value;await delay(200);}throw Error('Timed out: '+label);};
const command=c=>request(target,'/studio/command',c);
const evaluate=expression=>new Promise((resolve,reject)=>{const id=++seq,timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timeout'));},10000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method:'Runtime.evaluate',params:{expression,returnByValue:true,awaitPromise:true}}));});
try {
 target=await until(async()=>{try{const t=await discovery(control);await health(t);return t;}catch{return null;}},'Studio');
 await command({command:'chat.channel.add',input:`https://kick.com/${channel}`});
 await until(async()=>(await command({command:'chat.status'})).ready,'chat engine ready');
 const page=await until(async()=>{try{return (await(await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(p=>p.type==='page'&&p.url.includes('kick.com'));}catch{return null;}},'capture page');
 ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;});
 ws.onmessage=e=>{const m=JSON.parse(e.data),p=pending.get(m.id);if(!p)return;clearTimeout(p.timer);pending.delete(m.id);if(m.error||m.result?.exceptionDetails)p.reject(Error(JSON.stringify(m.error||m.result.exceptionDetails)));else p.resolve(m.result?.result?.value);};
 await until(()=>evaluate("!!document.querySelector('#chatroom-messages [data-index]') && !!window.__engineSpike"),'message-list DOM');
 await delay(6000); // Let the source attach its normal observer before adding local rows.
 const token='SpacingQA'+Date.now(),emote=name=>`<span class="chat-emote-container"><img alt="${name}" src="https://files.kick.com/emotes/1/fullsize"></span>`;
 const fixtures=[
  {html:emote('One')+' '+emote('Two'),text:'One Two'},
  {html:emote('One')+emote('Two'),text:'OneTwo'},
  {html:emote('One')+'   '+emote('Two'),text:'One   Two'},
  {html:'<span>'+emote('One')+' '+emote('Two')+'</span>!',text:'One Two!'},
  {html:'literal &lt;b&gt; &amp; '+emote('One')+' '+emote('Two'),text:'literal <b> & One Two'},
  {html:emote('One')+' '+emote('Two')+' hello '+emote('Three')+' '+emote('Four'),text:'One Two hello Three Four'}
 ].map((f,i)=>({...f,expected:`${token}-${i} ${f.text}`}));
 // Repeat an identical author/text under a distinct row ID; both must survive.
 fixtures.push({...fixtures[0]});
 const inserted=await evaluate(`(()=>{
 const parent=document.querySelector('#chatroom-messages [data-index]')?.parentElement;
 if(!parent)return JSON.stringify({error:'No live message-list parent',html:document.querySelector('#chatroom-messages')?.outerHTML.slice(0,2000)});
 const rows=[];
 for(const [i,f] of ${JSON.stringify(fixtures)}.entries()){
  const row=document.createElement('div');row.dataset.index=String(900000+i);row.dataset.chatEntry=${JSON.stringify(token)}+'-'+i;
  row.innerHTML='<button title="Spacing Fixture">Spacing Fixture</button><span>:</span><span class="chat-entry-content font-normal">'+f.expected.slice(0,f.expected.indexOf(' ')+1)+f.html+'</span>';
  parent.appendChild(row);rows.push(row);
 }
 window.__spacingRows=rows;return JSON.stringify({inserted:rows.length});
})()`);
 result.fixtureInsertion=JSON.parse(inserted);if(result.fixtureInsertion.error)throw Error(result.fixtureInsertion.error);
 await delay(6000);
 const collect=async()=>(await command({command:'chat.messages'})).messages.filter(m=>m.chatmessage.includes(token));
 const rows=await collect();result.expected=fixtures.map(f=>f.expected);result.captured=rows;
 const expected=new Map();for(const f of fixtures)expected.set(f.expected,(expected.get(f.expected)||0)+1);
 const counts=new Map();for(const r of rows)counts.set(r.chatmessage,(counts.get(r.chatmessage)||0)+1);
 const missing=[...expected].filter(([s,n])=>(counts.get(s)||0)<n),unexpected=[...counts].filter(([s,n])=>n>(expected.get(s)||0));
 check(!missing.length&&!unexpected.length,'Exact spacing and occurrence counts',{expected:fixtures.length,received:rows.length,missing,unexpected});
 // Moving/pruning previously delivered rows must not send them again.
 await evaluate(`(()=>{for(const row of __spacingRows){const parent=row.parentElement;if(parent){row.remove();parent.appendChild(row);}}return true;})()`);
 await delay(2000);const after=await collect();check(after.length===rows.length,'Moving existing rows does not duplicate delivery',{before:rows.length,after:after.length});
 const chosen=rows.find(r=>r.chatmessage===fixtures[5].expected);
 if(chosen){
  await command({command:'chat.show',id:chosen.id,style:'bar'});
  await until(async()=>(await command({command:'chat.status'})).on_screen,'on-screen comment');
  await command({command:'chat.keep'});
  await screenshot(target,{target:'program'},path.join(out,'program.png'));
  // Program screenshot is saved for visual review; native composition acknowledges the exact text.
  const state=await command({command:'chat.status'});check(state.on_air?.chatmessage===chosen.chatmessage,'Spaced comment reaches composed output');
 }else check(false,'Spaced comment available for output');
}catch(e){check(false,'Workflow completed',e.stack);}
finally{
 ws?.close();for(const p of pending.values())clearTimeout(p.timer);
 try{if(target)await request(target,'/commands',{command:'quit'});}catch{}
 if(child.exitCode===null)await Promise.race([new Promise(r=>child.once('exit',r)),delay(8000)]);if(child.exitCode===null)child.kill();
 await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log('dir',out);
 if(result.checks.some(c=>!c.ok))process.exitCode=1;
}
