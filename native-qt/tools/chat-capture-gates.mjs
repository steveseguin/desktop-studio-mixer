// Offline adapter contract gates. Packaged application testing is separate.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
const source=await fs.readFile(new URL('../resources/chat/capture.js',import.meta.url),'utf8');
function capture(href){
  const requests=[];
  const original=(...args)=>{requests.push(args);return Promise.resolve('forwarded');};
  const world={URL,Response,DOMException,location:new URL(href),fetch:original,setInterval(){},setTimeout:fn=>fn(),document:{readyState:'complete'}};
  world.window=world;world.top=world;
  vm.runInNewContext(source,world);
  let adapted;
  world.__chatSpike.register({matches:['https://*/*'],run_at:'document_start'},fetch=>{adapted=fetch;});
  assert.equal(world.fetch,original,'The platform fetch must remain unchanged');
  return {adapted,requests};
}
const {adapted,requests}=capture('https://kick.com/popout/channel_1/chat');
assert.deepEqual(await (await adapted('https://kick.com/channels/channel_1/Viewer-2')).json(),
  {profilepic:'https://studio-chat.invalid/kick-avatar/channel_1/Viewer-2'});
assert.equal(requests.length,0,'A deferred avatar must not make a browser request');
const abort=new AbortController();abort.abort();
await assert.rejects(adapted('https://kick.com/channels/channel_1/Viewer-2',{signal:abort.signal}),{name:'AbortError'});
for(const [input,init] of [
  ['https://kick.com/api/v2/channels/channel_1'],
  ['https://kick.com/channels/another/viewer'],
  ['https://kick.com.evil.test/channels/channel_1/viewer'],
  ['https://private@kick.com/channels/channel_1/viewer'],
  ['https://kick.com:8443/channels/channel_1/viewer'],
  ['https://kick.com/channels/channel_1/viewer?account=x'],
  ['https://kick.com/channels/channel_1/a%2Fb'],
  ['https://kick.com/channels/channel_1/viewer',{method:'POST',body:'untouched'}],
  ['http://[invalid'],
]) {
  const before=requests.length;
  assert.equal(await adapted(input,init),'forwarded');
  assert.equal(requests.length,before+1);
  assert.equal(requests.at(-1)[0],input);assert.equal(requests.at(-1)[1],init);
}
assert.equal(await capture('https://kick.com/channel_1').adapted('https://kick.com/channels/channel_1/viewer'),'forwarded');
assert.equal(await capture('https://www.twitch.tv/popout/channel_1/chat').adapted('https://kick.com/channels/channel_1/viewer'),'forwarded');
console.log('Offline capture adapter gates passed; this does not replace packaged application testing.');
