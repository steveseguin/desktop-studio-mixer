#!/usr/bin/env node
import { readFile } from 'node:fs/promises';
import { discovery, defaultDiscovery, request, launch, screenshot } from './studio-client.mjs';

const args=process.argv.slice(2);
function option(name) {
  const index=args.indexOf('--'+name);
  if(index<0) return undefined;
  if(index+1>=args.length || args[index+1].startsWith('--')) throw new Error(`--${name} requires a value.`);
  const value=args[index+1]; args.splice(index,2); return value;
}
function flag(name) { const index=args.indexOf('--'+name); if(index<0)return false;args.splice(index,1);return true; }
async function main() {
  const filename=option('discovery') || defaultDiscovery;
  const command=args.shift() || 'help';
  if(command==='help' || command==='--help') return {usage:[
    'studio-cli.mjs launch [--headless | --gui] [--executable path] [--show path.mixer.json] [--discovery new-path]',
    'studio-cli.mjs [--discovery path] status | schema | sources cameras|windows|spout|audio-inputs',
    'studio-cli.mjs [--discovery path] command <JSON> OR command --file request.json OR command --stdin',
    'studio-cli.mjs [--discovery path] screenshot program|preview|window --out new-file.png [--width 1280]',
    'studio-cli.mjs [--discovery path] quit'
  ], notes:'Node.js 22+. Launch defaults to windowless. The app keeps running after this CLI exits. Commands are JSON and errors exit nonzero. Keep discovery files private.'};
  if(command==='launch') {
    const gui=flag('gui'), hidden=flag('headless');
    if(gui && hidden)throw new Error('Choose --gui or --headless.');
    const options={executable:option('executable'),show:option('show'),headless:!gui};
    // Only use the shared discovery filename if it was explicitly requested.
    if(process.argv.includes('--discovery'))options.discoveryPath=filename;
    if(args.length)throw new Error('Unexpected launch arguments.');
    return (await launch(options)).result;
  }
  const target=await discovery(filename);
  if(command==='status' || command==='schema') {
    if(args.length)throw new Error('Unexpected arguments.');
    return request(target,command==='status'?'/studio/state':'/studio/schema');
  }
  if(command==='sources') {
    if(args.length!==1 || !['cameras','windows','spout','audio-inputs'].includes(args[0]))throw new Error('Choose cameras, windows, spout, or audio-inputs.');
    return request(target,'/sources/'+args[0]);
  }
  if(command==='command') {
    const file=option('file'), stdin=flag('stdin');
    if((file?1:0)+(stdin?1:0)+args.length!==1)throw new Error('Supply exactly one JSON string, --file, or --stdin.');
    let json;
    if(file)json=await readFile(file,'utf8');
    else if(stdin){let text='';for await(const chunk of process.stdin){text+=chunk;if(text.length>65536)throw new Error('Input too large.');}json=text;}
    else json=args[0];
    if(json.length>65536)throw new Error('Input too large.');
    return request(target,'/studio/command',JSON.parse(json.replace(/^\uFEFF/,'')));
  }
  if(command==='screenshot') {
    const out=option('out'), width=option('width');
    if(!out || args.length!==1)throw new Error('Supply program, preview, or window and --out new-file.png.');
    return screenshot(target,{target:args[0],...(width?{max_width:Number(width)}:{})},out);
  }
  if(command==='quit' && !args.length)return request(target,'/commands',{command:'quit'});
  throw new Error('Unknown command. Use help.');
}
try { console.log(JSON.stringify(await main(),null,2)); }
catch(error){console.error(JSON.stringify({error:error.message}));process.exitCode=1;}
