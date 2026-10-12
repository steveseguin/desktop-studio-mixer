// Real two-client MCP, disconnect, stale discovery, and native lifecycle cases.
import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StdioClientTransport} from '@modelcontextprotocol/sdk/client/stdio.js';
import {mkdir,readFile,writeFile,stat} from 'node:fs/promises';
import {setTimeout as delay} from 'node:timers/promises';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import assert from 'node:assert/strict';
import {launch,discovery,request} from './studio-client.mjs';
const [packageArg,reportArg]=process.argv.slice(2),pkg=path.resolve(packageArg),report=path.resolve(reportArg);
await mkdir(report,{recursive:true});const result={};let active,clients=[];
async function until(fn,label,timeout=30000){const end=Date.now()+timeout;while(Date.now()<end){if(await fn())return;await delay(100);}throw Error(label);}
async function exited(pid){try{process.kill(pid,0);return false;}catch{return true;}}
async function start(name){active=await launch({executable:path.join(pkg,'desktop-studio.exe'),show:path.join(report,name+'.mixer.json')});return active.target;}
async function stop(){if(!active)return;const pid=active.target.pid;await request(active.target,'/commands',{command:'quit'});await until(()=>exited(pid),'App did not quit');active=null;}
async function connect(target){const c=new Client({name:'concurrency-test',version:'1'});await c.connect(new StdioClientTransport({command:process.execPath,args:[path.join(pkg,'tools/mcp/studio-mcp.mjs')],stderr:'pipe'}));clients.push(c);await call(c,'attach',{discovery:target.filename});return c;}
async function call(c,name,args={}){const r=await c.callTool({name:'desktop_studio_'+name,arguments:args},undefined,{timeout:45000});assert(!r.isError,JSON.stringify(r.content));return r.structuredContent||r;}
const command=(target,command,args={})=>request(target,'/studio/command',{command,...args});
try{
 const target=await start('concurrent'),a=await connect(target),b=await connect(target);
 const baseline=await request(target,'/studio/state');const names=Array.from({length:32},(_,n)=>'Concurrent '+n);
 const started=Date.now();const added=await Promise.all(names.map((name,n)=>call(n%2?a:b,'scene_add',{name})));
 let state=await request(target,'/studio/state');assert.equal(state.scenes.length,baseline.scenes.length+names.length);
 assert.equal(new Set(added.map(s=>s.id)).size,names.length);for(const name of names)assert(state.scenes.some(s=>s.name===name));
 result.concurrentScenes={count:names.length,milliseconds:Date.now()-started};
 await Promise.all(added.map((s,n)=>call(n%2?a:b,'scene_rename',{id:s.id,name:'Renamed '+n})));
 await Promise.all(added.map((s,n)=>call(n%2?a:b,'scene_remove',{id:s.id})));
 assert.equal((await request(target,'/studio/state')).scenes.length,baseline.scenes.length);
 const rec=path.join(report,'disconnect-recording.mkv');await call(a,'record_start',{path:rec});await until(async()=> (await request(target,'/studio/state')).recording.active,'Recording failed');
 await delay(2000);await a.close();await b.close();clients=[];const before=(await stat(rec)).size;await delay(3000);
 assert((await request(target,'/studio/state')).recording.active);assert((await stat(rec)).size>before);result.disconnectPreservesRecording=true;
 const data=JSON.parse(await readFile(target.filename,'utf8'));
 const stale=path.join(report,'stale-control.json');await writeFile(stale,JSON.stringify({...data,pid:data.pid+1}));
 await assert.rejects(()=>discovery(stale).then(t=>request(t,'/studio/state')),/stale|different app/);
 const bad=path.join(report,'invalid-control.json');await writeFile(bad,JSON.stringify({...data,base_url:'https://example.invalid'}));
 await assert.rejects(()=>discovery(bad),/Invalid loopback/);result.staleAndNonLoopbackRejected=true;
 await assert.rejects(()=>launch({executable:path.join(pkg,'desktop-studio.exe'),show:path.join(report,'concurrent.mixer.json')}),/exited/);
 assert((await request(target,'/studio/state')).recording.active);result.duplicateLaunchPreservesRecording=true;
 await command(target,'record.stop');await until(async()=>!(await request(target,'/studio/state')).recording.busy,'Record finalization failed');
 const decoded=spawnSync(path.join(pkg,'ffmpeg/bin/ffmpeg.exe'),['-v','error','-i',rec,'-fps_mode','passthrough','-enc_time_base','1:1000000','-f','null','-'],{encoding:'utf8',windowsHide:true,timeout:30000});assert.equal(decoded.status,0);assert.equal(decoded.stderr,'');result.recordingDecoded=true;
 await stop();result.repeatedLaunches=[];
 for(let n=0;n<5;n++) {const t=await start('launch-'+n);await request(t,'/studio/state');const pid=t.pid;await stop();result.repeatedLaunches.push({pid,exited:await exited(pid)});}
 result.quitDuringStart=[];
 for(let n=0;n<5;n++){
  const t=await start('pending-'+n);await command(t,'output.configure',{protocol:'file',url:path.join(report,'pending-'+n+'.mkv'),encoder:'nvenc'});
  await command(t,'output.start');const begin=Date.now();const pid=t.pid;await stop();result.quitDuringStart.push({pid,quitMs:Date.now()-begin,exited:await exited(pid)});
 }
 result.passed=true;
}finally{
 for(const c of clients)try{await c.close();}catch{}
 try{await stop();}catch(error){result.cleanupError=error.message;}
 await writeFile(path.join(report,'concurrency.json'),JSON.stringify(result,null,2));
}
console.log(JSON.stringify(result,null,2));
