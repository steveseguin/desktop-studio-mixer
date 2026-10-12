// Edge/WebView2 152-154 can lose hardware decoder capabilities when RTP color
// metadata reinitializes its hybrid decoder (WebView2Feedback #5755). Avoid that
// renegotiated extension on receiving sources, without changing codec order or
// disabling the hybrid decoder's HEVC support. The source dialog offers opt-out
// for pages that need the original RTP color metadata. Future runtimes retain
// their normal negotiation behavior.
(() => {
  const version = /\bEdg\/(\d+)\./.exec(navigator.userAgent);
  if (!version || +version[1] < 152 || +version[1] > 154 || !window.RTCPeerConnection) return;
  const colorSpace = 'http://www.webrtc.org/experiments/rtp-hdrext/color-space';
  function prepare(pc) {
    try {
      for (const transceiver of pc.getTransceivers()) {
        if (!transceiver.getHeaderExtensionsToNegotiate || !transceiver.setHeaderExtensionsToNegotiate) continue;
        const extensions = transceiver.getHeaderExtensionsToNegotiate();
        if (extensions.some(e => e.uri === colorSpace && e.direction !== 'stopped'))
          transceiver.setHeaderExtensionsToNegotiate(extensions.map(e =>
            e.uri === colorSpace ? {...e, direction: 'stopped'} : e));
      }
    } catch (_) { /* An unsupported runtime/page keeps its normal negotiation. */ }
  }
  const prototype = RTCPeerConnection.prototype;
  for (const method of ['createOffer', 'createAnswer', 'setLocalDescription']) {
    const original = prototype[method];
    prototype[method] = function (...args) {
      if (method !== 'setLocalDescription' || !args[0]) prepare(this);
      return original.apply(this, args);
    };
  }
})();
