"""Token-authenticated loopback launcher. Run with python -m launcher."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import sys
from urllib.parse import urlsplit
import webbrowser
from .library import Library, ROOT
from .picker import pick_iso

ASSETS = ROOT / 'launcher' / 'web'
FILES = {'/': ('index.html', 'text/html'), '/app.css': ('app.css', 'text/css'),
         '/app.js': ('app.js', 'text/javascript'), '/emblem.svg': ('emblem.svg', 'image/svg+xml')}


def build_defaults():
    """Find only this checkout's known local build directories."""
    executable, toolkit = None, ROOT / '.local' / 'xboxrecomp'
    for build in (ROOT / '.local' / 'game-build', ROOT / 'work' / 'diagnostic-build'):
        names = ('Release/bruteforce_recomp.exe', 'Debug/bruteforce_recomp.exe', 'bruteforce_recomp')
        for name in names:
            candidate = build / name
            if candidate.is_file() and executable is None:
                executable = candidate
                cache = build / 'CMakeCache.txt'
                if cache.is_file():
                    for line in cache.read_text(encoding='utf-8', errors='replace').splitlines():
                        if line.startswith('XBOXRECOMP_DIR:PATH='):
                            toolkit = Path(line.split('=', 1)[1])
    return executable, toolkit


def make_server(library, port=0):
    token = secrets.token_urlsafe(32)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass  # URLs never print tokens or private game paths.

        def reply(self, code, data, mime='application/json'):
            body = json.dumps(data).encode('utf-8') if mime == 'application/json' else data
            self.send_response(code)
            self.send_header('Content-Type', mime + '; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('X-Frame-Options', 'DENY')
            self.send_header('Content-Security-Policy', "default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
            self.end_headers()
            self.wfile.write(body)

        def authorized(self):
            origin = f'http://127.0.0.1:{self.server.server_port}'
            supplied = self.headers.get('X-Launcher-Token', '')
            return (self.headers.get('Host') == f'127.0.0.1:{self.server.server_port}' and
                    self.headers.get('Origin', origin) == origin and secrets.compare_digest(supplied, token))

        def do_GET(self):
            path = urlsplit(self.path).path
            if path in FILES:
                name, mime = FILES[path]
                return self.reply(200, (ASSETS / name).read_bytes(), mime)
            if not self.authorized():
                return self.reply(403, {'error': 'Open the launcher using its session link'})
            if path == '/api/state':
                return self.reply(200, library.snapshot())
            self.reply(404, {'error': 'Unknown launcher route'})

        def do_POST(self):
            if not self.authorized():
                return self.reply(403, {'error': 'Invalid launcher session'})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 8192 or self.headers.get('Content-Type') != 'application/json':
                    raise ValueError('Expected a small JSON request')
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict):
                    raise ValueError('Expected a JSON object')
                path = urlsplit(self.path).path
                if path == '/api/pick':
                    return self.reply(200, {'path': pick_iso()})
                if path == '/api/import':
                    library.import_iso(data.get('path', ''))
                elif path == '/api/verify':
                    library.verify()
                elif path == '/api/launch':
                    library.launch(keyboard=data.get('keyboard', True) is True)
                else:
                    return self.reply(404, {'error': 'Unknown launcher route'})
                self.reply(202, library.snapshot())
            except (ValueError, OSError, TypeError, TimeoutError) as error:
                self.reply(400, {'error': str(error)})

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    server.daemon_threads = True
    server.token = token
    return server


def main():
    default_executable, default_toolkit = build_defaults()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolkit', type=Path, default=default_toolkit)
    parser.add_argument('--exe', type=Path, default=default_executable)
    parser.add_argument('--workspace', type=Path, default=ROOT / '.local' / 'launcher')
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    if sys.platform == 'android':
        parser.error('The Android app launcher is not integrated yet; this is the desktop launcher.')
    library = Library(args.workspace, args.toolkit, args.exe)
    if library.game is not None and library.snapshot()['native_available']:
        library.verify()
    server = make_server(library)
    url = f'http://127.0.0.1:{server.server_port}/#{server.token}'
    print(f'Brute Force launcher: {url}', flush=True)
    print('Local session only. Press Ctrl+C here to close the launcher.', flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
