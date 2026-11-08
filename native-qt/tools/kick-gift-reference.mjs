// Public, read-only gift reference. Does not authenticate, post, or purchase.
// node tools/kick-gift-reference.mjs CHANNEL SECONDS FRESH_OUTPUT_DIRECTORY
import fs from 'node:fs';
import path from 'node:path';
const [channel, duration, output] = process.argv.slice(2);
const seconds = Number(duration);
if (!/^[a-z0-9_-]{1,50}$/i.test(channel || '') || !Number.isFinite(seconds) || seconds < 30 || seconds > 7200 || !output)
  throw Error('Provide a public channel, 30–7200 seconds, and a fresh output directory');
fs.mkdirSync(output, {recursive: false});
const response = await fetch(`https://kick.com/api/v2/channels/${channel}`, {
  headers: {Accept: 'application/json'}, signal: AbortSignal.timeout(15000)
});
if (!response.ok) throw Error(`Public channel lookup returned HTTP ${response.status}`);
const info = await response.json();
if (!Number.isSafeInteger(info.id) || !Number.isSafeInteger(info.chatroom?.id) || !info.livestream)
  throw Error('The selected channel must have a live broadcast and public channel/chatroom identifiers');
// Ordinary messages and KICKs use different public topics. Subscribing only to
// chatrooms.* cannot independently verify gifts observed in the page's DOM.
const topics = [`chatrooms.${info.chatroom.id}.v2`, `channel_${info.id}`];
const subscribed = new Set(), counts = {}, errors = [];
const paidGiftEvents = new Set(['KicksGifted', 'App\\Events\\GiftedSubscriptionsEvent', 'App\\Events\\GiftPurchaseEvent']);
const startedMs = Date.now();
let lost = false, giftEvents = 0, giftRelatedFrames = 0, subscribedAtMs = null;
const gifts = fs.createWriteStream(path.join(output, 'gifts.jsonl'));
gifts.on('error', () => { lost = true; errors.push('Public gift ledger could not be written'); });
const socket = new WebSocket('wss://ws-us2.pusher.com/app/34bf7a0ff419a2a775b9?protocol=7&client=js&version=8.4.0&flash=false');
const send = (event, data) => { if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({event, data})); };
socket.onmessage = event => {
  try {
    const message = JSON.parse(event.data);
    counts[message.event] = (counts[message.event] || 0) + 1;
    if (message.event === 'pusher:connection_established')
      for (const topic of topics) send('pusher:subscribe', {channel: topic, auth: ''});
    if (message.event === 'pusher:ping') send('pusher:pong', {});
    if (message.event === 'pusher_internal:subscription_succeeded') {
      subscribed.add(message.channel);
      if (!subscribedAtMs && topics.every(topic => subscribed.has(topic))) subscribedAtMs = Date.now();
    }
    if (message.event === 'pusher:error') { lost = true; errors.push('Pusher reported an error'); }
    if (/gift/i.test(message.event) && topics.includes(message.channel)) {
      const data = typeof message.data === 'string' ? JSON.parse(message.data) : message.data;
      gifts.write(JSON.stringify({receivedMs: Date.now(), event: message.event, topic: message.channel, data}) + '\n');
      giftRelatedFrames++;
      if (paidGiftEvents.has(message.event)) giftEvents++;
    }
  } catch { lost = true; errors.push('A public reference frame could not be parsed'); }
};
socket.onclose = () => { lost = true; };
socket.onerror = () => { lost = true; errors.push('Public socket error'); };
const summary = complete => ({channel, channelId: info.id, chatroomId: info.chatroom.id,
  viewersAtStart: info.livestream.viewer_count, startedMs, subscribedAtMs, endedMs: Date.now(),
  complete, referenceHeld: !lost && topics.every(topic => subscribed.has(topic)),
  subscriptions: [...subscribed], counts, giftEvents, giftRelatedFrames, errors,
  meaning: 'Public reference events only. Match individual authors, amounts and times to Studio before claiming live gift delivery.'});
const heartbeat = setInterval(() => send('pusher:ping', {}), 25000);
const checkpoint = setInterval(() => fs.writeFileSync(path.join(output, 'summary.json'), JSON.stringify(summary(false), null, 2)), 10000);
try {
  while (Date.now() - startedMs < seconds * 1000 && !fs.existsSync(path.join(output, 'stop')))
    await new Promise(resolve => setTimeout(resolve, 1000));
} finally {
  clearInterval(heartbeat); clearInterval(checkpoint);
  socket.onclose = null; socket.close();
  if (!gifts.destroyed) await new Promise(resolve => {
    gifts.once('error', resolve);
    gifts.end(resolve);
  });
  const result = summary(true);
  fs.writeFileSync(path.join(output, 'summary.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify({channel, giftEvents, referenceHeld: result.referenceHeld, output}));
  if (!result.referenceHeld) process.exitCode = 1;
}
