// Node 22 built-ins only. Each client is tied to one target from our own process.
export const delay = ms => new Promise(resolve=>setTimeout(resolve,ms));
export async function until(fn,{timeout=15000,description='condition'}={}) {
  const end=Date.now()+timeout;let last;
  do {try {const value=await fn();if(value)return value;}catch(e){last=e;}await delay(150);}while(Date.now()<end);
  throw new Error(`Timed out waiting for ${description}${last?': '+last.message:''}`);
}
export class CDP {
  constructor(url){this.ws=new WebSocket(url);this.serial=0;this.pending=new Map();this.events=[];
    this.ready=new Promise((resolve,reject)=>{this.ws.addEventListener('open',resolve,{once:true});this.ws.addEventListener('error',()=>reject(new Error('DevTools connection failed')),{once:true});});
    this.ws.addEventListener('message',e=>{const m=JSON.parse(e.data);if(m.id){const p=this.pending.get(m.id);if(p){this.pending.delete(m.id);clearTimeout(p.timer);m.error?p.reject(new Error(JSON.stringify(m.error))):p.resolve(m.result);}}else this.events.push(m);});
    this.ws.addEventListener('close',()=>{for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(new Error('DevTools target closed'));}this.pending.clear();});
  }
  async call(method,params={},timeout=20000){await this.ready;const id=++this.serial;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{this.pending.delete(id);reject(new Error('DevTools timeout: '+method));},timeout);this.pending.set(id,{resolve,reject,timer});this.ws.send(JSON.stringify({id,method,params}));});}
  async evaluate(expression){const r=await this.call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true,userGesture:true});if(r.exceptionDetails)throw new Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result?.value;}
  async state(){return this.evaluate(`({url:location.href,title:document.title,ready:document.readyState,text:document.body?.innerText||'',elementCount:document.querySelectorAll('*').length,iframes:[...document.querySelectorAll('iframe')].map(f=>f.src)})`);}
  close(){this.ws.close();}
}
