/* Async extension boundary. No SSN function or source is patched. */
(() => {
  'use strict';
  const cfg = __ENGINE_CONFIG__, frame = crypto.randomUUID();
  const pending = new Map(), events = new Map(), children = new Map();
  const native = window.chrome?.webview;
  const topFrame = window === top;
  function send(packet) {
    packet.frame = frame; packet.url = location.href;
    if (topFrame || cfg.role === 'capture') native.postMessage(packet);
    else top.postMessage({engineSpikeUp: packet}, '*');
  }
  function log() {}
  function event(name) {
    const listeners = new Set(); events.set(name, listeners);
    return {addListener(fn) { log('api', {name: name+'.addListener'}); listeners.add(fn); },
      removeListener(fn) { listeners.delete(fn); }, hasListener(fn) { return listeners.has(fn); },
      hasListeners() { return !!listeners.size; }};
  }
  function rpc(method, args = [], callback) {
    const id = crypto.randomUUID();
    const p = new Promise((resolve, reject) => {
      const timer = setTimeout(() => { pending.delete(id); reject(new Error('Host RPC timeout: '+method)); }, 20000);
      pending.set(id, {resolve, reject, timer}); send({kind:'rpc', id, method, args});
    });
    if (callback) { p.then(v => callback(v), e => { chrome.runtime.lastError = {message:e.message}; try { callback(); } finally { chrome.runtime.lastError = undefined; } }); return; }
    return p;
  }
  function receive(m) {
    if (m.frame && m.frame !== frame) {
      if (topFrame && children.has(m.frame)) children.get(m.frame).postMessage({engineSpikeDown:m}, '*');
      return;
    }
    if (m.kind === 'reply') {
      const p = pending.get(m.id); if (!p) return;
      pending.delete(m.id); clearTimeout(p.timer); m.error ? p.reject(new Error(m.error)) : p.resolve(m.value);
    } else if (m.kind === 'event') {
      if (m.name.startsWith('host.')) window.dispatchEvent(new CustomEvent(m.name,{detail:m.args}));
      for (const fn of events.get(m.name) || []) { try { fn(...m.args); } catch(e) { log('listener-error', {message:String(e), stack:e.stack}); } }
    } else if (m.kind === 'runtime') {
      let answered = false, async = false;
      const reply = value => { if (!answered) { answered = true; send({kind:'runtime-reply', token:m.token, value}); } };
      for (const fn of events.get('runtime.onMessage') || []) {
        try {
          const result = fn(m.message, m.sender, reply);
          if (result === true) async = true;
          else if (result?.then) { async = true; result.then(reply, e => { log('listener-error',{message:String(e)}); reply(); }); }
        } catch(e) { log('listener-error', {message:String(e), stack:e.stack}); }
      }
      if (!async && !answered) reply();
    } else if (m.kind === 'bridge') window.dispatchEvent(new CustomEvent('engine-spike-bridge', {detail:m.packet}));
  }
  if (topFrame || cfg.role === 'capture') {
    native.addEventListener('message', e => receive(e.data));
    addEventListener('message', e => {
      if (!e.data?.engineSpikeUp || e.source === window) return;
      // Captures use the browser's frame messaging API; never relay page claims.
      if(cfg.role==='capture' || e.origin !== 'https://vdo.socialstream.ninja' || !['engine','host','overlay'].includes(cfg.role) || ![...document.querySelectorAll('iframe')].some(f=>f.contentWindow===e.source)) return;
      const m = e.data.engineSpikeUp;
      // Local bridge iframe and source subframes only; record the browser-observed origin.
      children.set(m.frame,e.source); m.frameOrigin = e.origin; native.postMessage(m);
    });
  } else addEventListener('message', e => { if (e.source === top && e.data?.engineSpikeDown) receive(e.data.engineSpikeDown); });
  const chrome = window.chrome ||= {};
  chrome.runtime = {
    id:'engine-spike-local', lastError:undefined,
    getURL(path='') { log('api',{name:'runtime.getURL',path}); return new URL(path,'https://socialstream.ninja/').href; },
    getManifest() { log('api',{name:'runtime.getManifest'}); return structuredClone(cfg.manifest); },
    onMessage:event('runtime.onMessage'), onConnect:event('runtime.onConnect'),
    sendMessage(...args) {
      const cb = typeof args.at(-1)==='function' ? args.pop() : undefined;
      if (typeof args[0] === 'string') args.shift();
      // Badges and avatars can embed whole images, enough to exceed the host's message limit. Send only what it reads.
      if (cfg.role === 'capture' && args[0]?.message) {
        const source=args[0].message;
        const message=Object.fromEntries(['chatname','chatmessage','hasDonation','membership','textonly','type','reply','initial','event'].filter(k => k in source).map(k => [k,source[k]]));
        // Keep bounded gift identity/counts, without transporting avatars, badge
        // images or arbitrary platform metadata through the native message pipe.
        if(source.meta)message.meta=Object.fromEntries(['tiktokGiftStreakId','tiktokGiftCount','giftName'].filter(k=>typeof source.meta[k]==='number'||typeof source.meta[k]==='string'&&source.meta[k].length<=256).map(k=>[k,source.meta[k]]));
        args[0]={message};
      }
      return rpc('runtime.sendMessage',[args[0]],cb);
    }
  };
  let runtimeError;
  Object.defineProperty(chrome.runtime,'lastError',{get(){log('api',{name:'runtime.lastError'});return runtimeError;},set(value){runtimeError=value;}});
  const method = name => (...args) => rpc(name,args,typeof args.at(-1)==='function' ? args.pop() : undefined);
  chrome.storage = {onChanged:event('storage.onChanged')};
  for (const area of ['local','sync']) {
    chrome.storage[area]={};
    for (const op of ['get','set','remove','clear','getBytesInUse']) chrome.storage[area][op]=method('storage.'+area+'.'+op);
  }
  chrome.tabs = {onUpdated:event('tabs.onUpdated'),onRemoved:event('tabs.onRemoved')};
  for (const op of ['query','get','getCurrent','sendMessage','update','create','remove']) chrome.tabs[op]=method('tabs.'+op);
  chrome.debugger={onDetach:event('debugger.onDetach'),onEvent:event('debugger.onEvent')};
  for (const op of ['attach','detach','sendCommand','getTargets']) chrome.debugger[op]=method('debugger.'+op);
  chrome.windows={update:method('windows.update'),getCurrent:method('windows.getCurrent')};
  chrome.notifications={create:method('notifications.create'),clear:method('notifications.clear')};
  chrome.action=chrome.browserAction={setIcon:method('action.setIcon'),setBadgeText:method('action.setBadgeText'),onClicked:event('action.onClicked')};
  // Observe unsupported feature probes without advertising invented functions.
  for(const name of ['runtime','tabs','debugger','windows','notifications','storage']){
    const object=chrome[name];chrome[name]=new Proxy(object,{get(target,key,receiver){
      if(typeof key==='string'&&!(key in target))log('api-unavailable-probe',{name:name+'.'+key});
      return Reflect.get(target,key,receiver);
    }});
  }
  window.__engineSpike={rpc,send,config:cfg,frame};
  // Absence preserves the extension feature-detection path. Reads are observable;
  // no fake Electron capabilities are advertised.
  for (const name of ['ninjafy','electronApi']) if (!(name in window)) Object.defineProperty(window,name,{configurable:true,get(){log('optional-bridge-read',{name});return undefined;}});
  addEventListener('error', e => log('js-error',{message:e.message,file:e.filename,line:e.lineno,stack:e.error?.stack}), true);
  addEventListener('unhandledrejection',e=>log('unhandled-rejection',{message:String(e.reason),stack:e.reason?.stack}));
  for (const level of ['error','warn']) {
    const original=console[level];console[level]=(...args)=>{log('console-'+level,{message:args.map(x=>{try{return typeof x==='string'?x:JSON.stringify(x);}catch{return String(x);}}).join(' ')});original.apply(console,args);};
  }
  const WS=window.WebSocket;
  window.WebSocket=class extends WS {
    constructor(url, protocols) {
      const u=new URL(url,location.href), blocked=/(^|\.)(socialstream\.ninja|vdo\.ninja)$/.test(u.hostname);
      log('websocket-attempt',{url:u.href,blocked});
      if (blocked) throw new DOMException('Cloud relay disabled by engine-spike','SecurityError');
      super(url,...(protocols===undefined?[]:[protocols]));
    }
  };
  send({kind:'document-ready'});
  if(topFrame && cfg.role==='overlay') {
    // featured.html schedules its real render callback after a fixed 500 ms
    // exit animation. Studio uses no transition; retain that callback and all
    // its arguments, but schedule our explicitly tagged cards immediately.
    const schedule=window.setTimeout.bind(window);
    window.setTimeout=(callback,delay,...args)=>schedule(callback,
      delay===500 && args[0]?.studioToken ? 0 : delay,...args);
    let expected;
    addEventListener('message', e=>{
      if(e.origin!=='https://vdo.socialstream.ninja' || ![...document.querySelectorAll('iframe')].some(f=>f.contentWindow===e.source))return;
      const card=e.data?.dataReceived?.overlayNinja;
      if(card?.studioToken){expected=card;delete document.documentElement.dataset.studioCard;}
    });
    setInterval(()=>{
      const output=document.getElementById('output'), message=output?.querySelector('.hl-message'), name=output?.querySelector('.hl-name');
      const expectedName=expected?new DOMParser().parseFromString(expected.chatname,'text/html').body.textContent:'';
      if(expected && output && message && name && getComputedStyle(output).opacity>.9 && message.textContent===expected.chatmessage && name.textContent.trim()===expectedName) {
        // Keep the complete Studio support label as plain text. The generic SSN
        // card otherwise chooses donation OR membership and adds platform titles.
        const parent=message.closest('.hl-c-cont');if(!parent)return;
        let support=parent.querySelector('.studio-support');
        if(expected.studioSupport){
          if(!support){support=document.createElement('div');support.className='studio-support';parent.append(support);}
          if(support.textContent!==expected.studioSupport)support.textContent=expected.studioSupport;
        }else support?.remove();
        document.documentElement.dataset.studioCard=expected.studioToken;
      }
    },16);
  }
})();
