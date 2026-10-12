// Only overlay sources load this script. Leave embedded documents and all
// message/card styling alone. A constructed stylesheet works before the DOM is
// parsed, survives body replacement, and needs no polling or mutation observer.
(() => {
  if (window !== window.top) return;
  const background = new CSSStyleSheet();
  background.replaceSync(':root, body { background: transparent !important; }');
  document.adoptedStyleSheets = [...document.adoptedStyleSheets, background];
})();
