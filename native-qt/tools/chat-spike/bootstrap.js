// Native APIs are captured before upstream scripts can override visibility/rAF.
(() => {
  const spike = window.__chatSpike;
  const nativeVisibility = Object.getOwnPropertyDescriptor(Document.prototype, 'visibilityState')?.get;
  const raf = window.requestAnimationFrame.bind(window), interval = window.setInterval.bind(window);
  if (window === window.top) {
    const epoch = performance.timeOrigin;
    let rafCount = 0, timerCount = 0;
    function frame() { rafCount++; raf(frame); }
    raf(frame);
    interval(() => timerCount++, 100);
    interval(() => spike.post({ kind: 'probe', epoch, elapsedMs: performance.now(), rafCount, timerCount,
      visibility: nativeVisibility ? nativeVisibility.call(document) : 'unknown',
      reportedVisibility: document.visibilityState }), 1000);
  }
  function wildcard(pattern, value) {
    return new RegExp('^' + pattern.split('*').map(s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('.*') + '$').test(value);
  }
  function match(pattern, href) {
    const url = new URL(href);
    if (pattern === '<all_urls>') return ['http:', 'https:', 'file:', 'ftp:'].includes(url.protocol);
    const parts = /^(\*|https?|file|ftp):\/\/([^/]*)(\/.*)$/.exec(pattern);
    if (!parts) return false;
    if (parts[1] === '*' ? !['http:', 'https:'].includes(url.protocol) : url.protocol !== parts[1] + ':') return false;
    const host = parts[2].toLowerCase();
    if (host !== '*' && !(host.startsWith('*.') ? url.hostname === host.slice(2) || url.hostname.endsWith(host.slice(1)) : url.host === host)) return false;
    return wildcard(parts[3], url.pathname + url.search);
  }
  spike.register = function (entry, run) {
    if (!entry.all_frames && window !== window.top) return;
    let href = location.href;
    if (href === 'about:blank' && entry.match_about_blank) {
      try { href = parent.location.href; } catch { return; }
    }
    if (!(entry.matches || []).some(p => match(p, href))) return;
    if ((entry.exclude_matches || []).some(p => match(p, href))) return;
    if (entry.include_globs && !entry.include_globs.some(p => wildcard(p, href))) return;
    if ((entry.exclude_globs || []).some(p => wildcard(p, href))) return;
    const execute = () => {
      spike.event('injection-start', { files: entry.js, runAt: entry.run_at || 'document_idle', allFrames: !!entry.all_frames });
      try { run(); spike.event('injection-complete', { files: entry.js }); }
      catch (e) { spike.event('injection-error', { files: entry.js, error: String(e), stack: e.stack }); }
    };
    if (entry.run_at === 'document_start') execute();
    else if (entry.run_at === 'document_end') {
      if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', execute, { once: true });
      else execute();
    } else {
      // Deterministic document_idle approximation: next task after window.load.
      if (document.readyState === 'complete') setTimeout(execute, 0);
      else window.addEventListener('load', () => setTimeout(execute, 0), { once: true });
    }
  };
})();
