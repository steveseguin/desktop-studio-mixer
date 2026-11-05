// Read-only reference count for a public Kick chat, using the public socket Kick's page uses.
//   node kick-truth.mjs CHANNEL SECONDS [OUT.jsonl]
import fs from 'node:fs';
const [channel,seconds='60',out]=process.argv.slice(2);
const UA='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0 Safari/537.36';
const info=await (await fetch(`https://kick.com/api/v2/channels/${channel}`,{headers:{'User-Agent':UA,Accept:'application/json'}})).json();
const id=info?.chatroom?.id;
if(!id){console.log(JSON.stringify({channel,error:'no chatroom id'}));process.exit(1);}
const stream=out?fs.createWriteStream(out):null;
const ws=new WebSocket('wss://ws-us2.pusher.com/app/34bf7a0ff419a2a775b9?protocol=7&client=js&version=8.4.0&flash=false');
let count=0;
ws.onmessage=e=>{
  const m=JSON.parse(e.data);
  if(m.event==='pusher:connection_established')ws.send(JSON.stringify({event:'pusher:subscribe',data:{channel:`chatrooms.${id}.v2`,auth:''}}));
  if(/ChatMessageEvent/.test(m.event)){count++;if(stream){const d=JSON.parse(m.data);stream.write(JSON.stringify({receivedMs:Date.now(),user:d.sender?.username,text:d.content})+'\n');}}
};
await new Promise(r=>setTimeout(r,Number(seconds)*1000));
ws.close();stream?.end();
console.log(JSON.stringify({channel,chatroom:id,seconds:Number(seconds),messages:count,viewers:info?.livestream?.viewer_count??null}));
