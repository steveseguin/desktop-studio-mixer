// Runs before page scripts, only in receiving sources. No native device access.
(() => {
  if (!window.AudioContext || window.__videoMixerAudio) return;
  window.__videoMixerAudio = true;
  const connect = AudioNode.prototype.connect;
  const disconnect = AudioNode.prototype.disconnect;
  const createElement = AudioContext.prototype.createMediaElementSource;
  const destinations = new WeakMap();
  const elements = new WeakMap();
  const streams = new WeakMap();
  const watchedStreams = new WeakSet();
  const warnedElements = new WeakMap();
  let mixer, input, processor;
  const send = data => {
    if (window === window.top) window.chrome?.webview?.postMessage(data);
    else window.top.postMessage({videoMixerAudio: data}, '*');
  };
  if (window === window.top) addEventListener('message', event => {
    const data = event.data?.videoMixerAudio;
    if (data?.type === 'mixer-audio' && typeof data.pcm === 'string' && data.pcm.length <= 32768)
      window.chrome?.webview?.postMessage(data);
  });
  function initialize() {
    if (mixer) return;
    mixer = new AudioContext({sampleRate: 48000, latencyHint: 'interactive'});
    input = mixer.createGain();
    // A bounded 1024-frame callback also works when a page's CSP blocks worklet blobs.
    processor = mixer.createScriptProcessor(1024, 2, 2);
    processor.onaudioprocess = event => {
      const left = event.inputBuffer.getChannelData(0);
      const right = event.inputBuffer.numberOfChannels > 1 ? event.inputBuffer.getChannelData(1) : left;
      const pcm = new Int16Array(left.length * 2);
      let peak = 0;
      for (let i = 0; i < left.length; ++i) {
        pcm[i * 2] = Math.max(-1, Math.min(1, left[i])) * 32767;
        pcm[i * 2 + 1] = Math.max(-1, Math.min(1, right[i])) * 32767;
        peak = Math.max(peak, Math.abs(pcm[i * 2]), Math.abs(pcm[i * 2 + 1]));
      }
      // The output buffer stays zero: playback happens only on the native monitor bus.
      if (!peak) return;
      const bytes = new Uint8Array(pcm.buffer);
      let binary = '';
      for (const byte of bytes) binary += String.fromCharCode(byte);
      send({type: 'mixer-audio', rate: mixer.sampleRate, pcm: btoa(binary)});
    };
    connect.call(input, processor); connect.call(processor, mixer.destination);
    mixer.resume().catch(() => {});
  }
  function route(context) {
    initialize();
    if (context === mixer) return input;
    if (!destinations.has(context)) {
      const destination = context.createMediaStreamDestination();
      const source = mixer.createMediaStreamSource(destination.stream);
      connect.call(source, input);
      destinations.set(context, {destination, source});
    }
    return destinations.get(context).destination;
  }
  AudioNode.prototype.connect = function (target, ...args) {
    if (target instanceof AudioDestinationNode) {
      connect.call(this, route(target.context), ...args);
      return target;
    }
    return connect.call(this, target, ...args);
  };
  AudioNode.prototype.disconnect = function (...args) {
    if (args[0] instanceof AudioDestinationNode) args[0] = route(args[0].context);
    return disconnect.apply(this, args);
  };
  AudioContext.prototype.createMediaElementSource = function (element) {
    if (elements.has(element)) return elements.get(element);
    const node = createElement.call(this, element);
    elements.set(element, node);
    return node;
  };
  function captureElement(element) {
    try {
      // A normally playable cross-origin element can become silent when Web
      // Audio reads it. Tell the operator instead of silently accepting it.
      if (!element.srcObject) {
        const url = new URL(element.currentSrc || element.src || location.href, location.href);
        if (/^https?:$/.test(url.protocol) && url.origin !== location.origin &&
            !element.crossOrigin && warnedElements.get(element) !== url.href) {
          warnedElements.set(element, url.href);
          send({type:'mixer-audio-error', reason:'cross-origin'});
        }
      }
      // Chromium's MediaElementAudioSource does not reliably expose remote
      // WebRTC audio. Tap its MediaStream and retain the element's mute/volume.
      if (element.srcObject instanceof MediaStream) {
        const stream = element.srcObject;
        if (!watchedStreams.has(stream)) {
          watchedStreams.add(stream);
          stream.addEventListener('addtrack', () => captureElement(element));
        }
        if (!stream.getAudioTracks().length) return;
        if (streams.get(element)?.stream === stream) { streams.get(element).update(); return; }
        initialize();
        const old = streams.get(element);
        if (old) { disconnect.call(old.source); disconnect.call(old.gain); old.removeListeners(); }
        if (!elements.has(element)) elements.set(element, createElement.call(mixer, element));
        const source = mixer.createMediaStreamSource(stream), gain = mixer.createGain();
        const update = () => { gain.gain.value = element.muted || element.paused ? 0 : element.volume; };
        const events = ['volumechange', 'playing', 'pause', 'ended'];
        events.forEach(event => element.addEventListener(event, update));
        streams.set(element, {stream, source, gain, update, removeListeners: () => events.forEach(event => element.removeEventListener(event, update))});
        connect.call(source, gain); connect.call(gain, input); update();
        return;
      }
      if (elements.has(element)) return;
      initialize();
      const node = createElement.call(mixer, element);
      elements.set(element, node); connect.call(node, input);
    } catch (error) {
      send({type: 'mixer-audio-error', message: 'This page owns an unsupported media audio path. Check its audio meter.'});
    }
  }
  const play = HTMLMediaElement.prototype.play;
  HTMLMediaElement.prototype.play = function (...args) {
    captureElement(this);
    return play.apply(this, args);
  };
  addEventListener('play', event => {
    if (event.target instanceof HTMLMediaElement) captureElement(event.target);
  }, true);
  addEventListener('loadedmetadata', event => {
    if (event.target instanceof HTMLMediaElement) captureElement(event.target);
  }, true);
  // WebRTC pages can replace srcObject/tracks while an element is already playing,
  // without another play or metadata event. Reconcile these bounded DOM sources.
  setInterval(() => document.querySelectorAll('video,audio').forEach(captureElement), 250);
})();
