// Main-world compatibility shim, installed BEFORE any manifest content script.
// No extension, Electron, sign-in, outbound chat, background service or frame pump.
(() => {
  if (window.__chatSpike) return;
  const nativePost = window.chrome?.webview?.postMessage.bind(window.chrome.webview);
  const topFrame = window === window.top;
  function post(packet) {
    packet.frameUrl = location.href;
    if (topFrame) nativePost?.(packet);
    else window.top.postMessage({ __chatSpikeRelay: packet }, '*');
  }
  // WebView2's document-created script runs in child frames too. Relaying avoids
  // depending on a particular Frame2 runtime revision for WebMessageReceived.
  if (topFrame) window.addEventListener('message', event => {
    if (event.source !== window && event.data?.__chatSpikeRelay) nativePost?.(event.data.__chatSpikeRelay);
  });
  const event = (name, detail = {}) => post({ kind: 'event', name, ...detail });
  function listeners() {
    const handlers = new Set();
    return {
      addListener: fn => handlers.add(fn), removeListener: fn => handlers.delete(fn),
      hasListener: fn => handlers.has(fn),
      fire: (...args) => { for (const fn of handlers) { try { fn(...args); } catch (e) { event('listener-error', { error: String(e) }); } } }
    };
  }
  // textonlymode makes the IRC comparison lossless for emote names and escaped HTML.
  // Everything else stays at SSN's defaults, so sources behave as they would in a release.
  const settings = { textonlymode: true };
  const onMessage = listeners(), onChanged = listeners();
  let nextId = 0;
  const seenRequests = new Set();
  function sendMessage(...args) {
    // A string message plus callback is the short form, not an extension ID.
    if (typeof args[0] === 'string' && args.length > 1 && typeof args[1] !== 'function'
        && (args[0] === 'chat-spike' || /^[a-p]{32}$/.test(args[0]))) args.shift();
    const request = args[0];
    const callback = typeof args.at(-1) === 'function' ? args.at(-1) : null;
    // Snapshot BEFORE replying. Do not add IDs, normalise, or mutate the payload.
    post({ kind: 'capture', payloadJson: JSON.stringify(request) ?? 'null' });
    const r = request && typeof request === 'object' ? request : {};
    const keys = Object.keys(r).sort().join(',');
    if (!seenRequests.has(keys)) { seenRequests.add(keys); event('runtime-request', { keys }); }
    let response = { state: true };
    // background.js getSettings and cmd:getSettings expose state + source settings.
    if ('getSettings' in r || r.cmd === 'getSettings') response = { state: true, settings: { ...settings }, streamID: '', password: '' };
    if (r.message && !r.message.id) response.id = ++nextId;
    queueMicrotask(() => {
      if (callback) callback(response);
      if (r.youtubeMessageRequestId && response.id) onMessage.fire({ youtubeMessageResponse: { requestId: r.youtubeMessageRequestId, id: response.id } }, {}, () => {});
    });
    // getBTTV/getSEVENTV/getFFZ only acknowledge state, with no emote broadcast.
    return callback ? undefined : Promise.resolve(response);
  }
  function area(name) {
    const data = {};
    const finish = (value, cb) => cb ? queueMicrotask(() => cb(value)) : Promise.resolve(value);
    return {
      get(keys, cb) {
        if (typeof keys === 'function') { cb = keys; keys = null; }
        const result = keys == null ? { ...data } : {};
        for (const key of typeof keys === 'string' ? [keys] : Array.isArray(keys) ? keys : Object.keys(keys || {})) {
          if (key in data) result[key] = data[key];
          else if (keys && !Array.isArray(keys) && typeof keys === 'object') result[key] = keys[key];
        }
        return finish(result, cb);
      },
      set(values, cb) {
        const changes = {};
        for (const [key, value] of Object.entries(values)) { changes[key] = { oldValue: data[key], newValue: value }; data[key] = value; }
        queueMicrotask(() => onChanged.fire(changes, name));
        return finish(undefined, cb);
      },
      remove(keys, cb) { for (const key of [keys].flat()) delete data[key]; return finish(undefined, cb); },
      clear(cb) { for (const key of Object.keys(data)) delete data[key]; return finish(undefined, cb); }
    };
  }
  // Extend, never replace: chrome.webview is the native host transport.
  window.chrome = window.chrome || {};
  window.chrome.runtime = Object.assign(window.chrome.runtime || {}, {
    id: 'chat-spike', lastError: undefined, sendMessage, onMessage,
    getURL: path => new URL(String(path).replace(/^\/+/, ''), 'https://ssn-resources.invalid/').href
  });
  window.chrome.storage = { local: area('local'), sync: area('sync'), onChanged };

  window.__chatSpike = { post, event };
  window.addEventListener('error', e => event('javascript-error', { error: e.message, file: e.filename, line: e.lineno }));
  window.addEventListener('unhandledrejection', e => event('promise-rejection', { error: String(e.reason) }));
  event('shim-ready', { topFrame });
})();
