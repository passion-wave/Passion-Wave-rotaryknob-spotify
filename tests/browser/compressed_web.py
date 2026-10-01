#!/usr/bin/env python3
"""Real loopback HTTP gzip/CSP browser check with simulated public device status.

This exercises browser decoding and the unmodified website, not ESP HTTPD or
device performance. An ephemeral loopback port keeps USB helper port 8766 free.
"""
from __future__ import annotations

import copy
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import tempfile
import threading
from urllib.parse import urlsplit

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools import pw_web_pack
from device_web import BASE

WEB = ROOT / "firmware/web"
TYPES = {"index.html": "text/html; charset=utf-8",
         "style.css": "text/css; charset=utf-8",
         "app.js": "text/javascript; charset=utf-8"}
CSP = ("default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
       "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", choices=("chromium", "webkit"), default="chromium")
    args = parser.parse_args()
    state = copy.deepcopy(BASE)
    state["secure_write"] = False
    for key in ("settings", "catalog", "weather", "config_revision"):
        state.pop(key)
    with tempfile.TemporaryDirectory(prefix="pw-compressed-web-") as directory:
        output = Path(directory)
        pw_web_pack.pack(WEB, output)

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *_args):
                pass

            def do_GET(self):
                path = urlsplit(self.path).path
                status, encoded = 200, False
                if path in ("/", "/app.js", "/style.css"):
                    name = "index.html" if path == "/" else path[1:]
                    body, media = (output / (name + ".gz")).read_bytes(), TYPES[name]
                    encoded = True
                elif path == "/api/v1/session":
                    body, media = b'{"secure_write":false}', "application/json; charset=utf-8"
                elif path == "/api/v1/status":
                    body = json.dumps(state).encode()
                    media = "application/json; charset=utf-8"
                else:
                    status, body, media = 404, b"Not found", "text/plain; charset=utf-8"
                self.send_response(status)
                self.send_header("Content-Type", media)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.send_header("Content-Security-Policy", CSP)
                if encoded:
                    self.send_header("Content-Encoding", "gzip")
                self.end_headers()
                self.wfile.write(body)

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        origin = f"http://127.0.0.1:{server.server_port}"
        try:
            with sync_playwright() as playwright:
                browser = getattr(playwright, args.browser).launch()
                context = browser.new_context(viewport={"width": 390, "height": 844})
                page = context.new_page()
                errors, responses, foreign = [], {}, []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.on("response", lambda response: responses.update({urlsplit(response.url).path: response}))

                def restrict(route):
                    if route.request.url.startswith(origin + "/"):
                        route.continue_()
                    else:
                        foreign.append(route.request.url)
                        route.abort()

                context.route("**/*", restrict)
                page.goto(origin + "/", wait_until="networkidle")
                page.locator("#security-notice").wait_for(state="visible")
                assert page.locator("#connection-pill").inner_text() == "Im WLAN verbunden"
                assert page.locator("#wifi-ssid").is_disabled()
                assert page.evaluate("document.styleSheets.length === 1 && document.styleSheets[0].cssRules.length > 10")
                assert page.evaluate("getComputedStyle(document.querySelector('.overview-grid')).display === 'grid'")
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
                assert "/places-de.json" not in responses, "Initial view must not fetch the large places database"
                for name in pw_web_pack.ASSETS:
                    path = "/" if name == "index.html" else "/" + name
                    response = responses[path]
                    headers = response.all_headers()
                    assert response.status == 200
                    assert headers["content-encoding"] == "gzip"
                    assert headers["content-type"] == TYPES[name]
                    assert int(headers["content-length"]) == (output / (name + ".gz")).stat().st_size
                    assert response.body() == (WEB / name).read_bytes(), "Browser must receive every decoded byte"
                assert not errors, errors
                assert not foreign, foreign
                browser.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
    print(f"Compressed website ({args.browser}): real loopback HTTP gzip decoding, MIME/nosniff/CSP, CSS, JavaScript and mobile layout: PASS")
    print("API status is simulated; no device transport/performance acceptance claimed.")


if __name__ == "__main__":
    main()
