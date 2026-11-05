// Read platform-owned state and error panels. Never classify viewer message text.
(() => {
  const text = node => (node?.textContent || '').trim();
  const errorText = [...document.querySelectorAll('ytd-error-page-renderer, ytd-background-promo-renderer, ytd-alert-renderer, ytd-alert-with-button-renderer, #error-page, [data-a-target="core-error-message"], [data-e2e="live-end-title"], [data-e2e="user-not-found"], [data-testid="error-detail"]')].map(text).join(' ');
  const path = location.pathname;
  const platform = location.hostname;
  const state = {url: location.href};
  state.notFound = /channel (does not exist|is not available)|page (isn't|is not) available|page (could not|cannot) be found|404|doesn't exist|couldn.t find (this|that|the) (channel|account)|user not found/i.test(errorText);
  const visible = node => !!node?.getClientRects().length && getComputedStyle(node).visibility !== 'hidden';
  state.login = [...document.querySelectorAll('input[type="password"]')].some(visible) || /\/(accounts\/login|login|i\/flow\/login)(\/|$)/.test(path);
  state.ended = /live (has |stream has |stream )?ended|broadcast (has )?ended/i.test(errorText);
  state.offline = /not live|is offline|no live (stream|broadcast)/i.test(errorText);
  state.unavailable = /chat (is )?(disabled|unavailable)|video unavailable/i.test(errorText);
  if (platform.endsWith('youtube.com')) {
    // Chat documents don't expose the channel name or reliable broadcast status.
    // Fetch only watch HTML (no player, images or subresources), every two minutes, and
    // keep only the fields read below: the full response costs hundreds of MiB of heap.
    const video = new URL(location.href).searchParams.get('v');
    const cached = window.__studioVideoProbe;
    if (path === '/live_chat' && video && (!cached || cached.video !== video || Date.now() - cached.at > 120000)) {
      const cache = window.__studioVideoProbe = {video, at: Date.now(), data: cached?.video === video ? cached.data : null};
      fetch('/watch?v=' + encodeURIComponent(video), {credentials: 'omit', signal: AbortSignal.timeout(10000)})
        .then(r => r.ok ? r.text() : '').then(html => {
          const match = /(?:var\s+)?ytInitialPlayerResponse\s*=\s*(\{.*?\});\s*(?:<\/script>|var\s|const\s|let\s)/s.exec(html);
          if (match) { try {
            const full = JSON.parse(match[1]), d = full.videoDetails, l = full.microformat?.playerMicroformatRenderer?.liveBroadcastDetails;
            cache.data = {videoDetails: {author: d?.author, channelId: d?.channelId, videoId: d?.videoId, isLive: d?.isLive},
              microformat: {playerMicroformatRenderer: {liveBroadcastDetails: {isLiveNow: l?.isLiveNow, endTimestamp: l?.endTimestamp}}},
              playabilityStatus: {status: full.playabilityStatus?.status}};
          } catch {} }
        }).catch(() => {});
    }
    const player = window.ytInitialPlayerResponse || window.__studioVideoProbe?.data;
    const details = player?.videoDetails, live = player?.microformat?.playerMicroformatRenderer?.liveBroadcastDetails;
    state.name = details?.author;
    state.resolve = details?.channelId ? 'https://www.youtube.com/channel/' + details.channelId + '/live' : '';
    state.video = details?.videoId || video;
    state.live = live?.isLiveNow === true || details?.isLive === true;
    // YouTube also sends endTimestamp while a stream is live, so isLiveNow decides.
    state.ended ||= live?.isLiveNow === false && !!live?.endTimestamp;
    state.offline ||= player?.playabilityStatus?.status === 'LIVE_STREAM_OFFLINE';
    state.unavailable ||= ['ERROR','UNPLAYABLE'].includes(player?.playabilityStatus?.status);
    state.login ||= player?.playabilityStatus?.status === 'LOGIN_REQUIRED';
    const header = window.ytInitialData?.header;
    state.name ||= header?.c4TabbedHeaderRenderer?.title || header?.pageHeaderRenderer?.pageTitle;
    // A nonexistent handle usually produces a successful HTTP navigation with an error alert.
    const alerts = JSON.stringify(window.ytInitialData?.alerts || []);
    state.notFound ||= /does not exist|isn't available|is not available|couldn.t find|not found/i.test(alerts);
  }
  if (platform.endsWith('tiktok.com')) {
    state.login ||= [...document.querySelectorAll('[data-e2e="login-modal"], [data-e2e="login-modal-title"]')].some(visible);
    const script = document.getElementById('__UNIVERSAL_DATA_FOR_REHYDRATION__');
    try {
      const live = JSON.parse(script?.textContent || '{}').__DEFAULT_SCOPE__?.['webcast.app-context'];
      const room = live?.roomInfo?.room;
      state.offline ||= !!room && [3,4].includes(Number(room.status));
      state.notFound ||= [10202,10221].includes(Number(live?.statusCode));
    } catch {}
  }
  if (platform.endsWith('instagram.com')) {
    state.login ||= [...document.querySelectorAll('form[action*="login"]')].some(visible);
    state.offline ||= /live (video|broadcast) (has )?ended/i.test(text(document.querySelector('[role="dialog"]')));
  }
  if (platform.endsWith('rumble.com')) state.notFound ||= !!document.querySelector('img[src="/img/astronaut-404.png"]');
  return JSON.stringify(state);
})()
