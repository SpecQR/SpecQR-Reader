#!/usr/bin/env python3
"""Serve this trial on IPv4 loopback only; no uploads or decoder on the server."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Referrer-Policy', 'no-referrer')
        super().end_headers()
    def log_message(self, fmt, *args):
        pass

def main():
    parser = argparse.ArgumentParser(description='SpecQR Reader local trial')
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    server = ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=str(root)))
    print(f'この Mac のブラウザで開いてください: http://127.0.0.1:{server.server_port}/', flush=True)
    print('終了: Control+C。公開・LAN 配信はしません。', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
