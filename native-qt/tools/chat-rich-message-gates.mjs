// Browser DOM contract gates; these do not replace packaged application testing.
// node tools/chat-rich-message-gates.mjs --edge PATH --out FRESH_QA_DIRECTORY
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {pathToFileURL} from 'node:url';
const flag=name=>process.argv[process.argv.indexOf('--'+name)+1];
if(!process.argv.includes('--edge')||!process.argv.includes('--out'))throw Error('--edge and --out are required');
const edge=path.resolve(flag('edge')),out=path.resolve(flag('out'));
await fs.mkdir(out);await fs.mkdir(path.join(out,'profile'));
const source=await fs.readFile(process.argv.includes('--source')?path.resolve(flag('source')):new URL('../resources/chat/chrome-shim.js',import.meta.url),'utf8');
const bootstrap=`
const sent=[],handlers=[];window.sideEffect=false;
window.chrome ||= {};
window.chrome.webview={addEventListener:(name,fn)=>handlers.push(fn),postMessage(packet){sent.push(structuredClone(packet));setTimeout(()=>handlers.forEach(fn=>fn({data:{kind:'reply',id:packet.id,frame:packet.frame,value:{id:1}}})),0);}};
`;
const fixtures=`
(async()=>{
 const checks=[];const check=(ok,name)=>{checks.push({ok,name});};
 async function capture(message){await chrome.runtime.sendMessage({message});return sent.filter(x=>x.method==='runtime.sendMessage').at(-1).args[0].message;}
 const image='<img src="https://invalid.example/'+ 'x'.repeat(240) +'" alt="Kappa">';
 const body='Hello &amp; goodbye<br>'+Array(120).fill(image).join(' ');
 const rich=await capture({chatname:'Fixture',chatmessage:body,textonly:false,type:'twitch'});
 check(rich.chatmessage==='Hello & goodbye\\n'+Array(120).fill('Kappa').join(' '),'Long emote markup retains the entire readable body');
 check(rich.textonly===true&&JSON.stringify(rich).length<8000,'Compacted body fits the existing native limit');
 const reply=await capture({chatmessage:body,reply:'Yes &lt;3<br>again',initial:'Replying to <b>@Anna</b>',textonly:false});
 check(reply.reply==='Yes <3\\nagain'&&reply.initial==='Replying to @Anna','Reply and parent fields share the text encoding');
 const literal='<img alt="literal"> &amp; '+'z'.repeat(8001);
 const plain=await capture({chatmessage:literal,textonly:true});
 check(plain.chatmessage===literal&&plain.textonly===true,'Plain text is not reinterpreted or truncated');
 const huge='x'.repeat(256001);
 check((await capture({chatmessage:huge,textonly:false})).chatmessage===huge,'Over-budget input remains subject to native rejection');
 const inert=await capture({chatmessage:body+'<img src="https://invalid.example/tracker" onerror="window.sideEffect=true">'+'<scr'+'ipt>window.sideEffect=true</scr'+'ipt>',textonly:false});
 check(!window.sideEffect&&!performance.getEntriesByType('resource').some(x=>x.name.includes('invalid.example')),'Template parsing neither executes markup nor loads images');
 document.querySelector('#result').textContent=JSON.stringify({passed:checks.every(x=>x.ok),checks});
})().catch(e=>document.querySelector('#result').textContent=JSON.stringify({passed:false,error:String(e)}));
`;
const file=path.join(out,'gate.html');
await fs.writeFile(file,'<!doctype html><meta charset="utf-8"><pre id="result"></pre><script>'+bootstrap+source.replace('__ENGINE_CONFIG__',JSON.stringify({role:'capture',manifest:{version:'gate'}}))+fixtures+'</script>');
const child=spawn(edge,['--headless','--no-first-run','--disable-background-networking','--disable-extensions','--disable-gpu','--user-data-dir='+path.join(out,'profile'),'--dump-dom','--virtual-time-budget=3000',pathToFileURL(file).href],{windowsHide:true,stdio:['ignore','pipe','pipe']});
let stdout='',stderr='';child.stdout.on('data',data=>stdout+=data);child.stderr.on('data',data=>stderr+=data);
const code=await new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',resolve);});
await fs.writeFile(path.join(out,'browser.stderr.log'),stderr);
const match=stdout.match(/<pre id="result">([^<]*)<\/pre>/);
const result=match?JSON.parse(match[1].replace(/&quot;/g,'"').replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&amp;/g,'&')):{passed:false,error:'No DOM gate result',exitCode:code};
await fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
process.exitCode=code||!result.passed?1:0;
