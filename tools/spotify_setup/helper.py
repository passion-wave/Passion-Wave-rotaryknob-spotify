#!/usr/bin/env python3
"""Temporary loopback/USB bridge. PKCE, credentials and Spotify API stay on S3."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from http.cookies import CookieError, SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import threading
import time
from urllib.parse import parse_qsl, urlsplit
import webbrowser

HOST = "127.0.0.1:8766"
ORIGIN = "http://" + HOST
REDIRECT = ORIGIN + "/callback"
CLIENT_ID = "2dadee96887f4e238fdd24342b82d99c"
SCOPES = {"user-read-playback-state", "user-modify-playback-state"}
HARDWARE = "JC3636K518C_I_YR1"
PREFIX = b"PWSET1 "
LINE_MAX = 8192
AUTH_TTL = 600
SESSION_TTL = 3600
STATIC = Path(__file__).with_name("static")
ERRORS = {
    "no_port": "RotaryKnob per USB anschließen und erneut prüfen.",
    "select_port": "Bitte den USB-Anschluss des RotaryKnob auswählen.",
    "usb_unavailable": "USB-Anschluss nicht verfügbar. Andere Programme mit USB-Zugriff schließen.",
    "usb_timeout": "Keine Antwort vom Display-Chip. USB-Verbindung und Firmware prüfen.",
    "usb_protocol": "Die Geräteantwort passt nicht zu diesem Einrichtungshelfer.",
    "wrong_chip": "Der Display-Chip ist nicht verbunden. USB-Stecker um 180° drehen und erneut prüfen.",
    "setup_closed": "Den Drehknopf 3 Sekunden gedrückt halten, um die Einrichtung zu öffnen.",
    "lab_disabled": "Spotify ist in dieser Gerätefirmware noch nicht freigeschaltet.",
    "auth_url": "Das Gerät hat keine gültige Spotify-Anmeldung bereitgestellt.",
    "auth_busy": "Eine Anmeldung läuft bereits. Zuerst abbrechen oder abschließen.",
    "auth_expired": "Die Anmeldung ist abgelaufen. Bitte Spotify erneut verbinden.",
    "auth_rejected": "Die Anmeldung wurde abgebrochen. Du kannst es erneut versuchen.",
    "callback_accepted": "Anmeldung an den RotaryKnob übergeben. Das Gerät prüft die Verbindung …",
    "callback_invalid": "Diese Anmeldung gehört nicht zum aktuellen Einrichtungsvorgang.",
    "auth_failed": "Die neue Anmeldung wurde vom Knob nicht bestätigt. Bitte Spotify erneut verbinden.",
    "device_rejected": "Das Gerät konnte den Vorgang nicht annehmen. Gerätestatus prüfen und erneut versuchen.",
    "not_ready": "Der Knob ist noch nicht bereit. WLAN, Uhrzeit und geöffnetes Einrichtungsfenster prüfen.",
    "busy": "Der Knob bearbeitet noch einen Vorgang. Kurz warten und erneut prüfen.",
    "invalid_request": "Die Gerätefirmware unterstützt diese Einrichtungsanfrage nicht.",
    "device_error": "Der Knob konnte die Anmeldung nicht verarbeiten. Gerätestatus prüfen und erneut versuchen.",
    "session": "Einrichtungsseite neu laden und erneut versuchen.",
    "request": "Die Anfrage konnte nicht verarbeitet werden.",
}


class SetupError(Exception):
    def __init__(self, code: str):
        self.code = code if isinstance(code, str) and code in ERRORS else "device_rejected"
        super().__init__(self.code)  # Never include serial bytes, code or URL.


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate field")
        result[key] = value
    return result


def parse_json(data):
    return json.loads(data, object_pairs_hook=unique_object,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError("constant")))


def validate_authorization_url(url: str) -> str:
    """Return device state only after exact Spotify/PKCE/least-scope validation."""
    if not isinstance(url, str) or len(url) > 1023 or not url.isascii() or any(ord(c) <= 32 for c in url):
        raise SetupError("auth_url")
    try:
        parts = urlsplit(url)
        if (parts.scheme, parts.netloc, parts.path, parts.fragment) != (
                "https", "accounts.spotify.com", "/authorize", ""):
            raise ValueError("destination")
        pairs = parse_qsl(parts.query, keep_blank_values=True, strict_parsing=True, max_num_fields=9)
        query = unique_object(pairs)
        required = {"client_id", "response_type", "redirect_uri", "code_challenge_method",
                    "code_challenge", "state", "scope"}
        if set(query) != required:
            raise ValueError("fields")
        if (query["client_id"] != CLIENT_ID or query["response_type"] != "code" or
                query["redirect_uri"] != REDIRECT or query["code_challenge_method"] != "S256" or
                not re.fullmatch(r"[A-Za-z0-9_-]{43}", query["code_challenge"]) or
                not re.fullmatch(r"[0-9a-f]{48}", query["state"]) or
                len(query["scope"].split(" ")) != 2 or set(query["scope"].split(" ")) != SCOPES):
            raise ValueError("policy")
        return query["state"]
    except (ValueError, KeyError):
        raise SetupError("auth_url") from None


class SerialDevice:
    """A single selected serial port; opening never deliberately toggles reset lines."""
    def __init__(self, port=None, serial_module=None, port_provider=None):
        if serial_module is None:
            import serial
            from serial.tools import list_ports
            serial_module, port_provider = serial, list_ports.comports
        self.serial_module = serial_module
        self.port_provider = port_provider
        self.selected = port
        self.serial = None
        self.request_id = secrets.randbelow(1_000_000) + 1

    def ports(self):
        result = []
        for item in self.port_provider():
            if item.device.startswith("/dev/tty."):
                continue  # macOS /dev/cu.* is the non-dial-in endpoint.
            if item.vid is None and not any(part in item.device for part in ("usb", "USB", "ACM")):
                continue
            result.append({"port": item.device, "label": item.device})
        return sorted(result, key=lambda item: item["port"])[:32]

    def select(self, port):
        if not isinstance(port, str) or port not in {item["port"] for item in self.ports()}:
            raise SetupError("no_port")
        self.close()
        self.selected = port

    def close(self):
        if self.serial:
            self.serial.close()
        self.serial = None

    def _open(self):
        if self.serial:
            return
        ports = self.ports()
        if self.selected is None:
            if not ports:
                raise SetupError("no_port")
            if len(ports) != 1:
                raise SetupError("select_port")
            self.selected = ports[0]["port"]
        if self.selected not in {item["port"] for item in ports}:
            raise SetupError("no_port")
        options = {"exclusive": True} if os.name == "posix" else {}
        connection = self.serial_module.Serial(port=None, baudrate=115200, timeout=0.1,
                                               write_timeout=1, rtscts=False, dsrdtr=False, **options)
        # Set before open. Some USB/OS drivers can still glitch these lines;
        # this code does not pulse them, run esptool or reset the device.
        connection.dtr = False
        connection.rts = False
        connection.port = self.selected
        try:
            connection.open()
        except (OSError, self.serial_module.SerialException):
            connection.close()
            raise SetupError("usb_unavailable") from None
        self.serial = connection

    def request(self, method, **values):
        if method not in {"hello", "status", "authorize", "callback", "cancel"}:
            raise SetupError("request")
        self._open()
        self.request_id += 1
        if self.request_id > 2_000_000_000:
            self.request_id = 1
        request_id = self.request_id
        payload = {"id": request_id, "method": method, **values}
        encoded = PREFIX + json.dumps(payload, separators=(",", ":")).encode("ascii") + b"\n"
        if len(encoded) > LINE_MAX:
            raise SetupError("request")
        deadline = time.monotonic() + (5 if method in {"hello", "status"} else 10)
        try:
            if self.serial.write(encoded) != len(encoded):
                raise SetupError("usb_unavailable")
            line = bytearray()
            overflow = False
            while time.monotonic() < deadline:
                chunk = self.serial.read(1)
                if not chunk:
                    continue
                if chunk != b"\n":
                    if len(line) < LINE_MAX and not overflow:
                        line.extend(chunk)
                    else:
                        overflow = True
                    continue
                if overflow:
                    line.clear()
                    overflow = False
                    continue
                raw = bytes(line).rstrip(b"\r")
                line.clear()
                if not raw.startswith(PREFIX):
                    continue  # Discard firmware logs without printing or retaining them.
                try:
                    response = parse_json(raw[len(PREFIX):].decode("utf-8"))
                except (ValueError, UnicodeError, RecursionError):
                    raise SetupError("usb_protocol") from None
                if not isinstance(response, dict) or type(response.get("id")) is not int:
                    raise SetupError("usb_protocol")
                if response["id"] != request_id:
                    continue
                if type(response.get("ok")) is not bool:
                    raise SetupError("usb_protocol")
                if not response["ok"]:
                    raise SetupError(response.get("error", "device_rejected"))
                return response
            raise SetupError("usb_timeout")
        except (OSError, self.serial_module.SerialException):
            self.close()
            raise SetupError("usb_unavailable") from None


def device_identity(response):
    if response.get("role") != "controller_s3" or response.get("hardware") != HARDWARE:
        raise SetupError("wrong_chip")
    if (type(response.get("setup_open")) is not bool or
            type(response.get("lab_enabled")) is not bool or
            not isinstance(response.get("version"), str) or len(response["version"]) > 64):
        raise SetupError("usb_protocol")
    return {key: response[key] for key in ("role", "hardware", "version", "lab_enabled", "setup_open")}


@dataclass
class Session:
    csrf: str
    expires: float
    notice: str = ""
    confirming_state: str = ""
    confirming_until: float = 0
    authorization_status: str = "none"


@dataclass
class Pending:
    state: str
    session: str
    port: str
    expires: float


class SetupApp:
    def __init__(self, device, clock=time.monotonic):
        self.device = device
        self.clock = clock
        self.sessions = {}
        self.pending = None
        self.lock = threading.RLock()

    def session(self, sid, create=False):
        now = self.clock()
        self.sessions = {key: value for key, value in self.sessions.items() if value.expires > now}
        if sid in self.sessions:
            return sid, self.sessions[sid]
        if not create or len(self.sessions) >= 8:
            raise SetupError("session")
        sid = secrets.token_hex(32)
        self.sessions[sid] = Session(secrets.token_hex(32), now + SESSION_TTL)
        return sid, self.sessions[sid]

    def status(self, sid):
        _, session = self.session(sid)
        ports = self.device.ports()
        result = {"ports": ports, "selected_port": self.device.selected,
                  "notice": ERRORS.get(session.notice, ""), "pending": False}
        if self.pending and self.pending.expires <= self.clock():
            owner = self.sessions.get(self.pending.session)
            if owner:
                owner.notice = "auth_expired"
                owner.authorization_status = "failed"
                owner.confirming_state = ""
            self.pending = None
        result["pending"] = bool(self.pending)
        try:
            identity = device_identity(self.device.request("hello"))
            result.update(identity)
            result["selected_port"] = self.device.selected
            if identity["setup_open"]:
                raw = self.device.request("status")
                result.update(device_identity(raw))
                spotify = raw.get("spotify")
                if not isinstance(spotify, dict) or any(type(spotify.get(k)) is not bool for k in ("linked", "connected")):
                    raise SetupError("usb_protocol")
                if any(not isinstance(spotify.get(k), str) or not re.fullmatch(r"[a-z_]{1,40}", spotify[k])
                       for k in ("state", "error")):
                    raise SetupError("usb_protocol")
                confirmation = spotify.get("authorization_id")
                if not isinstance(confirmation, str) or not re.fullmatch(r"(?:[0-9a-f]{48})?", confirmation):
                    raise SetupError("usb_protocol")
                result["spotify"] = {key: spotify[key] for key in ("linked", "connected", "state", "error")}
                if session.confirming_state and session.authorization_status == "waiting" and not self.pending:
                    if spotify["linked"] and hmac.compare_digest(confirmation, session.confirming_state):
                        session.authorization_status = "confirmed"
                        session.confirming_state = ""
                    elif spotify["state"] != "authorizing" or self.clock() >= session.confirming_until:
                        session.authorization_status = "failed"
                        session.confirming_state = ""
                        session.notice = "auth_failed"
                if spotify["linked"] and spotify["state"] == "ready" and session.authorization_status in {"none", "confirmed"}:
                    session.notice = ""
        except SetupError as error:
            result["error"] = error.code
            result["message"] = ERRORS[error.code]
        result["notice"] = ERRORS.get(session.notice, "")
        result["authorization_status"] = session.authorization_status
        return result

    def require_open(self):
        identity = device_identity(self.device.request("hello"))
        if not identity["setup_open"]:
            raise SetupError("setup_closed")
        if not identity["lab_enabled"]:
            raise SetupError("lab_disabled")

    def authorize(self, sid):
        _, session = self.session(sid)
        if self.pending and self.pending.expires > self.clock():
            raise SetupError("auth_busy")
        self.pending = None
        self.require_open()
        response = self.device.request("authorize")
        url = response.get("authorization_url")
        state = validate_authorization_url(url)
        self.pending = Pending(state, sid, self.device.selected, self.clock() + AUTH_TTL)
        session.confirming_state = state
        session.confirming_until = self.clock() + AUTH_TTL
        session.authorization_status = "waiting"
        session.notice = ""
        return {"authorization_url": url}

    def cancel(self, sid):
        _, session = self.session(sid)
        if self.pending and self.pending.session != sid:
            raise SetupError("auth_busy")
        self.pending = None  # Local one-use state closes even when USB disappeared.
        session.notice = "auth_rejected"
        session.confirming_state = ""
        session.authorization_status = "failed"
        self.require_open()
        self.device.request("cancel")
        return {"ok": True}

    def callback(self, query):
        # Cross-site navigation deliberately need not carry the SameSite=Strict
        # cookie. Only the already CSRF-authorized, unguessable one-use state
        # identifies its originating session; no new session is made here.
        try:
            values = unique_object(parse_qsl(query, keep_blank_values=True, strict_parsing=True, max_num_fields=4))
        except ValueError:
            return
        state = values.get("state", "")
        if not re.fullmatch(r"[0-9a-f]{48}", state) or not self.pending:
            return
        pending = self.pending
        if not hmac.compare_digest(pending.state, state):
            return
        self.pending = None  # Consume before serial delivery, including timeout/error.
        session = self.sessions.get(pending.session)
        if not session:
            return
        session.authorization_status = "failed"
        if pending.expires <= self.clock() or session.expires <= self.clock():
            session.notice = "auth_expired"
            return
        if pending.port != self.device.selected:
            session.notice = "callback_invalid"
            return
        has_code, has_error = "code" in values, "error" in values
        if has_code == has_error or not set(values) <= {"code", "state", "error", "error_description"}:
            session.notice = "callback_invalid"
            return
        try:
            self.require_open()
            if has_error:
                self.device.request("cancel")
                session.notice = "auth_rejected"
            else:
                code = values["code"]
                if not re.fullmatch(r"[!-~]{1,1024}", code):
                    raise SetupError("callback_invalid")
                self.device.request("callback", code=code, state=state)
                session.notice = "callback_accepted"
                session.authorization_status = "waiting"
        except SetupError as error:
            session.notice = error.code


class SetupServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, app):
        self.app = app
        self.slots = threading.BoundedSemaphore(8)
        super().__init__(("127.0.0.1", 8766), Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()

    def handle_error(self, request, client_address):
        pass  # Never traceback callback request bytes or serial data.


class Handler(BaseHTTPRequestHandler):
    server_version = "PassionWaveSetup"
    sys_version = ""

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, *args):
        pass  # Base HTTP access/error logs would expose OAuth query parameters.

    def send_error(self, code, message=None, explain=None):
        self.reply(code, {"error": "request", "message": ERRORS["request"]})

    def reply(self, code, data=b"", content_type="application/json; charset=utf-8", cookie=None, location=None):
        if isinstance(data, dict):
            data = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        for name, value in {
            "Content-Type": content_type, "Content-Length": str(len(data)),
            "Cache-Control": "no-store", "Referrer-Policy": "no-referrer",
            "X-Content-Type-Options": "nosniff", "X-Frame-Options": "DENY",
            "Cross-Origin-Resource-Policy": "same-origin",
            "Content-Security-Policy": "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'",
            "Connection": "close",
        }.items():
            self.send_header(name, value)
        if cookie:
            self.send_header("Set-Cookie", f"pw_setup={cookie}; HttpOnly; SameSite=Strict; Path=/; Max-Age={SESSION_TTL}")
        if location:
            self.send_header("Location", location)
        self.end_headers()
        self.close_connection = True
        if self.command != "HEAD":
            self.wfile.write(data)

    def validate_host(self):
        if self.headers.get_all("Host") != [HOST] or not self.path.startswith("/") or len(self.path) > 4096:
            raise SetupError("request")
        if self.headers.get("Origin") not in (None, ORIGIN):
            raise SetupError("request")

    def cookie(self):
        raw = self.headers.get("Cookie", "")
        if len(raw) > 1024:
            return None
        try:
            parsed = SimpleCookie(raw)
            value = parsed["pw_setup"].value
            return value if re.fullmatch(r"[0-9a-f]{64}", value) else None
        except (KeyError, ValueError, CookieError):
            return None

    def api_source(self):
        if self.headers.get("Sec-Fetch-Site") != "same-origin" and self.headers.get("Origin") != ORIGIN:
            raise SetupError("request")

    def do_GET(self):
        try:
            self.validate_host()
            target = urlsplit(self.path)
            if target.path == "/callback":
                with self.server.app.lock:
                    self.server.app.callback(target.query)
                self.reply(303, location="/")
                return
            if target.query:
                raise SetupError("request")
            assets = {"/": ("index.html", "text/html; charset=utf-8"),
                      "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                      "/style.css": ("style.css", "text/css; charset=utf-8")}
            if target.path in assets:
                filename, mime = assets[target.path]
                self.reply(200, (STATIC / filename).read_bytes(), mime)
                return
            self.api_source()
            with self.server.app.lock:
                sid, session = self.server.app.session(self.cookie(), create=target.path == "/api/session")
                if target.path == "/api/session":
                    self.reply(200, {"csrf": session.csrf}, cookie=sid)
                elif target.path == "/api/status":
                    self.reply(200, self.server.app.status(sid))
                else:
                    self.reply(404, {"error": "request"})
        except SetupError as error:
            self.reply(403, {"error": error.code, "message": ERRORS[error.code]})

    def do_POST(self):
        try:
            self.validate_host()
            if self.headers.get_all("Origin") != [ORIGIN]:
                raise SetupError("request")
            if (self.headers.get("Content-Type") != "application/json" or
                    self.headers.get_all("Transfer-Encoding") or
                    len(self.headers.get_all("Content-Length", [])) != 1):
                raise SetupError("request")
            length = self.headers.get("Content-Length", "")
            if not re.fullmatch(r"[0-9]{1,4}", length) or not 1 <= int(length) <= 2048:
                raise SetupError("request")
            data = self.rfile.read(int(length))
            if len(data) != int(length):
                raise SetupError("request")
            try:
                values = parse_json(data)
            except (ValueError, UnicodeError, RecursionError):
                raise SetupError("request") from None
            if not isinstance(values, dict):
                raise SetupError("request")
            with self.server.app.lock:
                sid, session = self.server.app.session(self.cookie())
                csrf = self.headers.get("X-CSRF-Token", "")
                if not re.fullmatch(r"[0-9a-f]{64}", csrf) or not hmac.compare_digest(csrf, session.csrf):
                    raise SetupError("session")
                if self.path == "/api/authorize" and not values:
                    result = self.server.app.authorize(sid)
                elif self.path == "/api/cancel" and not values:
                    result = self.server.app.cancel(sid)
                elif self.path == "/api/port" and set(values) == {"port"}:
                    if self.server.app.pending:
                        raise SetupError("auth_busy")
                    self.server.app.device.select(values["port"])
                    result = {"ok": True}
                else:
                    raise SetupError("request")
                self.reply(200, result)
        except SetupError as error:
            self.reply(409, {"error": error.code, "message": ERRORS[error.code]})


def main():
    parser = argparse.ArgumentParser(description="PassionWave Spotify per USB einrichten")
    parser.add_argument("--port", help="USB-Anschluss explizit wählen; nur aufgelistete USB-Ports")
    parser.add_argument("--no-browser", action="store_true", help="Browser nicht automatisch öffnen")
    args = parser.parse_args()
    device = None
    try:
        device = SerialDevice(args.port)
        server = SetupServer(SetupApp(device))
    except ImportError:
        print("Bitte zuerst die Abhängigkeit aus tools/spotify_setup/requirements.txt installieren.")
        return 1
    except OSError:
        print("Einrichtungshelfer nicht gestartet: Port 8766 ist bereits belegt oder nicht verfügbar.")
        return 1
    print(f"PassionWave Einrichtung: {ORIGIN}/ — Beenden mit Strg+C.")
    print("Der Helfer läuft nur auf diesem Rechner. Keine Spotify-Zugangsdaten hier eingeben.")
    if not args.no_browser:
        webbrowser.open(ORIGIN + "/", new=2)
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        device.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
