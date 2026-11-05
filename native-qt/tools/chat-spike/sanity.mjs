// Offline contract/analysis gates. These are NOT end-to-end application testing.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const here = fileURLToPath(new URL('.', import.meta.url));
const packets = [], jobs = [], timers = [], load = [];
class Doc { get visibilityState() { return 'hidden'; } }
class Element { click() {} focus() {} }
class Target { dispatchEvent() { return true; } }
const document = new Doc(); document.readyState = 'loading'; document.addEventListener = (name, fn) => load.push([name, fn]);
const channel = { postMessage: p => packets.push(p) };
const context = { URL, Document: Doc, HTMLElement: Element, EventTarget: Target, document, location: { href: 'https://www.twitch.tv/popout/example/chat' },
  chrome: { webview: channel }, addEventListener: (name, fn) => load.push([name, fn]), queueMicrotask: fn => jobs.push(fn),
  setTimeout: fn => timers.push(fn), setInterval: () => {}, requestAnimationFrame: () => {}, performance: { timeOrigin: 1, now: () => 1000 } };
context.window = context; context.top = context;
vm.createContext(context);
const shim = fs.readFileSync(join(here, 'chrome-shim.js'), 'utf8'), bootstrap = fs.readFileSync(join(here, 'bootstrap.js'), 'utf8');
vm.runInContext(shim+'\n'+bootstrap, context);
const drain = () => { while (jobs.length) jobs.shift()(); };
assert.equal(context.chrome.webview,channel);
let stringAnswer;
context.chrome.runtime.sendMessage('heartbeat', r => stringAnswer = r); drain();
assert.equal(stringAnswer.state,true);
assert.equal(packets.at(-2).payloadJson,JSON.stringify('heartbeat'));
for (const withId of [false,true]) {
  let answer;
  context.chrome.runtime.sendMessage(...(withId ? ['chat-spike',{ getSettings:true },r=>answer=r] : [{ getSettings:true },r=>answer=r]));
  drain(); assert.equal(answer.state,true); assert.equal(answer.settings.textonlymode,true);
}
const request = { message: { chatname:'Example',chatmessage:'a < b & c',textonly:true,type:'youtube' },youtubeMessageRequestId:'req1' };
const original = JSON.stringify(request); let reply;
context.chrome.runtime.onMessage.addListener(m=>reply=m);
const answer = await context.chrome.runtime.sendMessage(request); drain();
assert.equal(answer.id,1); assert.equal(reply.youtubeMessageResponse.id,1);
assert.equal(JSON.stringify(request),original);
assert.equal(packets.find(p=>p.payloadJson===original).payloadJson,original);
assert.equal(context.chrome.runtime.getURL('/shared/a.js'),'https://ssn-resources.invalid/shared/a.js');
await context.chrome.storage.local.set({hello:1}); drain();
assert.equal((await context.chrome.storage.local.get({hello:0,missing:2})).missing,2);
assert.equal((await context.chrome.storage.local.get('hello')).hello,1);
let runs = 0;
context.__chatSpike.register({matches:['https://*.twitch.tv/popout/*'],run_at:'document_start'},()=>runs++);
context.__chatSpike.register({matches:['https://*.twitch.tv/popout/*'],exclude_matches:['https://www.twitch.tv/*'],run_at:'document_start'},()=>runs++);
assert.equal(runs,1);
context.location.href='https://twitch.tv/popout/example/chat';
context.__chatSpike.register({matches:['https://*.twitch.tv/popout/*'],run_at:'document_start'},()=>runs++);
assert.equal(runs,2); // wildcard subdomain also matches bare host
context.location.href='https://twitch.tv.evil.invalid/popout/example/chat';
context.__chatSpike.register({matches:['https://*.twitch.tv/popout/*'],run_at:'document_start'},()=>runs++);
assert.equal(runs,2);
context.location.href='https://www.youtube.com/live_chat?v=abc';
context.__chatSpike.register({matches:['https://www.youtube.com/live_chat*'],run_at:'document_end'},()=>runs++);
assert.equal(runs,2); for (const [name,fn] of load) if(name==='DOMContentLoaded') fn(); assert.equal(runs,3);
context.__chatSpike.register({matches:['https://www.youtube.com/live_chat*']},()=>runs++);
for (const [name,fn] of load) if(name==='load') fn(); assert.equal(runs,3); timers.shift()(); assert.equal(runs,4);
context.top = {}; context.parent = {};
context.__chatSpike.register({matches:['https://www.youtube.com/*'],run_at:'document_start'},()=>runs++);
assert.equal(runs,4);
context.__chatSpike.register({matches:['https://www.youtube.com/*'],run_at:'document_start',all_frames:true},()=>runs++);
assert.equal(runs,5);
new context.HTMLElement().click(); new context.EventTarget().dispatchEvent({type:'keydown'});
if (process.argv[2]) {
  const root = process.argv[2], manifest = JSON.parse(fs.readFileSync(join(root,'manifest.json'),'utf8'));
  let bundle = shim+'\n'+bootstrap;
  for(const entry of manifest.content_scripts) if(entry.js?.length) bundle+='\nwindow.__chatSpike.register('+JSON.stringify(entry)+',function(){\n'+entry.js.map(f=>fs.readFileSync(join(root,f),'utf8')).join('\n;\n')+'\n});\n';
  new vm.Script(bundle); console.log(`Manifest bundle parses: ${Buffer.byteLength(bundle)} bytes`);
}
// Duplicate text is assigned once; one miss and one unmatched capture are real.
const fixture = join(here,'build','sanity-fixture'); fs.mkdirSync(fixture,{recursive:true});
const start = 1700000000000;
fs.writeFileSync(join(fixture,'run.json'),JSON.stringify({startMs:start,endMs:start+100000,mode:'hidden',sources:[{id:'twitch-example',spec:'twitch:example'}]}));
fs.writeFileSync(join(fixture,'metrics.csv'),'timestamp_ms,source,mode,message_count,last_message_age_s,raf_per_s,timer_per_s,visibility,reported_visibility,probe_age_s,blocked_input,cpu_percent,private_working_set_mib,process_count,process_errors\n'+[10,20,30].map((v,i)=>`${start+v*1000},twitch-example,hidden,3,0,0,1,hidden,visible,0,0,1,${100+i},3,0`).join('\n'));
fs.writeFileSync(join(fixture,'events.log'),JSON.stringify({source:'twitch-example',event:'injection-complete',files:['example.js']})+'\n');
fs.writeFileSync(join(fixture,'truth-twitch-example-meta.json'),JSON.stringify({startMs:start,endMs:start+100000,intervals:[{startMs:start,endMs:start+100000}]}));
const record = (seconds,text) => JSON.stringify({receivedMs:start+seconds*1000,payload:{message:{chatname:' @Example ',chatmessage:text,textonly:true}}});
fs.writeFileSync(join(fixture,'truth-twitch-example.jsonl'),[record(50,'same'),record(52,'same'),record(65,'miss')].join('\n'));
fs.writeFileSync(join(fixture,'messages-twitch-example.jsonl'),[record(50.1,'same'),record(52.2,'same'),record(65,'false')].join('\n'));
const report = JSON.parse(execFileSync(process.execPath,[join(here,'analyze.mjs'),fixture],{encoding:'utf8'}));
const comparison=report.sources['twitch-example'].comparison;
assert.equal(comparison.matched,2); assert.equal(comparison.missedCount,1); assert.equal(comparison.falseCount,1);
assert.equal(comparison.latencyMs.p95,200);
assert.equal(report.metrics['twitch-example'].rafPerSecond.mean,0);
assert.equal(report.metrics['twitch-example'].nativeVisibility[0],'hidden');
assert.equal(report.metrics['twitch-example'].memoryGrowth.direction,'increasing during this run');
console.log('Shim, matcher/timing, immutable payload, and analysis fixture gates passed. Application testing NOT performed by this script.');
