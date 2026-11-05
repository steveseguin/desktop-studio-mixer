(()=>{const spike=window.__chatSpike={event(){}};
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
  // Hidden pages get no scroll events, and Kick's virtual chat list only renders new comments on one.
  if (window === window.top && location.hostname.endsWith('kick.com') && location.pathname.includes('/popout/'))
    setInterval(() => document.getElementById('chatroom-messages')?.dispatchEvent(new Event('scroll')), 100);
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
