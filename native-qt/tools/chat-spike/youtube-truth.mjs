// Read-only reference feed for a public YouTube live chat, using the same polling
// endpoint the chat page uses. No sign-in. Node 22+, no dependencies.
//   node youtube-truth.mjs --survey ID ID ... [--seconds 40]     messages per minute per video
//   node youtube-truth.mjs --video ID --out DIR --duration-min N write truth-youtube-ID.jsonl
//   node youtube-truth.mjs --compare DIR --video ID [--warmup 30] score captured messages against truth
import fs from 'node:fs';
import { join } from 'node:path';

const argv = process.argv.slice(2);
const flag = name => { const i = argv.indexOf('--' + name); return i < 0 ? undefined : argv[i + 1]; };
const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0 Safari/537.36';
const sleep = ms => new Promise(r => setTimeout(r, ms));
// The feed spells emoji as :shortcodes: while the page renders the characters, so
// compare letters and digits only. Emoji-only messages then match on the author.
const normalize = text => String(text ?? '').normalize('NFKC').replace(/:[\w-]+:/g, '').replace(/[^\p{L}\p{N}]/gu, '').toLowerCase();

async function open(video) {
  const html = await (await fetch(`https://www.youtube.com/live_chat?v=${video}`, { headers: { 'User-Agent': UA, 'Accept-Language': 'en' } })).text();
  const key = html.match(/"INNERTUBE_API_KEY":"([^"]+)"/)?.[1];
  const clientVersion = html.match(/"INNERTUBE_CLIENT_VERSION":"([^"]+)"/)?.[1] ?? html.match(/"clientVersion":"([^"]+)"/)?.[1];
  const start = html.indexOf('ytInitialData');
  if (!key || start < 0) throw new Error('no chat available');
  const open = html.indexOf('{', start);
  let depth = 0, end = open, inString = false;
  for (; end < html.length; end++) {
    const c = html[end];
    if (inString) { if (c === '\\') end++; else if (c === '"') inString = false; }
    else if (c === '"') inString = true;
    else if (c === '{') depth++;
    else if (c === '}' && --depth === 0) break;
  }
  const data = JSON.parse(html.slice(open, end + 1));
  const renderer = data?.contents?.liveChatRenderer;
  if (!renderer) throw new Error('no chat renderer');
  // The second view selector entry is full "Live chat", the first is "Top chat".
  const items = renderer.header?.liveChatHeaderRenderer?.viewSelector?.sortFilterSubMenuRenderer?.subMenuItems ?? [];
  const live = items[1]?.continuation?.reloadContinuationData?.continuation;
  const fallback = renderer.continuations?.[0];
  const continuation = live ?? Object.values(fallback ?? {})[0]?.continuation;
  if (!continuation) throw new Error('no continuation');
  return { key, clientVersion, continuation };
}

function parse(actions, seen) {
  const out = [];
  for (const action of actions ?? []) {
    const item = action.addChatItemAction?.item;
    const r = item?.liveChatTextMessageRenderer ?? item?.liveChatPaidMessageRenderer;
    if (!r || seen.has(r.id)) continue;
    seen.add(r.id);
    const text = (r.message?.runs ?? []).map(run => run.text ?? run.emoji?.shortcuts?.[0] ?? '').join('');
    out.push({ id: r.id, user: r.authorName?.simpleText ?? '', text, usec: Number(r.timestampUsec ?? 0) });
  }
  return out;
}

async function follow(video, ms, onMessage) {
  const session = await open(video);
  const seen = new Set();
  const stop = Date.now() + ms;
  let continuation = session.continuation, first = true;
  while (Date.now() < stop && continuation) {
    const res = await fetch(`https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?key=${session.key}&prettyPrint=false`, {
      method: 'POST', headers: { 'User-Agent': UA, 'Content-Type': 'application/json' },
      body: JSON.stringify({ context: { client: { clientName: 'WEB', clientVersion: session.clientVersion, hl: 'en' } }, continuation })
    });
    const body = await res.json();
    const chat = body?.continuationContents?.liveChatContinuation;
    if (!chat) break;
    const receivedMs = Date.now();
    // The first response is backfill, which capture correctly ignores too.
    const messages = parse(chat.actions, seen);
    if (!first) for (const m of messages) onMessage({ ...m, receivedMs });
    first = false;
    const next = Object.values(chat.continuations?.[0] ?? {})[0];
    continuation = next?.continuation;
    await sleep(Math.min(Math.max(next?.timeoutMs ?? 2000, 500), 5000));
  }
}

if (argv.includes('--survey')) {
  const ids = argv.slice(argv.indexOf('--survey') + 1).filter(a => /^[\w-]{11}$/.test(a));
  const seconds = Number(flag('seconds') ?? 40);
  const rows = await Promise.all(ids.map(async id => {
    let n = 0;
    try { await follow(id, seconds * 1000, () => n++); return { id, perMinute: Math.round(n * 60 / seconds) }; }
    catch (e) { return { id, error: e.message }; }
  }));
  rows.sort((a, b) => (b.perMinute ?? -1) - (a.perMinute ?? -1));
  console.log(JSON.stringify(rows));
} else if (argv.includes('--compare')) {
  const dir = flag('compare'), video = flag('video'), warmupMs = Number(flag('warmup') ?? 30) * 1000;
  const lines = file => fs.readFileSync(file, 'utf8').split('\n').filter(Boolean).map(l => JSON.parse(l));
  const truth = lines(join(dir, `truth-youtube-${video}.jsonl`));
  const captured = lines(join(dir, `messages-youtube-${video}.jsonl`)).filter(r => r.payload?.message?.chatname !== undefined || r.payload?.message?.chatmessage !== undefined);
  const key = (user, text) => normalize(String(user).replace(/^@/, '')) + '\u0000' + normalize(text);
  const start = (captured[0]?.receivedMs ?? truth[0].receivedMs) + warmupMs;
  const end = Math.min(truth.at(-1).receivedMs, captured.at(-1)?.receivedMs ?? Infinity) - 15000;
  const pool = new Map();
  for (const r of captured) {
    const k = key(r.payload.message.chatname, r.payload.message.chatmessage);
    if (!pool.has(k)) pool.set(k, []);
    pool.get(k).push(r.receivedMs);
  }
  const scored = truth.filter(t => t.receivedMs >= start && t.receivedMs <= end);
  let matched = 0; const latencies = [], missed = [];
  for (const t of scored) {
    const hits = pool.get(key(t.user, t.text));
    if (hits?.length) { matched++; latencies.push(hits.shift() - t.receivedMs); } else missed.push(`${t.user}: ${t.text}`.slice(0, 80));
  }
  latencies.sort((a, b) => a - b);
  const pct = p => latencies.length ? latencies[Math.floor((latencies.length - 1) * p)] : null;
  console.log(JSON.stringify({ video, reference: scored.length, captured: captured.length, matched, rate: scored.length ? matched / scored.length : null, latencyMsVsPoll: { p50: pct(0.5), p95: pct(0.95) }, missedSample: missed.slice(0, 6) }));
} else {
  const video = flag('video'), out = flag('out'), minutes = Number(flag('duration-min') ?? 5);
  fs.mkdirSync(out, { recursive: true });
  const stream = fs.createWriteStream(join(out, `truth-youtube-${video}.jsonl`), { flags: 'a' });
  await follow(video, minutes * 60000, m => stream.write(JSON.stringify(m) + '\n'));
  stream.end();
}
