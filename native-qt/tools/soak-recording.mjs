// Optional recording evidence for chat-soak.mjs. Uses only the selected package.
import fs from 'node:fs/promises';
import {existsSync} from 'node:fs';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';
import {request, screenshot} from './mcp/studio-client.mjs';

const exec = promisify(execFile);
const GiB = 1024 ** 3;
async function freeBytes(folder) {
  const disk = await fs.statfs(folder);
  return disk.bavail * disk.bsize;
}

export async function prepareRecording(exe, out, minutes) {
  // 720p30 records at 12 Mbps, plus audio, container overhead and headroom.
  const required = GiB + minutes * 60 * 12500000 / 8 * 1.1;
  const free = await freeBytes(out);
  if (free < required) throw Error(`Recording needs ${(required/GiB).toFixed(1)} GiB free; only ${(free/GiB).toFixed(1)} GiB available`);
  const ffmpeg = ['ffmpeg.exe','ffmpeg/bin/ffmpeg.exe','bin/ffmpeg.exe','ffmpeg/ffmpeg.exe']
    .map(p => path.join(path.dirname(exe),p)).find(existsSync);
  if (!ffmpeg) throw Error('Packaged FFmpeg is missing');
  const fixture = path.join(out,'moving-source.mp4');
  await exec(ffmpeg,['-hide_banner','-nostdin','-v','error','-f','lavfi','-i','testsrc2=size=1280x720:rate=30',
    '-t','7','-an','-c:v','mpeg4','-q:v','5',fixture],{windowsHide:true,timeout:60000});
  return {ffmpeg,fixture,file:path.join(out,'soak-recording.mkv'),out,requiredBytes:required,freeBytesAtStart:free};
}

export async function startRecording(target, evidence) {
  const command = c => request(target,'/studio/command',c);
  await command({command:'canvas.set',format:0});
  await command({command:'scene.add',name:'Recording soak'});
  const source = await command({command:'source.add',kind:'media',name:'Moving recording reference',locator:evidence.fixture});
  await command({command:'layer.set',source:source.id});
  await command({command:'media.set',id:source.id,loop:true});
  await command({command:'scene.take'});
  await delay(1500);
  await screenshot(target,{target:'program'},path.join(evidence.out,'recording-start.png'));
  await command({command:'record.start',path:evidence.file});
  evidence.startedAt = Date.now();
  for(let n=0;n<100;n++) {
    if ((await request(target,'/studio/state')).recording.active &&
        await fs.stat(evidence.file).then(s=>s.size>0,()=>false)) return;
    await delay(100);
  }
  throw Error('Recording did not become active');
}

export async function checkRecording(target, evidence) {
  if (!(await request(target,'/studio/state')).recording.active) throw Error('Recording stopped during the soak');
  const free = await freeBytes(evidence.out);
  if (free < 512*1024**2) throw Error('Stopping soak: less than 512 MiB disk space remains');
  return {freeBytes:free,bytes:(await fs.stat(evidence.file)).size};
}

export async function finishRecording(target, evidence) {
  await screenshot(target,{target:'program'},path.join(evidence.out,'recording-end.png'));
  await request(target,'/studio/command',{command:'record.stop'});
  evidence.elapsedSeconds = (Date.now()-evidence.startedAt)/1000;
  for(let n=0;n<300;n++) {
    const state = (await request(target,'/studio/state')).recording;
    if (!state.active && !state.busy) break;
    if (n===299) throw Error('Recording did not finalize');
    await delay(100);
  }
  // Decode the entire file. One hash per second also detects sustained frozen
  // output; fps duplicates frames across timestamp gaps instead of hiding them.
  const {stdout,stderr} = await exec(evidence.ffmpeg,['-hide_banner','-nostdin','-v','error','-xerror',
    '-i',evidence.file,'-map','0:v:0','-vf','fps=1,scale=96:54','-an','-f','framemd5','-'],
    {windowsHide:true,timeout:Math.max(120000,evidence.elapsedSeconds*1500),maxBuffer:8*1024**2});
  await fs.writeFile(path.join(evidence.out,'recording-frames.md5'),stdout);
  await fs.writeFile(path.join(evidence.out,'recording-decode.log'),stderr);
  const hashes = stdout.split(/\r?\n/).filter(l=>l && !l.startsWith('#')).map(l=>l.split(',').at(-1).trim());
  let frozen=0,longestFrozen=0,previous;
  for (const hash of hashes) { frozen=hash===previous?frozen+1:1;longestFrozen=Math.max(longestFrozen,frozen);previous=hash; }
  const summary = {elapsedSeconds:evidence.elapsedSeconds,decodedSeconds:hashes.length,uniqueFrames:new Set(hashes).size,
    longestUnchangedSeconds:longestFrozen,bytes:(await fs.stat(evidence.file)).size};
  const failures=[];
  if (hashes.length < evidence.elapsedSeconds*.98 || hashes.length > evidence.elapsedSeconds*1.02+1) failures.push('Recording duration differs from wall clock');
  if (summary.uniqueFrames<4 || longestFrozen>3) failures.push('Recorded moving picture froze for more than three seconds');
  await fs.writeFile(path.join(evidence.out,'recording.json'),JSON.stringify({summary,failures},null,2));
  if(failures.length) throw Error(failures.join('; '));
  return summary;
}
