import fs from 'node:fs/promises';
import {createWriteStream} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import net from 'node:net';
import {createHash} from 'node:crypto';
import {CDP,delay,until} from './cdp.mjs';
import {auditSource} from './audit-source.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const args={};for(let i=2;i<process.argv.length;i++){const key=process.argv[i];if(!key.startsWith('--'))throw Error('Expected --option');args[key.slice(2)]=process.argv[++i];}
const stamp=new Date().toISOString().replaceAll(/[:.]/g,'-');
const out=path.resolve(args.out||path.join(here,'runs',stamp));
await fs.mkdir(path.dirname(out),{recursive:true});await fs.mkdir(out); // Never reuse evidence.
const profile=path.resolve(args.profile||path.join(out,'profile'));
const root=path.resolve(args['ssn-root']||'C:/Users/Steve/code/social_stream');
const exe=path.resolve(args.exe||path.join(here,'build','engine-spike.exe'));
let session='enginespike'+stamp.replaceAll(/[^a-z0-9]/gi,'').slice(-20);
try{session=JSON.parse(await fs.readFile(path.join(profile,'extension-storage.json'),'utf8')).sync.streamID||session;}catch{}
const result={started:new Date().toISOString(),out,root,exe,profile,session,scenarios:[],runs:[],screenshots:[],evidence:[],errors:[],testingCompleted:false};
const titles=['Engine boots','Capture to engine','Clean feed to host','Comment on screen','Poll including reload','Giveaway','Game and games.html','Bridge-required overlays','Event Flow','Points across native restart','Live Twitch source','Reply path (dry)'];
let child,port,host,engine,source,currentNative,runIndex=0;
const clients=new Map(),tools=new Map();
const write=async()=>fs.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2));
const assert=(v,message)=>{if(!v)throw new Error(message);};
async function freePort(){const s=net.createServer();await new Promise((res,rej)=>{s.once('error',rej);s.listen(args.port?Number(args.port):0,'127.0.0.1',res);});const n=s.address().port;await new Promise(res=>s.close(res));return n;}
async function targets(){return fetch(`http://127.0.0.1:${port}/json/list`,{signal:AbortSignal.timeout(1200)}).then(r=>r.json());}
async function connect(url){
  const t=await until(async()=> (await targets()).find(t=>t.type==='page'&&t.url===url),{timeout:30000,description:'page '+url});
  if(clients.has(t.id))return clients.get(t.id);
  const c=new CDP(t.webSocketDebuggerUrl);await c.ready;clients.set(t.id,c);
  await c.call('Runtime.enable');await c.call('Log.enable');await c.call('Network.enable');
  return c;
}
async function start(){
  port=await freePort();currentNative=path.join(out,'native-'+(++runIndex));
  const argv=['--ssn-root',root,'--out',currentNative,'--profile',profile,'--room',session,'--remote-debugging-port',String(port),'--duration-sec','900'];
  if(args['browser-args'])argv.push('--browser-args',args['browser-args']);if(args.twitch)argv.push('--twitch',args.twitch);
  const std=createWriteStream(path.join(out,`native-${runIndex}-stdout.log`));
  child=spawn(exe,argv,{windowsHide:true,stdio:['ignore','pipe','pipe']});child.stdout.pipe(std);child.stderr.pipe(std);
  let launchError;child.once('error',e=>{launchError=e;});child.once('close',()=>std.end());
  result.runs.push({directory:currentNative,pid:child.pid,port,argv});await write();
  await until(async()=>{if(launchError)throw launchError;return (await targets()).some(t=>t.url.startsWith('https://engine-spike.invalid/host.html'));},{timeout:30000,description:'native host page to navigate'});
  host=await connect(`https://engine-spike.invalid/host.html?session=${session}`);
  engine=await connect('https://socialstream.ninja/background.html');source=await connect('https://engine-spike.invalid/test-source.html');
  await until(()=>host.evaluate('!!window.__engineSpike && !!window.feature'),{description:'host control surface'});
}
async function stop(){
  const owned=child;if(!owned)return;
  try{if(host)await host.evaluate(`__engineSpike.rpc('host.quit')`);}catch{}
  for(const c of clients.values())c.close();clients.clear();tools.clear();host=engine=source=null;
  if(owned.exitCode===null&&owned.signalCode===null)await Promise.race([new Promise(res=>owned.once('exit',res)),delay(3500)]);
  // Only the exact ChildProcess created above. No process-name or tree kill.
  if(owned.exitCode===null&&owned.signalCode===null){owned.kill();await Promise.race([new Promise(res=>owned.once('exit',res)),delay(1500)]);}
  child=null;
}
async function openTool(name,file=name+'.html',params={}){
  if(tools.has(name))return tools.get(name);
  const u=new URL(file,'https://socialstream.ninja/');u.search=new URLSearchParams({session,password:'false',...params}).toString();
  const tab=await host.evaluate(`__engineSpike.rpc('host.open',[${JSON.stringify({role:name,url:u.href})}])`);
  const c=await connect(u.href);c.tab=tab.id;tools.set(name,c);
  await until(()=>c.evaluate(`document.readyState==='complete'`),{description:name+' document load'});
  return c;
}
async function runtime(message){return source.evaluate(`chrome.runtime.sendMessage(${JSON.stringify(message)})`);}
async function setting(key,value,type){return runtime({cmd:'saveSetting',setting:key,value,...(type?{type}:{})});}
async function capture(name,text,extra={}){return source.evaluate(`capture(${JSON.stringify({chatname:name,username:name,userid:name,type:'twitch',chatmessage:text,textonly:true,...extra})})`);}
async function peers(){return host.evaluate(`__engineSpike.rpc('host.peers')`);}
async function peerFor(c){return until(async()=> (await peers()).find(p=>p.tab===c.tab),{description:'bridge peer for tab '+c.tab});}
async function visible(c,selector,text){return until(()=>c.evaluate(`(()=>{const e=document.querySelector(${JSON.stringify(selector)});return e && getComputedStyle(e).display!=='none' && getComputedStyle(e).visibility!=='hidden' && e.innerText.includes(${JSON.stringify(text)}) ? e.innerText : false;})()`),{description:`${selector} visibly contains ${text}`});}
async function screenshot(c,label){
  const base=label.replaceAll(/[^a-z0-9_-]/gi,'-');
  try{const state=await c.state();await fs.writeFile(path.join(out,base+'.dom.json'),JSON.stringify(state,null,2));result.evidence.push(base+'.dom.json');
    const image=await c.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});await fs.writeFile(path.join(out,base+'.png'),Buffer.from(image.data,'base64'));result.screenshots.push(base+'.png');
  }catch(e){result.errors.push({stage:'screenshot',label,error:e.message});}
}
async function readyEngine(){return until(()=>engine.evaluate(`window.ssappBackgroundLoadState?.status==='ready' && typeof loadedFirst!=='undefined' && loadedFirst && isExtensionOn && typeof sendToDestinations==='function'`),{timeout:30000,description:'unmodified engine and settings ready'});}
async function step(n,fn){const row={number:n,name:titles[n-1],status:'running',started:new Date().toISOString()};result.scenarios.push(row);await write();console.log(`${n}. ${row.name}`);
  try{row.observation=await fn(row);row.status='passed';}catch(e){row.status='failed';row.reason=e.message;}
  row.finished=new Date().toISOString();await write();console.log(`   ${row.status}${row.reason?': '+row.reason:''}`);
}
async function hostProcessed(name){return until(()=>host.evaluate(`feed.findLast(m=>m?.chatname===${JSON.stringify(name)} && m.chatmessage)`),{description:'processed host feed for '+name});}
let firstMessage,firstId;
try {
  const audit=await auditSource(root);await fs.writeFile(path.join(out,'source-audit.json'),JSON.stringify(audit,null,2));result.staticAudit='source-audit.json';
  await start();
  await step(1,async()=>{
    await readyEngine();
    const observed=await engine.evaluate(`({load:window.ssappBackgroundLoadState,extensionPath:!isSSAPP,streamID,settingsLoaded:loadedFirst,scripts:[...document.scripts].map(s=>s.src).filter(Boolean)})`);
    await screenshot(engine,'01-engine');assert(observed.extensionPath,'Engine selected Electron path');assert(observed.load.failures.length===0,'Loader reported module failures');
    const errors=engine.events.filter(e=>e.method==='Runtime.exceptionThrown');assert(errors.length===0,'Uncaught engine errors: '+JSON.stringify(errors));return observed;
  });
  await step(2,async()=>{
    const response=await capture('HarnessCapture','Engine capture callback');firstId=response?.id;
    const dom=await source.evaluate(`JSON.parse(document.querySelector('#callback').innerText)`);
    await screenshot(source,'02-capture-callback');assert(Number.isSafeInteger(dom.id)&&dom.id===firstId&&dom.state===true,'Capture callback DOM did not contain an engine allocated ID and enabled state');return {callback:dom};
  });
  await step(3,async()=>{
    // HTML-mode capture must be sanitized by the actual destination pipeline.
    await capture('HarnessClean','<b>Clean feed marker</b><script>bad()</script>',{textonly:false});
    firstMessage=await hostProcessed('HarnessClean');assert(Number.isSafeInteger(firstMessage.id),'Processed feed lacks allocated ID');assert(!firstMessage.chatmessage.includes('<script'),'Destination pipeline did not sanitize HTML');
    await visible(host,'#feed','Clean feed marker');await runtime({delete:{id:firstMessage.id,type:'twitch'}});
    const deletion=await until(()=>host.evaluate(`feed.find(m=>m?.delete?.id===${firstMessage.id})`),{description:'processed deletion in host feed'});
    await screenshot(host,'03-host-feed-and-delete');
    const native=await fs.readFile(path.join(currentNative,'feed.jsonl'),'utf8');assert(native.includes('Clean feed marker')&&native.includes('"delete"'),'Native feed file missing processed message/deletion');return {processed:firstMessage,deletion};
  });
  await step(4,async row=>{
    const air=await openTool('featured-air','featured.html',{instance:'air'}),preview=await openTool('featured-preview','featured.html',{instance:'preview'});
    const pa=await peerFor(air),pp=await peerFor(preview);
    await capture('HarnessAir','AIR ONLY');const a=await hostProcessed('HarnessAir');await capture('HarnessPreview','PREVIEW ONLY');const b=await hostProcessed('HarnessPreview');
    await host.evaluate(`feature(${JSON.stringify(a)},${JSON.stringify(pa.uuid)});feature(${JSON.stringify(b)},${JSON.stringify(pp.uuid)})`);
    await visible(air,'body','AIR ONLY');await visible(preview,'body','PREVIEW ONLY');
    const snapshot=async c=>c.evaluate(`({text:document.body.innerText,messages:[...document.querySelectorAll('.hl-c-cont.highlight-chat')].filter(e=>getComputedStyle(e).display!=='none'&&getComputedStyle(e).opacity!=='0').map(e=>e.innerText)})`);
    const states={air:await snapshot(air),preview:await snapshot(preview)};row.observation=states;
    assert(!states.air.text.includes('PREVIEW ONLY')&&!states.preview.text.includes('AIR ONLY'),'Featured instances leaked into each other');
    assert(states.air.messages.length===1&&states.preview.messages.length===1,'Featured DOM did not display exactly one comment per instance');
    await screenshot(air,'04-air');await screenshot(preview,'04-private-preview');
    await host.evaluate(`feature(false,${JSON.stringify(pa.uuid)})`);
    const outputOpacity=c=>c.evaluate(`Number(getComputedStyle(document.getElementById('output')).opacity)`);assert(await outputOpacity(air)>0.9,'On-air comment was not visible before clear');await until(async()=>(await outputOpacity(air))<0.05,{description:'clear fades out the on-air comment'});assert(await outputOpacity(preview)>0.9,'Clearing on-air also hid the private preview');
    await visible(preview,'body','PREVIEW ONLY');await screenshot(air,'04-air-cleared');return {...states,clearRemovedAir:true,previewPreserved:true};
  });
  // Measure exactly six WebViews: engine + five SSN tools. Temporarily close the
  // host control view and capture views too; use the engine's CDP control channel.
  try {
    for(const [name,c]of tools)await host.evaluate(`__engineSpike.rpc('host.close',[${c.tab}])`);tools.clear();
    for(const name of ['featured','poll','giveaway','ticker','hype'])await openTool(name,name+'.html',name==='giveaway'?{managed:'1'}:{});
    const support=await host.evaluate(`chrome.tabs.query({}).then(tabs=>tabs.filter(t=>['host','test-source','twitch'].includes(t.title)))`);
    try {
      for(const tab of support)await engine.evaluate(`chrome.tabs.remove(${tab.id})`);
      await delay(2500);result.memory={composition:'engine + featured, poll, managed giveaway, ticker, hype; host and all capture views closed',samples:[]};
      for(let i=0;i<3;i++){result.memory.samples.push(await engine.evaluate(`__engineSpike.rpc('host.metrics')`));await delay(500);}
      assert(result.memory.samples.every(s=>s.pageCount===6),'Memory sample did not contain exactly six WebViews');
    }finally {
      for(const tab of support)await engine.evaluate(`chrome.tabs.create(${JSON.stringify({role:tab.title,url:tab.url})})`);
      host=await connect(`https://engine-spike.invalid/host.html?session=${session}`);source=await connect('https://engine-spike.invalid/test-source.html');
      await until(()=>host.evaluate('!!window.feature'),{description:'host restored after memory measurement'});
    }
  }catch(e){result.memory={...result.memory,error:e.message};}
  await step(5,async row=>{
    const p=await openTool('poll');await peerFor(p);await setting('pollEnabled',true,'setting');
    await engine.evaluate(`createNewPoll({pollType:'multiple',pollQuestion:'Harness colour?',multipleChoiceOptions:'Red,Blue',pollTally:true,pollTimerState:false});sendTargetP2P({cmd:'startpoll'},'poll')`);
    await visible(p,'#poll-title','Harness colour?');await capture('PollRed','1');await capture('PollBlue','2');
    const votes=await until(()=>p.evaluate(`(()=>{const v=[...document.querySelectorAll('.option-tally')].map(e=>e.innerText);return v.length===2&&v.every(x=>/^1 votes?$/.test(x))?v:false;})()`),{description:'two visible one-vote tallies'});
    await screenshot(p,'05-poll-before-reload');row.observation={before:votes};
    await p.call('Page.reload');await delay(1000);await visible(p,'#poll-title','Harness colour?');await delay(1500);
    const after=await p.evaluate(`[...document.querySelectorAll('.option-tally')].map(e=>e.innerText)`);row.observation.after=after;await screenshot(p,'05-poll-after-reload');
    assert(JSON.stringify(after)===JSON.stringify(votes),'Poll vote totals did not survive reload; engine reconnect supplies configuration/control state, not accumulated votes');return row.observation;
  });
  await step(6,async()=>{
    const p=await openTool('giveaway','giveaway.html',{managed:'1'});await peerFor(p);
    const start=await engine.evaluate(`handleGiveawayAction('startgiveaway',{giveawayId:'default',config:{keyword:'!harnessenter',match:'exact',winnerCount:1,removeWinner:false,kind:'giveaway'}})`);assert(start?.ok,'Giveaway start failed: '+JSON.stringify(start));
    await capture('GiveawayAlice','!harnessenter');await capture('GiveawayBob','!harnessenter');await visible(p,'.giveaway-stage','2 eligible entries');await screenshot(p,'06-giveaway-entries');
    const draw=await engine.evaluate(`handleGiveawayAction('drawgiveaway',{giveawayId:'default'})`);assert(draw?.ok,'Giveaway draw failed: '+JSON.stringify(draw));
    const winner=await until(()=>p.evaluate(`document.querySelector('.giveaway-result')?.innerText.match(/Giveaway(Alice|Bob) wins!/)?.[0]`),{description:'actual drawn winner shown'});await screenshot(p,'06-giveaway-winner');return {entries:2,winner};
  });
  await step(7,async()=>{
    const quick=await openTool('quickcall','games/quickcall.html'),games=await openTool('games');await peerFor(quick);await peerFor(games);
    const before=await games.evaluate(`Number(document.querySelector('#total-power').innerText)`);
    await capture('HarnessPlayer','!quick');await visible(quick,'#calls','HarnessPlayer');
    const code=await quick.evaluate(`[...document.querySelectorAll('#calls .call')].find(e=>e.innerText.includes('HarnessPlayer'))?.querySelector('.code')?.innerText`);assert(code,'Quick Call did not show an actual challenge');await capture('HarnessPlayer',code);
    await visible(quick,'#leaders','HarnessPlayer');const power=await until(()=>games.evaluate(`Number(document.querySelector('#total-power').innerText)>${before} && Number(document.querySelector('#total-power').innerText)`),{description:'games.html power increases from chat'});
    await screenshot(quick,'07-quickcall');await screenshot(games,'07-games');return {quickCallCompleted:true,powerBefore:before,powerAfter:power};
  });
  await step(8,async row=>{
    const checks={};row.observation=checks;
    for(const name of ['ticker','hype','waitlist','multi-alerts']){
      try{
        const p=await openTool(name);await peerFor(p);
        if(name==='ticker'){await engine.evaluate(`sendTargetP2P({ticker:['Harness ticker control']},'ticker')`);await visible(p,'#ticker','Harness ticker control');}
        if(name==='hype'){await setting('hypemode',true,'setting');await capture('HarnessHype','Hype capture');await until(()=>p.evaluate(`[...document.querySelectorAll('[data-chatters]')].some(e=>Number(e.textContent)>0)`),{timeout:18000,description:'hype chatter count > 0'});}
        if(name==='waitlist'){await setting('waitlistmode',true,'setting');await capture('HarnessWaitlist','!join');await visible(p,'#waitlist','HarnessWaitlist');}
        if(name==='multi-alerts'){await capture('HarnessFollower','New follower',{event:'new_follower'});await visible(p,'body','HarnessFollower');}
        checks[name]={status:'passed',state:await p.state()};await screenshot(p,'08-'+name);
      }catch(e){checks[name]={status:'failed',reason:e.message};}
    }
    assert(Object.values(checks).every(c=>c.status==='passed'),'Overlay failures: '+Object.entries(checks).filter(([,c])=>c.status!=='passed').map(([n,c])=>n+': '+c.reason).join('; '));return checks;
  });
  await step(9,async row=>{
    const flow={id:'engine-spike-flow',name:'Harness keyword transformation',active:true,nodes:[{id:'trigger',type:'trigger',triggerType:'messageContains',config:{text:'harnesskeyword'}},{id:'action',type:'action',actionType:'modifyMessage',config:{newMessage:'FLOW VISIBLE EFFECT'}}],connections:[{from:'trigger',to:'action'}]};
    await engine.evaluate(`eventFlowSystem.saveFlow(${JSON.stringify(flow)})`);
    await capture('HarnessFlow','harnesskeyword');const processed=await hostProcessed('HarnessFlow');assert(processed.chatmessage==='FLOW VISIBLE EFFECT','Saved flow did not transform captured chat');await visible(host,'#feed','FLOW VISIBLE EFFECT');
    const featured=await openTool('flow-featured','featured.html');const peer=await peerFor(featured);await delay(2000);row.observation={processed,peer};await host.evaluate(`feature(${JSON.stringify(processed)},${JSON.stringify(peer.uuid)})`);await delay(1500);row.observation.page=await featured.evaluate(`({text:document.body.innerText,html:document.getElementById('output')?.innerHTML?.slice(0,300),lastMessageId:typeof lastMessageId==='undefined'?'n/a':lastMessageId,opacity:getComputedStyle(document.getElementById('output')).opacity})`);await screenshot(featured,'09-event-flow-debug');await visible(featured,'body','FLOW VISIBLE EFFECT');await screenshot(featured,'09-event-flow');return {flowId:flow.id,processed};
  });
  await step(10,async row=>{
    await setting('enablePointsSystem',true,'setting');const name='Points'+stamp.replaceAll(/[^0-9]/g,'');
    await engine.evaluate(`window.pointsSystemReady().then(()=>true)`);const before=await engine.evaluate(`pointsSystem.getUserPoints(${JSON.stringify(name)},'twitch')`);
    const leaderboard=await openTool('leaderboard','leaderboard.html',{rankby:'loyalty'});await peerFor(leaderboard);
    await capture(name,'Accrue points from captured chat');const earned=await until(async()=>{const v=await engine.evaluate(`pointsSystem.getUserPoints(${JSON.stringify(name)},'twitch')`);return v.points>before.points?v:false;},{description:'points accrued in authoritative database'});
    await visible(leaderboard,'body',name);await screenshot(leaderboard,'10-points-before-restart');row.observation={before,earned};
    await stop();await delay(1500);await start();await readyEngine();await engine.evaluate('window.pointsSystemReady().then(()=>true)');
    const restored=await engine.evaluate(`pointsSystem.getUserPoints(${JSON.stringify(name)},'twitch')`);row.observation.restored=restored;
    const after=await openTool('leaderboard','leaderboard.html',{rankby:'loyalty'});await visible(after,'body',name);await screenshot(after,'10-points-after-restart');
    assert(restored.points===earned.points,'Points changed or disappeared across native process restart');return row.observation;
  });
  if(args.twitch)await step(11,async()=>{
    const twitchTab=await host.evaluate(`chrome.tabs.query({}).then(tabs=>tabs.find(t=>t.url.startsWith('https://www.twitch.tv/popout/'))?.id)`);assert(twitchTab,'Native Twitch capture tab missing');
    const message=await until(()=>host.evaluate(`feed.find(m=>m?.type==='twitch' && m.chatmessage && String(m.tid)===String(${twitchTab}))`),{timeout:Number(args['live-seconds']||60)*1000,description:'real Twitch capture reaches processed host feed'});
    const p=await openTool('live-featured','featured.html');const peer=await peerFor(p);await delay(2000);await host.evaluate(`feature(${JSON.stringify(message)},${JSON.stringify(peer.uuid)})`);
    const literal=await p.evaluate(`(()=>{const t=document.createElement('template');t.innerHTML=${JSON.stringify(message.chatmessage)};return ${message.textonly===true?'true':'false'}?${JSON.stringify(message.chatmessage)}:t.content.textContent;})()`);
    await visible(p,'body',literal);await screenshot(p,'11-live-twitch-featured');return {message,visibleText:literal};
  });else result.scenarios.push({number:11,name:titles[10],status:'not-run',reason:'No --twitch channel supplied; no real source was contacted.'});
  await step(12,async()=>{
    const tab=await source.evaluate('__engineSpike.config.tab');await host.evaluate(`engineCommand({response:'DRY LOCAL REPLY',tid:${tab},outgoingOrigin:'host'})`);
    await visible(host,'#debugger','Input.insertText');const entries=await host.evaluate(`document.querySelector('#debugger').innerText.trim().split(String.fromCharCode(10)).filter(Boolean).map(JSON.parse)`);
    assert(entries.some(e=>e.tab===tab&&e.args[1]==='Input.insertText'&&e.args[2]?.text==='DRY LOCAL REPLY'),'Engine debugger command did not target the local capture tab');assert(entries.every(e=>e.executed===false),'Dry debugger stub unexpectedly executed input');
    await screenshot(host,'12-dry-reply-host');await screenshot(source,'12-local-target-unchanged');return {tab,commands:entries,executed:false};
  });
  result.testingCompleted=result.scenarios.every(s=>s.status==='passed');
}catch(e){
  result.errors.push({stage:'startup-or-driver',error:e.stack});
  for(let n=1;n<=12;n++)if(!result.scenarios.some(s=>s.number===n))result.scenarios.push(n===11&&!args.twitch?{number:n,name:titles[n-1],status:'not-run',reason:'No --twitch channel supplied.'}:{number:n,name:titles[n-1],status:'blocked',reason:'Application did not become runnable: '+e.message});
}finally{
  for(const [id,c]of clients){try{if(c.ws.readyState===WebSocket.OPEN)await screenshot(c,'final-'+id);await fs.writeFile(path.join(out,'cdp-'+id+'.json'),JSON.stringify(c.events,null,2));}catch{}}
  await stop();
  const logs=[];for(const run of result.runs){try{for(const line of (await fs.readFile(path.join(run.directory,'events.jsonl'),'utf8')).trim().split('\n'))if(line)logs.push(JSON.parse(line));}catch(e){result.errors.push({stage:'read-native-log',error:e.message});}}
  result.nativeFailures=logs.filter(e=>['native-error','process-failed'].includes(e.event));
  result.observationCoverage={documentsStarted:logs.filter(e=>e.event==='document-ready').length,requestsObserved:logs.filter(e=>e.event==='request'||e.event==='cdp-network').length};
  if(!result.memory)result.memory={status:'unavailable',reason:'Application did not reach the six-WebView measurement workflow.'};
  result.pageErrors=logs.filter(e=>['js-error','unhandled-rejection','console-error','console-warn','missing-api','listener-error'].includes(e.event));
  result.chromeCalls=[...new Set(logs.filter(e=>e.event==='api'&&!/^(host|bridge)\./.test(e.name)).map(e=>e.name))].sort();
  result.unavailableApiProbes=logs.filter(e=>e.event==='api-unavailable-probe');
  result.optionalBridgeReads=logs.filter(e=>e.event==='optional-bridge-read');result.ninjafyCalls=[];result.electronApiCalls=[];
  result.bridgeUnsupported=logs.filter(e=>e.event==='unsupported-bridge');
  result.network=[];const seen=new Set();const roles=new Map(logs.filter(e=>e.event==='page-open').map(e=>[e.tab,e.role]));
  for(const e of logs){
    let url=e.event==='request'?e.url:e.event==='websocket-attempt'?e.url:e.event==='cdp-network'?(e.data?.request?.url||e.data?.response?.url||e.data?.url):null;
    if(!url)continue;let u;try{u=new URL(url);}catch{continue;}if(!['http:','https:','ws:','wss:'].includes(u.protocol))continue;
    const disposition=e.disposition||(e.event==='websocket-attempt'?(e.blocked?'blocked-websocket':'websocket-attempt'):e.method==='Network.responseReceived'?'response-received':'cdp-request-observed');
    const key=[e.tab,u.host,disposition].join('|');if(seen.has(key))continue;seen.add(key);result.network.push({tab:e.tab,role:e.role||roles.get(e.tab),host:u.host,disposition,example:url});
  }
  result.outsideHosts=[...new Set(result.network.filter(e=>!['socialstream.ninja','engine-spike.invalid','vdo.socialstream.ninja'].includes(e.host)).map(e=>e.host))].sort();
  result.browserPids=[...new Set(logs.filter(e=>e.event==='controller-ready').map(e=>e.browserPid))];
  result.sourceFiles=[];
  const files=new Set(['manifest.json','service_worker.js','background.html','loader.js','background.js','shared/overlay-control-transport.js','sources/twitch.js']);
  for(const e of logs){const url=e.event==='request'?e.url:e.event==='cdp-network'?e.data?.request?.url:null;try{const u=new URL(url);if(u.host==='socialstream.ninja')files.add(decodeURIComponent(u.pathname).replace(/^\//,''));}catch{}}
  for(const name of files){const full=path.resolve(root,name);if(!full.startsWith(root+path.sep))continue;try{const bytes=await fs.readFile(full);result.sourceFiles.push({path:name,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});}catch{}}
  result.finished=new Date().toISOString();await write();console.log('Results: '+path.join(out,'results.json'));
}
process.exitCode=result.scenarios.some(s=>['failed','blocked'].includes(s.status))?1:0;
