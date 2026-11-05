// Node >=22 with global WebSocket. Anonymous IRC, no npm and no chat sending API.
import { createWriteStream, existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { randomInt } from 'node:crypto';
const args = process.argv.slice(2), options = {};
for (let i = 0; i < args.length; i += 2) {
  if (!['--channel', '--out', '--duration-min'].includes(args[i]) || !args[i + 1]) throw Error('Usage: node twitch-truth.mjs --channel channel --out runDir [--duration-min N]');
  options[args[i]] = args[i + 1];
}
const channel = (options['--channel'] || '').toLowerCase();
if (!/^[a-z0-9_]+$/.test(channel) || !options['--out']) throw Error('Require --channel and --out');
if (typeof WebSocket !== 'function') throw Error('Node >=22 with global WebSocket required');
const duration = options['--duration-min'] === undefined ? 0 : Number(options['--duration-min']);
if (!Number.isFinite(duration) || duration < 0) throw Error('Invalid duration');
const out = resolve(options['--out']); mkdirSync(out, { recursive: true });
const source = `twitch-${channel}`, stem = join(out, `truth-${source}`);
if (existsSync(`${stem}.jsonl`) || existsSync(`${stem}-meta.json`)) throw Error('Truth logs already exist; use a new run directory');
const messages = createWriteStream(`${stem}.jsonl`, { flags: 'wx' });
const events = createWriteStream(`${stem}-events.jsonl`, { flags: 'wx' });
for (const stream of [messages, events]) stream.on('error', e => { console.error(e); process.exit(2); });
const metadata = { startMs: Date.now(), channel, intervals: [], messageCount: 0 };
let socket, reconnect, stopping = false, interval, connected = false;
function record(stream, payload) {
  const receivedMs = Date.now();
  stream.write(JSON.stringify({ receivedMs, receivedAt: new Date(receivedMs).toISOString(), source, payload }) + '\n');
}
function save() { writeFileSync(`${stem}-meta.json`, JSON.stringify(metadata, null, 2)); }
function closeInterval() { if (interval) { interval.endMs = Date.now(); interval = null; save(); } }
function unescapeTag(s = '') { return s.replace(/\\([snr: ])/g, (_, c) => ({ s: ' ', n: '\n', r: '\r', ':': ';', ' ': ' ' }[c])).replace(/\\\\/g, '\\'); }
function line(rawIRC) {
  if (rawIRC.startsWith('PING ')) { socket.send(rawIRC.replace(/^PING/, 'PONG') + '\r\n'); return; }
  let text = rawIRC, tags = {};
  if (text.startsWith('@')) {
    const end = text.indexOf(' ');
    tags = Object.fromEntries(text.slice(1, end).split(';').map(s => { const i = s.indexOf('='); return [i < 0 ? s : s.slice(0, i), unescapeTag(i < 0 ? '' : s.slice(i + 1))]; }));
    text = text.slice(end + 1);
  }
  const parsed = /^(?::([^ ]+) )?([^ ]+)(?: (.*))?$/.exec(text);
  if (!parsed) return;
  const [, prefix = '', command, rest = ''] = parsed;
  if (command === '366' && !connected) {
    connected = true; interval = { startMs: Date.now(), endMs: null }; metadata.intervals.push(interval);
    record(events, { event: 'joined', channel }); save();
  }
  if (command === 'PRIVMSG') {
    const match = /^#([^ ]+) :(.*)$/.exec(rest);
    if (!match || match[1].toLowerCase() !== channel) return;
    // ACTION is a presentation wrapper, not part of visible message text.
    const chatmessage = match[2].replace(/^\x01ACTION (.*)\x01$/, '$1');
    record(messages, { message: { type: 'twitch', username: prefix.split('!')[0], chatname: tags['display-name'] || prefix.split('!')[0], chatmessage, textonly: true, id: tags.id }, tags, rawIRC });
    metadata.messageCount++;
  } else if (!['JOIN', 'PART', '353', '366', '001', '002', '003', '004', '375', '372', '376', 'CAP', 'ROOMSTATE', 'USERSTATE', 'GLOBALUSERSTATE'].includes(command)) {
    record(events, { event: 'irc', rawIRC });
    if (command === 'RECONNECT') socket.close();
  }
}
function connect() {
  if (stopping) return;
  connected = false;
  socket = new WebSocket('wss://irc-ws.chat.twitch.tv:443');
  let pending = '';
  socket.addEventListener('open', () => {
    record(events, { event: 'connected' });
    // Only protocol handshake, membership and PONG are ever sent. No PRIVMSG.
    socket.send('CAP REQ :twitch.tv/tags twitch.tv/commands\r\n');
    socket.send(`NICK justinfan${randomInt(10000, 999999999)}\r\n`);
    socket.send(`JOIN #${channel}\r\n`);
  });
  socket.addEventListener('message', e => {
    if (stopping) return;
    pending += String(e.data);
    const lines = pending.split('\r\n'); pending = lines.pop();
    for (const raw of lines) if (raw) line(raw);
  });
  socket.addEventListener('error', e => { if (!stopping) record(events, { event: 'error', error: e.message || String(e.error || e) }); });
  socket.addEventListener('close', e => {
    if (stopping) return;
    record(events, { event: 'closed', code: e.code, reason: e.reason }); closeInterval();
    if (!stopping) reconnect = setTimeout(connect, 3000);
  });
}
function stop() {
  if (stopping) return;
  stopping = true; clearTimeout(reconnect); clearTimeout(deadline); closeInterval();
  metadata.endMs = Date.now(); save();
  record(events, { event: 'end', messageCount: metadata.messageCount });
  socket?.close(); messages.end(); events.end();
  console.log(JSON.stringify({ channel, messages: metadata.messageCount, joinedIntervals: metadata.intervals.length }));
  setTimeout(() => process.exit(metadata.intervals.length ? 0 : 2), 1000).unref();
}
const deadline = duration ? setTimeout(stop, duration * 60000) : null;
process.on('SIGINT', stop); process.on('SIGTERM', stop);
save(); connect();
