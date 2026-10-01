"""Real loopback HTTP/security and serial framing; all USB/Spotify are isolated fakes."""
from contextlib import contextmanager, redirect_stderr
import http.client
import io
import json
from pathlib import Path
import sys
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from urllib.parse import urlencode

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import helper


def authorization_url(**changes):
    fields = {"client_id": helper.CLIENT_ID, "response_type": "code", "redirect_uri": helper.REDIRECT,
              "code_challenge_method": "S256", "code_challenge": "A" * 43,
              "state": "ab" * 24, "scope": "user-read-playback-state user-modify-playback-state"}
    fields.update(changes)
    return "https://accounts.spotify.com/authorize?" + urlencode(fields)


class FakeDevice:
    def __init__(self):
        self.selected = "/dev/cu.usbFAKE"
        self.calls = []
        self.setup = True
        self.lab = True
        self.role = "controller_s3"
        self.linked = False
        self.state = "unlinked"
        self.authorization_id = ""
        self.failure = None
        self.url = authorization_url()

    def ports(self):
        return [{"port": self.selected, "label": self.selected}]

    def select(self, port):
        if port != self.selected:
            raise helper.SetupError("no_port")

    def request(self, method, **values):
        self.calls.append((method, values))
        if self.failure:
            raise helper.SetupError(self.failure)
        if method in {"hello", "status"}:
            return {"ok": True, "role": self.role, "hardware": helper.HARDWARE, "version": "test-fixture",
                    "lab_enabled": self.lab, "setup_open": self.setup,
                    "spotify": {"linked": self.linked, "connected": self.linked, "state": self.state, "error": "none", "authorization_id": self.authorization_id},
                    "access_token": "MUST-NEVER-BE-RETURNED"}
        if method == "authorize":
            return {"ok": True, "authorization_url": self.url}
        if method == "callback":
            self.state = "authorizing"
        return {"ok": True}

    def close(self):
        pass


class AppTests(unittest.TestCase):
    def test_existing_account_does_not_falsely_confirm_failed_new_login(self):
        self.device.linked, self.device.state = True, "ready"
        self.app.authorize(self.sid)
        self.app.callback(urlencode({"state": "ab" * 24, "code": "fixture"}))
        self.device.state = "ready"  # Old account restored after exchange failure/reboot.
        status = self.app.status(self.sid)
        self.assertTrue(status["spotify"]["linked"])
        self.assertEqual(status["authorization_status"], "failed")
        self.assertEqual(status["notice"], helper.ERRORS["auth_failed"])

    def test_confirmation_binds_to_successful_attempt_and_is_not_exposed(self):
        self.app.authorize(self.sid)
        self.app.callback(urlencode({"state": "ab" * 24, "code": "fixture"}))
        self.device.linked, self.device.state = True, "ready"
        self.device.authorization_id = "ab" * 24
        status = self.app.status(self.sid)
        self.assertEqual(status["authorization_status"], "confirmed")
        self.assertEqual(status["notice"], "")
        self.assertNotIn("authorization_id", json.dumps(status))
        self.assertNotIn("ab" * 24, json.dumps(status))

    def setUp(self):
        self.now = 100.
        self.device = FakeDevice()
        self.app = helper.SetupApp(self.device, lambda: self.now)
        self.sid, self.session = self.app.session(None, create=True)

    def test_url_policy_rejects_extra_scope_redirect_host_verifier_and_duplicates(self):
        self.assertEqual(helper.validate_authorization_url(authorization_url()), "ab" * 24)
        bad = [authorization_url(scope="user-read-email"), authorization_url(redirect_uri="http://evil/callback"),
               authorization_url(code_challenge_method="plain"), authorization_url(state="short"),
               authorization_url(client_id="unapproved"), authorization_url(code_verifier="secret"),
               authorization_url() + "&state=duplicate", authorization_url().replace("accounts.spotify.com", "accounts.spotify.com.evil"),
               authorization_url().replace("https://", "http://"), authorization_url() + "#fragment",
               authorization_url().replace("/authorize?", "/authorize/?"),
               authorization_url().replace("accounts.spotify.com", "name@accounts.spotify.com")]
        for url in bad:
            with self.subTest(url=url), self.assertRaises(helper.SetupError):
                helper.validate_authorization_url(url)

    def test_no_callback_without_authorized_state_and_one_use(self):
        query = urlencode({"state": "ab" * 24, "code": "fixture-code"})
        self.app.callback(query)
        self.assertFalse(self.device.calls)
        self.app.authorize(self.sid)
        self.app.callback(query)
        self.app.callback(query)
        self.assertEqual(sum(method == "callback" for method, _ in self.device.calls), 1)
        self.assertEqual(self.session.notice, "callback_accepted")
        status = self.app.status(self.sid)
        self.assertFalse(status["spotify"]["linked"])
        self.assertEqual(status["spotify"]["state"], "authorizing")
        self.assertNotIn("access_token", json.dumps(status))

    def test_wrong_state_does_not_consume_and_expiry_blocks_code(self):
        self.app.authorize(self.sid)
        self.app.callback(urlencode({"state": "cd" * 24, "code": "fixture"}))
        self.assertIsNotNone(self.app.pending)
        self.now += 601
        self.app.callback(urlencode({"state": "ab" * 24, "code": "fixture"}))
        self.assertIsNone(self.app.pending)
        self.assertEqual(self.session.notice, "auth_expired")
        self.assertNotIn("callback", [call[0] for call in self.device.calls])

    def test_cancel_error_and_closed_window_cannot_link(self):
        self.app.authorize(self.sid)
        self.app.callback(urlencode({"state": "ab" * 24, "error": "access_denied", "error_description": "not reflected"}))
        self.assertEqual(self.session.notice, "auth_rejected")
        self.app.authorize(self.sid)
        self.device.setup = False
        self.app.callback(urlencode({"state": "ab" * 24, "code": "fixture"}))
        self.assertEqual(self.session.notice, "setup_closed")
        self.assertNotIn("callback", [call[0] for call in self.device.calls])

    def test_wrong_chip_and_closed_window_never_authorize(self):
        for role, setup, expected in [("companion_esp32", True, "wrong_chip"), ("controller_s3", False, "setup_closed")]:
            self.device.role, self.device.setup = role, setup
            with self.assertRaises(helper.SetupError) as error:
                self.app.authorize(self.sid)
            self.assertEqual(error.exception.code, expected)
        self.assertNotIn("authorize", [call[0] for call in self.device.calls])

    def test_active_login_cannot_be_replaced_by_another_session(self):
        other, _ = self.app.session(None, create=True)
        self.app.authorize(self.sid)
        for action in (self.app.authorize, self.app.cancel):
            with self.assertRaises(helper.SetupError):
                action(other)
        self.assertEqual(self.app.pending.session, self.sid)

    def test_callback_failure_consumes_code_and_device_change_blocks(self):
        self.app.authorize(self.sid)
        self.device.failure = "usb_timeout"
        self.app.callback(urlencode({"state": "ab" * 24, "code": "fixture"}))
        self.assertIsNone(self.app.pending)
        self.assertEqual(self.session.notice, "usb_timeout")
        self.device.failure = None
        self.app.authorize(self.sid)
        self.device.selected = "/dev/cu.other"
        self.app.callback(urlencode({"state": "ab" * 24, "code": "fixture"}))
        self.assertEqual(self.session.notice, "callback_invalid")


class SerialTests(unittest.TestCase):
    def make_device(self, response, ports=1):
        self.events, self.buffer, self.writes = [], bytearray(), []
        owner = self

        class FakeSerial:
            def __init__(self, **kwargs):
                owner.events.append(("construct", kwargs))
                self.dtr = self.rts = True
                self.port = None

            def open(self):
                owner.events.append(("open", self.dtr, self.rts, self.port))

            def close(self):
                owner.events.append(("close",))

            def write(self, data):
                owner.writes.append(data)
                request = json.loads(data[len(helper.PREFIX):])
                owner.buffer.extend(response(request))
                return len(data)

            def read(self, count):
                chunk = bytes(owner.buffer[:count])
                del owner.buffer[:count]
                return chunk

        module = SimpleNamespace(Serial=FakeSerial, SerialException=type("SerialError", (OSError,), {}))
        provider = lambda: [SimpleNamespace(device=f"/dev/cu.usbFAKE{i}", vid=123) for i in range(ports)]
        return helper.SerialDevice(serial_module=module, port_provider=provider)

    def test_framing_correlated_id_discards_logs_and_does_not_reset(self):
        def response(request):
            ignored = helper.PREFIX + json.dumps({"id": request["id"] - 1, "ok": True}).encode() + b"\n"
            actual = helper.PREFIX + json.dumps({"id": request["id"], "ok": True, "role": "controller_s3", "hardware": helper.HARDWARE, "version": "test-fixture", "lab_enabled": True, "setup_open": True}).encode() + b"\n"
            return b"private firmware log ignored\n" + ignored + actual
        device = self.make_device(response)
        self.assertEqual(device.request("hello")["role"], "controller_s3")
        self.assertEqual(self.events[0][1]["port"], None)
        self.assertEqual(self.events[0][1]["baudrate"], 115200)
        self.assertEqual(self.events[1], ("open", False, False, "/dev/cu.usbFAKE0"))
        self.assertTrue(self.writes[0].startswith(b"PWSET1 "))
        self.assertTrue(self.writes[0].endswith(b"\n"))

    def test_multiple_or_no_ports_never_probe(self):
        for count in (0, 2):
            device = self.make_device(lambda _: b"", ports=count)
            with self.assertRaises(helper.SetupError):
                device.request("hello")
            self.assertFalse(self.events)

    def test_timeout_and_overflow_are_bounded(self):
        device = self.make_device(lambda _: b"")
        with patch.object(helper.time, "monotonic", side_effect=range(100000)), self.assertRaises(helper.SetupError) as error:
            device.request("hello")
        self.assertEqual(error.exception.code, "usb_timeout")
        device = self.make_device(lambda request: b"X" * 9000 + b"\n" + helper.PREFIX +
                                  json.dumps({"id": request["id"], "ok": True, "role": "controller_s3", "hardware": helper.HARDWARE, "version": "test-fixture", "lab_enabled": True, "setup_open": True}).encode() + b"\n")
        self.assertTrue(device.request("hello")["ok"])

    def test_duplicate_json_key_rejected(self):
        device = self.make_device(lambda request: helper.PREFIX + ('{"id":%s,"ok":true,"ok":false}\n' % request["id"]).encode())
        with self.assertRaises(helper.SetupError) as error:
            device.request("hello")
        self.assertEqual(error.exception.code, "usb_protocol")


@contextmanager
def running_server(app):
    # Sequential fixtures share the registered loopback port. Reuse only closed
    # fixture sockets in TIME_WAIT; the actual application keeps reuse disabled.
    server = type("FixtureServer", (helper.SetupServer,), {"allow_reuse_address": True})(app)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        thread.join()
        server.server_close()


class HTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = helper.SetupApp(FakeDevice())
        cls.server_context = running_server(cls.app)
        cls.server = cls.server_context.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.server_context.__exit__(None, None, None)

    def setUp(self):
        with self.app.lock:
            self.app.device = FakeDevice()
            self.app.sessions.clear()
            self.app.pending = None
        status, headers, body = self.request("GET", "/api/session", headers={"Sec-Fetch-Site": "same-origin"})
        self.assertEqual(status, 200)
        self.cookie = headers["Set-Cookie"].split(";", 1)[0]
        self.csrf = json.loads(body)["csrf"]

    def request(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", 8766, timeout=3)
        connection.request(method, path, body, headers or {})
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result

    def post(self, path, data=b"{}", **change):
        headers = {"Origin": helper.ORIGIN, "Cookie": self.cookie, "X-CSRF-Token": self.csrf, "Content-Type": "application/json"}
        headers.update(change)
        return self.request("POST", path, data, headers)

    def test_bind_cookie_csp_no_cors(self):
        self.assertEqual(self.server.server_address, ("127.0.0.1", 8766))
        status, headers, _ = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertEqual(headers["Referrer-Policy"], "no-referrer")
        self.assertIn("frame-ancestors 'none'", headers["Content-Security-Policy"])
        self.assertNotIn("Access-Control-Allow-Origin", headers)
        _, headers, _ = self.request("GET", "/api/session", headers={"Sec-Fetch-Site": "same-origin"})
        self.assertIn("HttpOnly; SameSite=Strict", headers["Set-Cookie"])

    def test_host_origin_csrf_and_cross_site_session_rejected(self):
        self.assertEqual(self.request("GET", "/", headers={"Host": "evil.test:8766"})[0], 403)
        self.assertEqual(self.request("GET", "/api/session", headers={"Sec-Fetch-Site": "cross-site"})[0], 403)
        for change in ({"Origin": "https://evil.test"}, {"X-CSRF-Token": "bad"}, {"Cookie": ""}):
            self.assertEqual(self.post("/api/authorize", **change)[0], 409)
        self.assertNotIn("authorize", [call[0] for call in self.app.device.calls])

    def test_callback_303_no_cookie_required_no_secret_echo_or_log(self):
        self.assertEqual(self.post("/api/authorize")[0], 200)
        log = io.StringIO()
        with redirect_stderr(log):
            status, headers, body = self.request("GET", "/callback?" + urlencode({"state": "ab" * 24, "code": "fake-secret-code"}))
        self.assertEqual(status, 303)
        self.assertEqual(headers["Location"], "/")
        self.assertNotIn("Set-Cookie", headers)
        self.assertNotIn(b"fake-secret-code", body)
        self.assertNotIn("fake-secret-code", log.getvalue())
        self.assertEqual(sum(call[0] == "callback" for call in self.app.device.calls), 1)
        status, _, body = self.request("GET", "/api/status", headers={"Cookie": self.cookie, "Sec-Fetch-Site": "same-origin"})
        self.assertEqual(status, 200)
        self.assertFalse(json.loads(body)["spotify"]["linked"])
        self.assertNotIn(b"MUST-NEVER", body)

    def test_non_json_duplicate_fields_and_unknown_methods_rejected(self):
        for path, body in [("/api/authorize", b'{"a":1,"a":2}'), ("/api/authorize", b"[]"),
                           ("/api/token", b"{}"), ("/api/authorize", b'{"code":"fake"}')]:
            self.assertEqual(self.post(path, body)[0], 409)
        self.assertEqual(self.post("/api/authorize", **{"Content-Type": "text/plain"})[0], 409)


if __name__ == "__main__":
    unittest.main()
