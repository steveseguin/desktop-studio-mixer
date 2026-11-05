import { readFileSync, existsSync, createReadStream } from 'node:fs';
import { join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
const args = process.argv.slice(2), dir = args.shift();
if (!dir) throw Error('Usage: node analyze.mjs runDir [--control visibleRunDir] [--truth truthDir] [--window-sec 15] [--warmup-sec 30]');
const options = {};
for (let i = 0; i < args.length; i += 2) {
  if (!['--control', '--truth', '--window-sec', '--warmup-sec'].includes(args[i]) || args[i+1] === undefined) throw Error(`Invalid option ${args[i]}`);
  options[args[i]] = args[i+1];
}
const windowMs = Number(options['--window-sec'] ?? 15) * 1000, warmupMs = Number(options['--warmup-sec'] ?? 30) * 1000;
if (![windowMs, warmupMs].every(n => Number.isFinite(n) && n >= 0)) throw Error('Invalid window/warmup');
const root = resolve(dir), controlDir = options['--control'] ? resolve(options['--control']) : join(root, 'control');
const truthDir = options['--truth'] ? resolve(options['--truth']) : root;
const readJson = path => JSON.parse(readFileSync(path, 'utf8'));
const run = readJson(join(root, 'run.json'));
const control = existsSync(join(controlDir, 'run.json')) ? readJson(join(controlDir, 'run.json')) : null;
const warnings = [];
if (!run.endMs) warnings.push('Run has no endMs: incomplete/abrupt termination; duration inferred from metrics.');
const csv = readFileSync(join(root, 'metrics.csv'), 'utf8').trim().split(/\r?\n/);
const headers = csv.shift().split(',');
const metrics = csv.filter(Boolean).map(line => Object.fromEntries(line.split(',').map((v, i) => [headers[i], v === '' ? null : ['source', 'mode', 'visibility', 'reported_visibility'].includes(headers[i]) ? v : Number(v)])));
const endMs = run.endMs ?? Math.max(run.startMs, ...metrics.map(m => m.timestamp_ms));
function normalise(s) { return String(s ?? '').normalize('NFKC').replace(/[\u200B\uFEFF]/g, '').replace(/\s+/gu, ' ').trim(); }
function user(s) { return normalise(s).replace(/^@/, '').toLowerCase(); }
function htmlText(s) {
  const entities = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ' };
  return String(s).replace(/<img\b[^>]*\balt=["']([^"']*)["'][^>]*>/gi, '$1').replace(/<[^>]*>/g, '').replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (all, code) => {
    if (code[0] === '#') { const n = code[1].toLowerCase() === 'x' ? parseInt(code.slice(2), 16) : Number(code.slice(1)); return n <= 0x10ffff ? String.fromCodePoint(n) : all; }
    return entities[code] ?? all;
  });
}
async function messages(path) {
  const result = [];
  if (!existsSync(path)) return result;
  let lineNo = 0;
  for await (const line of createInterface({ input: createReadStream(path), crlfDelay: Infinity })) {
    lineNo++; if (!line) continue;
    let record;
    try { record = JSON.parse(line); } catch { warnings.push(`Malformed JSONL ${path}:${lineNo}`); continue; }
    const request = record.payload || {};
    for (const m of request.messages || [request.message]) {
      if (!m || m.event || typeof m.chatmessage !== 'string' || !(m.chatname || m.username)) continue;
      result.push({ time: record.receivedMs, users: [...new Set([m.username, m.chatname].filter(Boolean).map(user))],
        text: normalise(m.textonly ? m.chatmessage : htmlText(m.chatmessage)) });
    }
  }
  return result.sort((a, b) => a.time - b.time);
}
function stats(values) {
  const a = values.filter(v => typeof v === 'number' && Number.isFinite(v)).sort((a,b) => a-b);
  if (!a.length) return { n: 0, mean: null, p95: null, max: null };
  const quantile = p => a[Math.min(a.length - 1, Math.ceil(a.length * p) - 1)];
  return { n: a.length, mean: a.reduce((x,y) => x+y, 0) / a.length, p50: quantile(.5), p95: quantile(.95), p99: quantile(.99), max: a.at(-1), min: a[0] };
}
function silentGap(messages, start, end) {
  let previous = start, longest = 0;
  for (const m of messages) if (m.time >= start && m.time <= end) { longest = Math.max(longest, m.time-previous); previous = m.time; }
  return Math.max(longest, end-previous)/1000;
}
function compare(captured, truth, intervals) {
  // Only score complete matching windows inside concurrent, joined coverage.
  const inside = t => intervals.some(i => t >= i.startMs + windowMs && t <= i.endMs - windowMs);
  const eligible = truth.filter(m => inside(m.time)), scored = captured.filter(m => inside(m.time));
  const index = new Map();
  truth.forEach((m, i) => { for (const u of m.users) { const key = JSON.stringify([u, m.text]); if (!index.has(key)) index.set(key, []); index.get(key).push(i); } });
  const used = new Set(), matchedTruth = new Set();
  let matchedCapture = 0;
  const latency = [];
  // Include boundary captures in assignment; count only interior records.
  for (const m of captured) {
    let best = -1, distance = Infinity;
    for (const u of m.users) for (const i of index.get(JSON.stringify([u, m.text])) || []) {
      const delta = Math.abs(m.time-truth[i].time);
      if (!used.has(i) && delta <= windowMs && delta < distance) { best = i; distance = delta; }
    }
    if (best >= 0) {
      used.add(best); matchedTruth.add(truth[best]);
      if (inside(m.time)) matchedCapture++;
      if (inside(truth[best].time)) latency.push(m.time-truth[best].time);
    }
  }
  const matched = eligible.filter(m => matchedTruth.has(m)).length;
  return { referenceMessages: eligible.length, capturedMessages: scored.length, matched, captureRate: eligible.length ? matched / eligible.length : null,
    missedCount: eligible.length-matched, falseCount: scored.length-matchedCapture, latencyMs: stats(latency),
    scoredSeconds: intervals.reduce((n,i) => n+Math.max(0,i.endMs-i.startMs-2*windowMs)/1000,0),
    excludedReferenceMessages: truth.length-eligible.length, excludedCapturedMessages: captured.length-scored.length };
}
function growth(rows) {
  const valid = rows.filter(r => r.private_working_set_mib !== null && r.process_errors === 0 && r.process_count > 0);
  if (valid.length < 3) return { samples: valid.length, slopeMiBPerHour: null, direction: 'insufficient data' };
  const x = valid.map(r => (r.timestamp_ms-valid[0].timestamp_ms)/3600000), y = valid.map(r => r.private_working_set_mib);
  const mx = x.reduce((a,b)=>a+b,0)/x.length, my = y.reduce((a,b)=>a+b,0)/y.length;
  const slope = x.reduce((sum,v,i)=>sum+(v-mx)*(y[i]-my),0)/x.reduce((sum,v)=>sum+(v-mx)**2,0);
  const n = Math.max(1,Math.floor(y.length/4)), mean = a => a.reduce((s,v)=>s+v,0)/a.length;
  const delta = mean(y.slice(-n))-mean(y.slice(0,n));
  return { samples: valid.length, observedMinutes: (x.at(-1)-x[0])*60, slopeMiBPerHour: slope, lastMinusFirstQuartileMiB: delta,
    direction: delta > 1 && slope > 0 ? 'increasing during this run' : delta < -1 && slope < 0 ? 'decreasing during this run' : 'approximately flat/mixed',
    caveat: 'Includes startup/cache growth; a short run cannot establish a leak or long-term stability.' };
}
const report = { runDir: root, mode: run.mode, durationSeconds: (endMs-run.startMs)/1000, windowSeconds: windowMs/1000, warmupSeconds: warmupMs/1000, warnings, sources: {}, metrics: {} };
report.events = {};
if (existsSync(join(root, 'events.log'))) {
  for await (const line of createInterface({ input: createReadStream(join(root, 'events.log')), crlfDelay: Infinity })) {
    if (!line) continue;
    let e; try { e = JSON.parse(line); } catch { warnings.push('Malformed events.log line'); continue; }
    const summary = report.events[e.source] ??= { counts: {}, processFailures: [], apiErrors: [], injectedFiles: [] };
    summary.counts[e.event] = (summary.counts[e.event] || 0) + 1;
    if (e.event === 'process-failed') summary.processFailures.push({ kind: e.kind,
      kindName: ['browser-exited','renderer-exited','renderer-unresponsive','frame-renderer-exited','utility-exited','sandbox-helper-exited','gpu-exited','ppapi-plugin-exited','ppapi-broker-exited','unknown-exited'][e.kind] ?? 'unknown',
      reason: e.reason, exitCodeHex: e.exitCodeHex, description: e.description });
    if (e.event === 'api-error') summary.apiErrors.push({ operation: e.operation, hresult: e.hresult });
    if (e.event === 'injection-complete') for (const file of e.files || []) if (!summary.injectedFiles.includes(file)) summary.injectedFiles.push(file);
  }
}
for (const source of run.sources) {
  if (!report.events[source.id]?.counts['injection-complete']) warnings.push(`${source.id}: no completed content-script injection; this run cannot measure working capture.`);
  const captured = await messages(join(root, `messages-${source.id}.jsonl`));
  const summary = { totalCapturedChatMessages: captured.length, longestSilentGapSeconds: silentGap(captured,run.startMs,endMs) };
  if (source.spec.startsWith('twitch:')) {
    const metaPath = join(truthDir, `truth-${source.id}-meta.json`);
    if (!existsSync(metaPath)) summary.comparison = { unavailable: 'No Twitch truth metadata/log; no capture-rate claim possible.' };
    else {
      const meta = readJson(metaPath), truth = await messages(join(truthDir, `truth-${source.id}.jsonl`));
      const intervals = meta.intervals.map(i => ({ startMs: Math.max(run.startMs+warmupMs, i.startMs), endMs: Math.min(endMs,i.endMs ?? meta.endMs ?? endMs) })).filter(i=>i.endMs>i.startMs);
      summary.comparison = { reference: 'anonymous Twitch IRC', ...compare(captured,truth,intervals) };
      if (!intervals.length) warnings.push(`${source.id}: no concurrent joined IRC coverage`);
    }
  } else if (source.spec.startsWith('youtube:')) {
    const other = control?.sources.find(s => s.spec === source.spec);
    if (!other || control.mode !== 'visible') summary.comparison = { unavailable: 'Provide concurrent --control visibleRunDir (or runDir/control).' };
    else {
      const truth = await messages(join(controlDir, `messages-${other.id}.jsonl`));
      const startMs = Math.max(run.startMs,control.startMs)+warmupMs, stop = Math.min(endMs,control.endMs ?? endMs);
      summary.comparison = { reference: 'visible capture control, NOT API ground truth', ...compare(captured,truth,stop>startMs ? [{startMs,endMs:stop}] : []) };
    }
  }
  report.sources[source.id] = summary;
}
for (const source of new Set(metrics.map(m=>m.source))) {
  const rows = metrics.filter(m=>m.source===source), good = rows.filter(r=>r.process_errors===0 && r.process_count>0);
  report.metrics[source] = { samples: rows.length, incompleteProcessSamples: rows.filter(r=>r.process_errors>0).length,
    missingProbeSamples: rows.filter(r=>r.raf_per_s===null).length,
    rafPerSecond: stats(rows.map(r=>r.raf_per_s)), timerPerSecond: stats(rows.map(r=>r.timer_per_s)),
    cpuPercent: stats(good.map(r=>r.cpu_percent)), privateWorkingSetMiB: stats(good.map(r=>r.private_working_set_mib)),
    nativeVisibility: [...new Set(rows.map(r=>r.visibility).filter(Boolean))], reportedVisibility: [...new Set(rows.map(r=>r.reported_visibility).filter(Boolean))],
    blockedInputMaxPerDocument: Math.max(0,...rows.map(r=>r.blocked_input || 0)), memoryGrowth: growth(rows) };
}
// Intentionally no usernames or message text in this report.
console.log(JSON.stringify(report,null,2));
