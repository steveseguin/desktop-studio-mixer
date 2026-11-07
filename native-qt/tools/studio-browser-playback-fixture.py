"""Serve one synthetic clip on loopback for actual browser-source playback journeys."""
import argparse
import http.server
import json
from pathlib import Path
import socketserver

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--file', required=True)
p.add_argument('--destination', required=True, help='New destination JSON for studio-receiver-cadence-workflow.ps1')
a = p.parse_args()
source = Path(a.file).resolve(strict=True)
destination = Path(a.destination)
if destination.exists():
    raise SystemExit('Use a new destination file')
page = b'''<!doctype html><meta charset="utf-8"><title>Browser playback reference</title>
<style>body{margin:0;background:black}video{position:fixed;width:100%;height:100%;object-fit:fill}nav{position:fixed;bottom:0;right:0}button{padding:10px}</style>
<video id="v" src="/reference.mp4" autoplay loop playsinline></video><nav><button onclick="v.pause()">Pause video</button><button onclick="v.play()">Resume video</button><button onclick="v.currentTime=0">Restart video</button></nav>'''


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/':
            data, kind = page, 'text/html'
        elif self.path == '/reference.mp4':
            data, kind = source.read_bytes(), 'video/mp4'
        else:
            self.send_error(404)
            return
        start, end, partial = 0, len(data) - 1, False
        if self.headers.get('Range', '').startswith('bytes='):
            try:
                spec = self.headers['Range'][6:].split('-')
                start, end = int(spec[0] or 0), min(int(spec[1] or end), end)
                if start < 0 or end < start:
                    raise ValueError()
                partial = True
            except (ValueError, IndexError):
                self.send_error(416)
                return
        self.send_response(206 if partial else 200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(end - start + 1))
        self.send_header('Accept-Ranges', 'bytes')
        if partial:
            self.send_header('Content-Range', f'bytes {start}-{end}/{len(data)}')
        self.end_headers()
        try:
            self.wfile.write(data[start:end + 1])
        except (BrokenPipeError, ConnectionResetError):
            pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


with Server(('127.0.0.1', 0), Handler) as server:
    destination.write_text(json.dumps({'viewer': f'http://127.0.0.1:{server.server_port}/'}))
    print('Local synthetic video fixture ready', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
