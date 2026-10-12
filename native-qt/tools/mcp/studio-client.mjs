import { open, access, mkdir, mkdtemp, writeFile } from 'node:fs/promises';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import path from 'node:path';

export const defaultDiscovery = process.env.DESKTOP_STUDIO_DISCOVERY ||
  path.join(process.env.LOCALAPPDATA || process.cwd(), 'VideoMixer', 'control.json');
export async function discovery(filename = defaultDiscovery) {
  const file = await open(path.resolve(filename), 'r');
  let data;
  try {
    const stat = await file.stat();
    if (!stat.isFile() || stat.size > 16384) throw new Error('Invalid discovery file.');
    const buffer = Buffer.alloc(16385);
    let size = 0;
    while (size < buffer.length) {
      const { bytesRead } = await file.read(buffer, size, buffer.length-size, null);
      if (!bytesRead) break;
      size += bytesRead;
    }
    if (size > 16384) throw new Error('Discovery file exceeds 16 KiB.');
    data = JSON.parse(buffer.subarray(0,size).toString('utf8').replace(/^\uFEFF/, ''));
  } finally { await file.close(); }
  const url = new URL(data.base_url);
  if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || !url.port || url.pathname !== '/' ||
      url.username || url.password || url.search || url.hash || !Number.isSafeInteger(data.pid) || data.pid < 1 ||
      typeof data.token !== 'string' || !data.token || data.token.length > 1024) throw new Error('Invalid loopback discovery.');
  return { base: url.origin, token: data.token, pid: data.pid, filename: path.resolve(filename) };
}
export async function health(target) {
  const response = await fetch(target.base+'/health', {redirect:'error', signal:AbortSignal.timeout(4000)});
  const body = await response.json();
  if (!response.ok || body.pid !== target.pid || body.schema !== 'video-mixer-local-control-v1')
    throw new Error('Discovery is stale or belongs to a different app instance.');
  return body;
}
export async function request(target, route, body) {
  await health(target);
  const response = await fetch(target.base+route, {
    method: body === undefined ? 'GET' : 'POST', redirect:'error', signal:AbortSignal.timeout(15000),
    headers: {Authorization:`Bearer ${target.token}`, 'Content-Type':'application/json'},
    body: body === undefined ? undefined : JSON.stringify(body)
  });
  const result = await response.json();
  if (!response.ok) throw new Error(`Desktop Studio ${response.status}: ${result.error || 'Request failed'}`);
  return result;
}
export async function executablePath(explicit) {
  if (explicit || process.env.DESKTOP_STUDIO_EXECUTABLE) return path.resolve(explicit || process.env.DESKTOP_STUDIO_EXECUTABLE);
  // Works directly from both the portable package and the source checkout.
  for (const relative of ['../../desktop-studio.exe','../../dist/desktop-studio-portable/desktop-studio.exe']) {
    const candidate = fileURLToPath(new URL(relative, import.meta.url));
    try { await access(candidate); return candidate; } catch { /* next candidate */ }
  }
  if (process.platform === 'win32') {
    try {
      const {stdout} = await promisify(execFile)('powershell.exe', ['-NoProfile','-NonInteractive','-Command',
        "[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new(); [Microsoft.Win32.Registry]::GetValue('HKEY_CURRENT_USER\\Software\\VideoMixer','InstallDir','')"],
        {windowsHide:true,timeout:10000,maxBuffer:16384});
      const root=stdout.replace(/^\uFEFF/,'').trim();
      if (root) return path.join(root,'desktop-studio.exe');
    } catch { /* default installation */ }
  }
  return path.join(process.env.LOCALAPPDATA || process.cwd(),'Programs','Desktop Studio','desktop-studio.exe');
}
export async function launch({executable, headless = true, show, discoveryPath} = {}) {
  const exe = await executablePath(executable);
  await access(exe);
  const root = path.join(process.env.LOCALAPPDATA || process.cwd(),'VideoMixer','automation');
  await mkdir(root,{recursive:true});
  const run = await mkdtemp(path.join(root,'session-'));
  const filename = path.resolve(discoveryPath || path.join(run,'control.json'));
  // Do not steal another instance's discovery file.
  try { await access(filename); throw new Error('Discovery path already exists. Choose a new path.'); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  const args=['--local-control-port=0',`--local-control-discovery=${filename}`];
  if (headless) args.push('--studio-headless');
  if (show) args.push(`--studio-show=${path.resolve(show)}`);
  const child=spawn(exe,args,{shell:false,detached:true,windowsHide:headless,stdio:'ignore'});
  let spawnError; child.on('error',error=>{spawnError=error;});
  try {
    for (let n=0;n<180;n++) {
      if (spawnError) throw spawnError;
      if (child.exitCode !== null) throw new Error(`Desktop Studio exited during launch (${child.exitCode}).`);
      try {
        const target=await discovery(filename);
        if (target.pid !== child.pid) throw new Error('Launch discovery PID mismatch.');
        await request(target,'/studio/state');
        child.unref();
        return {target, result:{pid:target.pid,discovery:filename,headless,show:show ? path.resolve(show) : 'current user show'}};
      } catch { await delay(200); }
    }
    throw new Error('Desktop Studio did not become ready within 36 seconds. Check its local logs.');
  } catch (error) { child.kill(); child.unref(); throw error; }
}
export async function screenshot(target, options, destination) {
  const image=await request(target,'/studio/screenshot',options);
  if (destination) {
    // Explicit filename; do not silently overwrite an existing artifact.
    await writeFile(path.resolve(destination),Buffer.from(image.data,'base64'),{flag:'wx'});
    return {path:path.resolve(destination),width:image.width,height:image.height,mime_type:image.mime_type};
  }
  return image;
}
