// Run only against a private QA installation; temporarily removes/corrupts a pack
// file, restores its exact bytes, and verifies video still records when chat fails.
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';
import {discovery,request,screenshot} from './mcp/studio-client.mjs';
const [exe,out]=process.argv.slice(2), root=path.resolve('e2e/reports');
if(!path.resolve(exe).startsWith(root+path.sep))throw Error('Requires a private QA installation');
await fs.mkdir(out,{recursive:true});
const file=path.join(path.dirname(exe),'ssn/background.html'),bytes=await fs.readFile(file),results=[];
for(const mode of ['missing','tampered']) {
 let child,target;
 try {
  if(mode==='missing')await fs.unlink(file);else await fs.writeFile(file,Buffer.concat([bytes,Buffer.from('\n<!-- QA tamper -->')]));
  const data=path.join(out,mode);await fs.mkdir(data,{recursive:true});const control=path.join(data,'control.json');
  child=spawn(exe,['--studio-headless',`--studio-show=${path.join(data,'show.mixer.json')}`,'--local-control-port=0',`--local-control-discovery=${control}`],{env:{...process.env,VIDEO_MIXER_DATA_DIR:data},windowsHide:true,stdio:'ignore'});
  for(let i=0;i<100&&!target;i++){try{target=await discovery(control);}catch{await delay(200);}}
  if(!target)throw Error('Startup failed');
  const cmd=(command,args={})=>request(target,'/studio/command',{command,...args});
  const chat=await cmd('chat.status');if(chat.available)throw Error(mode+' pack incorrectly available');
  const clip=path.join(data,'recording.mkv');await cmd('record.start',{path:clip});await delay(1800);
  const state=await request(target,'/studio/state');if(!state.recording.active)throw Error('Recording failed');
  await screenshot(target,{target:'window'},path.join(data,'unavailable.png'));await cmd('record.stop');await delay(1000);
  const decoded=await promisify(execFile)(path.join(path.dirname(exe),'ffmpeg/bin/ffmpeg.exe'),['-v','error','-i',clip,'-f','null','-'],{windowsHide:true,timeout:30000});
  if(decoded.stderr.trim())throw Error(decoded.stderr);
  results.push({mode,chatUnavailable:true,recordingDecoded:true});
 } finally {
  if(target)try{await request(target,'/commands',{command:'quit'});}catch{}
  if(child)for(let i=0;i<50&&child.exitCode===null;i++)await delay(100);
  if(child?.exitCode===null)child.kill();
  await fs.writeFile(file,bytes);
 }
}
await fs.writeFile(path.join(out,'results.json'),JSON.stringify(results,null,2));console.log(JSON.stringify(results));
