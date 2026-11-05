import fs from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';

// Static evidence only: none of these lists imply a call or network request occurred.
export async function auditSource(root){
  const loader=await fs.readFile(path.join(root,'loader.js'),'utf8');
  const modules=[...loader.matchAll(/['"]\.\/([^'"]+\.js)(?:\?[^'"]*)?['"]/g)].map(m=>m[1]);
  const top=await fs.readdir(root,{withFileTypes:true});
  const files=[...new Set(['manifest.json','service_worker.js','background.html','loader.js','background.js','shared/overlay-control-transport.js','sources/twitch.js',...modules,...top.filter(x=>x.isFile()&&/\.(html|js)$/.test(x.name)).map(x=>x.name)])];
  const result={manifest:JSON.parse(await fs.readFile(path.join(root,'manifest.json'),'utf8')),loaderModules:modules,legacyBridgeDeclarations:[],originChecks:[],chromeReferences:[],optionalDesktopReferences:[],hashes:[]};
  const apis=new Set(),desktop=new Set();
  for(const file of files){
    const bytes=await fs.readFile(path.join(root,file));const text=bytes.toString('utf8');
    result.hashes.push({file,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});
    text.split(/\r?\n/).forEach((line,i)=>{
      if(/legacyBridgeRequired\s*:\s*true/.test(line))result.legacyBridgeDeclarations.push({file,line:i+1,text:line.trim()});
      if(/location\.(host|hostname|origin|protocol)/.test(line)&&/socialstream|chrome-extension|file:/.test(line))result.originChecks.push({file,line:i+1,text:line.trim()});
      if(file==='background.js'||modules.includes(file)){
        for(const m of line.matchAll(/\bchrome(?:\.[A-Za-z_$][\w$]*)+/g))apis.add(m[0]);
        for(const m of line.matchAll(/\b(?:ninjafy|electronApi)(?:\??\.[A-Za-z_$][\w$]*)+/g))desktop.add(m[0]);
      }
    });
  }
  result.chromeReferences=[...apis].sort();result.optionalDesktopReferences=[...desktop].sort();return result;
}
