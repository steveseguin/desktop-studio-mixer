// Offline contract gates; NOT application testing. Uses mocks intentionally.
import vm from 'node:vm';
import fs from 'node:fs/promises';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
const shim=await fs.readFile(new URL('chrome-shim.js',import.meta.url),'utf8');
const bridge=(await fs.readFile(new URL('bridge.html',import.meta.url),'utf8')).match(/<script>([\s\S]*?)<\/script>/)[1];
const sent=[],handlers=new Map(),nativeHandlers=new Map();
const world={crypto:webcrypto,URL,URLSearchParams,structuredClone,setTimeout,clearTimeout,DOMException,WebSocket,
  console:{error(){},warn(){}},location:{href:'https://engine-spike.invalid/test-source.html'},
  addEventListener(name,fn){if(!handlers.has(name))handlers.set(name,[]);handlers.get(name).push(fn);},
  dispatchEvent(e){for(const fn of handlers.get(e.type)||[])fn(e);},
  CustomEvent:class {constructor(type,options){this.type=type;Object.assign(this,options);}},
  chrome:{webview:{postMessage(m){sent.push(structuredClone(m));},addEventListener(name,fn){nativeHandlers.set(name,fn);}}}};
world.window=world;world.top=world;const context=vm.createContext(world);
vm.runInContext(shim.replace('__ENGINE_CONFIG__',JSON.stringify({manifest:{version:'gate'},tab:3})),context);
function answer(m,value,error){nativeHandlers.get('message')({data:{kind:'reply',id:m.id,frame:m.frame,value,error}});}
async function tick(){await new Promise(r=>setTimeout(r,0));}
let callback;
world.chrome.runtime.sendMessage({message:{chatmessage:'unchanged'}},v=>callback=v);
const capture=sent.at(-1);assert.equal(capture.method,'runtime.sendMessage');assert.equal(callback,undefined);answer(capture,{id:44});await tick();assert.equal(callback.id,44);
const stored=world.chrome.storage.local.get({state:false});const get=sent.at(-1);assert.equal(get.method,'storage.local.get');assert.deepEqual(get.args,[{state:false}]);answer(get,{state:true});assert.equal((await stored).state,true);
let errorDuring,errorAfter;
world.chrome.tabs.get(999,()=>errorDuring=world.chrome.runtime.lastError?.message);answer(sent.at(-1),undefined,'unknown tab');await tick();errorAfter=world.chrome.runtime.lastError;assert.equal(errorDuring,'unknown tab');assert.equal(errorAfter,undefined);
world.chrome.runtime.onMessage.addListener((m,sender,reply)=>{setTimeout(()=>reply({echo:m.value}),0);return true;});
nativeHandlers.get('message')({data:{kind:'runtime',frame:world.__engineSpike.frame,token:'async',message:{value:'async-value'},sender:{tab:{id:3}}}});await tick();assert(sent.some(m=>m.kind==='runtime-reply'&&m.token==='async'&&m.value.echo==='async-value'));
assert.throws(()=>new world.WebSocket('wss://io.socialstream.ninja/api'),/Cloud relay disabled/);
assert.equal(world.chrome.runtime.getURL('background.html'),'https://socialstream.ninja/background.html');assert.equal(world.ninjafy,undefined);
const up=[],down=[],listeners=new Map(),parent={};
const w={crypto:webcrypto,URLSearchParams,location:{search:'?room=gate&view=engine&label=poll&password=false'},console,
  addEventListener(name,fn){listeners.set(name,fn);},parent:{postMessage(m){down.push(m);}},
  __engineSpike:{rpc(method,args){up.push({method,args});return Promise.resolve(true);},send(m){up.push(m);}}};
w.window=w;vm.runInNewContext(bridge,w);await tick();assert.equal(up[0].method,'bridge.join');assert.equal(up[0].args[0].label,'poll');
listeners.get('message')({source:parent,data:{sendData:{overlayNinja:'spoof'}}});assert.equal(up.length,1);
listeners.get('message')({source:w.parent,data:{sendData:{overlayNinja:{vote:1}},type:'rpcs',UUID:'engine'}});assert.equal(up[1].args[1].type,'rpcs');assert.equal(up[1].args[1].UUID,'engine');
listeners.get('engine-spike-bridge')({detail:{dataReceived:{overlayNinja:false},UUID:'host'}});assert.equal(down[0].dataReceived.overlayNinja,false);
listeners.get('pagehide')();assert.equal(up.at(-1).kind,'bridge-leave');
console.log('Offline async shim / iframe protocol gates passed. Application testing has NOT been completed.');
