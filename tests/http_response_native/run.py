#!/usr/bin/env python3
"""Exercise the real asset handler and pinned IDF response writer on a fake send sink.

The sink deliberately returns short writes and errors; it does not model Wi-Fi,
TCP ACKs, socket buffering, browser behavior or device transfer performance.
"""
from __future__ import annotations

import gzip
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from http.client import HTTPResponse

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.pw_web_pack import pack


def function(source: str, declaration: str) -> str:
    # These C functions close in column zero. Fail closed if the pinned source
    # structure or declaration changes; never maintain a copied HTTP writer.
    if source.count(declaration) != 1:
        raise ValueError(f"Expected exactly one function: {declaration}")
    start = source.index(declaration)
    end = source.index("\n}", start) + 2
    return source[start:end] + "\n"


class MemorySocket:
    def __init__(self, data: bytes):
        self.data = data

    def makefile(self, *_args):
        return io.BytesIO(self.data)


def main() -> None:
    idf = os.environ.get("IDF_PATH")
    if not idf:
        raise SystemExit("Set IDF_PATH to the pinned ESP-IDF 5.4.3 checkout.")
    txrx_path = Path(idf) / "components/esp_http_server/src/httpd_txrx.c"
    txrx = txrx_path.read_text()
    app = (ROOT / "firmware/components/pw_app/pw_app.c").read_text()
    with tempfile.TemporaryDirectory(prefix="pw-http-response-") as directory:
        work = Path(directory)
        pack(ROOT / "firmware/web", work)
        shutil.copyfile(ROOT / "firmware/web/places-de.json.gz", work / "places-de.json.gz")
        fragments = function(app, "static int http_socket_send(")
        fragments += function(txrx, "static esp_err_t httpd_send_all(")
        fragments += function(txrx, "esp_err_t httpd_resp_send(")
        fragments += function(app, "static void headers(")
        fragments += function(app, "static esp_err_t asset_handler(")
        (work / "production.inc").write_text(fragments)
        binary = work / "response-test"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-pedantic", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-g", "-I", str(work),
                        str(Path(__file__).with_name("test_response.c")), "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(work)], check=True,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
        count = 0
        for path in sorted(work.glob("wire-*.http")):
            response = HTTPResponse(MemorySocket(path.read_bytes()))
            response.begin()
            body = response.read()
            asset = path.name.split("--")[1]
            expected = (work / (asset + ".gz")).read_bytes()
            assert response.status == 200
            assert response.getheader("Content-Encoding") == "gzip"
            assert int(response.getheader("Content-Length")) == len(expected)
            assert body == expected, "No truncated, duplicated or appended bytes"
            raw = gzip.decompress(body)
            original = ROOT / "firmware/web" / asset
            assert raw == (gzip.decompress(expected) if asset == "places-de.json" else original.read_bytes())
            assert response.getheader("X-Content-Type-Options") == "nosniff"
            assert response.getheader("Cache-Control") == "no-store"
            assert "script-src 'self'" in response.getheader("Content-Security-Policy")
            count += 1
        assert count == 20
        print(f"{count} real asset/IDF response streams: exact HTTP framing and gzip round-trip PASS")
        print("Send sink is simulated; no Wi-Fi/TCP ACK/device performance acceptance claimed.")


if __name__ == "__main__":
    main()
