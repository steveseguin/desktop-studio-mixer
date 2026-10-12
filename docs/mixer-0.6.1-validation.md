# Desktop Studio 0.6.1: default overlay backgrounds

Validated on Windows using the packaged Release application on 2026-10-11.
The original Game Capture project was not modified.

## Behavior

New browser sources and Social chat default to **Transparent overlay background**.
Overlay sources inject a top-level HTML/body background override before page
scripts run. Message cards, embedded documents and other element styling remain
intact. The existing opaque 30 fps Windows capture path remains available by
unchecking the option; saved source modes are preserved.

The override uses a constructed stylesheet and requires no polling or DOM
mutation observer. It is installed on each document creation, including reloads
and source reconnects. It does not expose a native host object, accept website
commands, or change the page's Content Security Policy. Browser process isolation
and permission/navigation restrictions remain in place. Social Stream Ninja
continues to provide its own custom CSS controls; this patch does not add a
general custom CSS editor to Desktop Studio.

This is a default CSS override of HTML/body backgrounds, not removal of every
opaque element in arbitrary pages. Backgrounds on other elements and stronger
site CSS overrides can still be opaque. Overlay capture retains its existing
15 fps snapshot ceiling; no new performance improvement is claimed.

## Packaged application workflows

`native-qt/e2e/mixer-overlay-background-e2e.py` exercised the real source dialog,
WebView2 worker, Preview/Take, Program canvas and recording pipeline against a
private loopback page with opaque HTML/body CSS and an opaque message card:

- New browser source had overlay mode enabled without changing the checkbox.
- HTML/body became transparent; the card and embedded document retained their
  backgrounds. Actual Preview and Program pixels showed the backing color layer.
- Reload, navigation, a late stylesheet, body replacement and source reconnect
  retained transparency.
- A page with `script-src 'none'` and `style-src 'self'` retained the override
  without changing its security headers.
- Switching to opaque mode restored the site's background and selected the
  Windows capture / 30 fps path. Reopening and saving its source dialog preserved
  that mode. Switching back restored the overlay behavior.
- An actual MKV recording decoded completely without FFmpeg errors. Its decoded
  frame contained both the backing layer and the opaque card at their expected
  colors, verifying the composed output rather than only browser computed styles.

`native-qt/e2e/mixer-chat-e2e.py` also exercised the real Social Stream Ninja
dock and featured overlay with a fresh private session and a synthetic incoming
message. Selecting the message displayed it in native Program; both the HTML and
body backgrounds were transparent. Clearing the featured message was sent through
the dock. No social account was authenticated and no public message was sent.

Private artifacts are under `native-qt/e2e/reports/overlay-background/` and are
not tracked. The application was closed after the workflows; the user's original
show and settings were restored byte-for-byte.

## Build gates and artifact

The Release build/package and whitespace gate passed. These are build gates,
separate from the application workflows above.

Packaged executable SHA-256:
`8e88362c35fe2dcab803fb357874f5498b173beb63cdad2ecf782e1b24fa760f`.

The portable archive and installer are generated as
`desktop-studio-0.6.1-windows-x64.zip` and `desktop-studio-0.6.1-setup.exe`.
The installer is rebuilt for this patch; the installation/upgrade workflow was
last exercised for 0.6.0. Protocol, hardware and broad failure-isolation results
remain documented in [0.6.0 validation](mixer-0.6.0-validation.md); those broad
workflows were not repeated for this CSS change.

Implementation reference: Microsoft's
[WebView2 document-created script API](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2#addscripttoexecuteondocumentcreated)
requires waiting for script registration before navigation. The browser source
does so and now reports registration failures without loading an uninitialized
source.
