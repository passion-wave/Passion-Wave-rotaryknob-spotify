"""Shared PWSET1 goldens against the shipped helper and, when available, C parser.

No socket, serial device, account, web request or new runtime dependency is used.
Schema checks use the existing explicitly bounded tools.check validator. Native
checks use the pinned cJSON sources already required by the firmware toolchain.
"""
from copy import deepcopy
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from urllib.parse import urlencode

from tools.check import validate
from tools.spotify_setup import helper

ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts/usb-setup/v1"
GOLD = json.loads((CONTRACT / "golden.json").read_text())
SCHEMAS = {kind: json.loads((CONTRACT / f"{kind[:-1]}.schema.json").read_text())
           for kind in ("requests", "responses")}


def payload(wire):
    """Fixture framing adapter; payload parsing is the real helper's strict parser."""
    if not wire.endswith("\n"):
        raise ValueError("incomplete fixture")
    line = wire[:-1]
    if len(line.encode("utf-8")) > 8191:
        raise ValueError("oversize fixture")
    if line.endswith("\r"):
        line = line[:-1]
    if not line.startswith("PWSET1 ") or any(c in line for c in "\r\n\0"):
        raise ValueError("invalid fixture framing")
    return helper.parse_json(line[7:])


def response_named(name):
    return payload(next(case["wire_utf8"] for case in GOLD["responses"]["good"] if case["name"] == name))


class FixtureSerial:
    """Only supplies bytes; protocol decisions stay in SerialDevice.request."""
    def __init__(self, responder):
        self.responder = responder
        self.received = []
        self.buffer = bytearray()
        self.options = None

    def device(self):
        owner = self

        class Port:
            def __init__(self, **kwargs):
                owner.options = kwargs
                self.dtr = self.rts = True
                self.port = None

            def open(self):
                if self.dtr or self.rts:
                    raise AssertionError("test observed active reset lines")

            def close(self):
                pass

            def write(self, data):
                request = helper.parse_json(data[len(helper.PREFIX):])
                owner.received.append(request)
                owner.buffer.extend(owner.responder(request))
                return len(data)

            def read(self, size):
                result = bytes(owner.buffer[:size])
                del owner.buffer[:size]
                return result

        module = SimpleNamespace(Serial=Port, SerialException=OSError)
        ports = lambda: [SimpleNamespace(device="/dev/cu.usb-fixture", vid=0x303a)]
        return helper.SerialDevice(serial_module=module, port_provider=ports)


class ScriptedDevice:
    """Wire fixture producer; does not implement OAuth, state validation or tokens."""
    def __init__(self):
        self.status_reply = response_named("status-unlinked")
        self.callback_reply = response_named("callback-accepted-only")
        self.transport = FixtureSerial(self.reply)
        self.device = self.transport.device()

    def reply(self, request):
        method = request["method"]
        if method == "hello":
            data = response_named("hello-window-open")
        elif method == "status":
            data = deepcopy(self.status_reply)
        elif method == "authorize":
            data = response_named("authorize")
        elif method == "callback":
            data = deepcopy(self.callback_reply)
        elif method == "cancel":
            data = response_named("cancel")
        else:
            raise AssertionError("unexpected method")
        data["id"] = request["id"]
        return helper.PREFIX + json.dumps(data).encode() + b"\n"


class GoldenHelperTests(unittest.TestCase):
    def setUp(self):
        self.profile = patch.object(helper, "CLIENT_ID", GOLD["fixture_oauth_profile"]["client_id"])
        self.profile.start()
        self.addCleanup(self.profile.stop)

    def test_all_frame_and_schema_cases_stay_consistent(self):
        count = 0
        for kind in ("requests", "responses"):
            for outcome in ("good", "bad"):
                for case in GOLD[kind][outcome]:
                    with self.subTest(kind=kind, outcome=outcome, name=case["name"]):
                        if outcome == "good":
                            validate(payload(case["wire_utf8"]), SCHEMAS[kind])
                        else:
                            with self.assertRaises(ValueError):
                                validate(payload(case["wire_utf8"]), SCHEMAS[kind])
                        count += 1
        self.assertGreaterEqual(count, 62)

    def test_good_requests_are_emitted_by_actual_serial_sender(self):
        replies = {method: response_named(name) for method, name in {
            "hello": "hello-window-open", "status": "status-unlinked", "authorize": "authorize",
            "callback": "callback-accepted-only", "cancel": "cancel"}.items()}
        for case in GOLD["requests"]["good"]:
            with self.subTest(name=case["name"]):
                request = payload(case["wire_utf8"])
                # IDs are allocator-owned; the largest valid fixture ID exceeds
                # this helper's deliberately smaller rollover, tested by C below.
                def respond(actual):
                    data = {**replies[actual["method"]], "id": actual["id"]}
                    return helper.PREFIX + json.dumps(data).encode() + b"\n"
                transport = FixtureSerial(respond)
                device = transport.device()
                device.request_id = 16
                device.request(request["method"], **{k: v for k, v in request.items() if k not in {"id", "method"}})
                emitted = transport.received[0]
                validate(emitted, SCHEMAS["requests"])
                self.assertEqual({k: v for k, v in emitted.items() if k != "id"},
                                 {k: v for k, v in request.items() if k != "id"})

    def test_every_good_response_uses_actual_stream_parser(self):
        for case in GOLD["responses"]["good"]:
            with self.subTest(name=case["name"]):
                expected = payload(case["wire_utf8"])
                device = FixtureSerial(lambda _: case["wire_utf8"].encode()).device()
                device.request_id = expected["id"] - 1
                if expected["ok"]:
                    self.assertEqual(device.request(case["request_method"]), expected)
                else:
                    with self.assertRaises(helper.SetupError) as error:
                        device.request(case["request_method"])
                    self.assertEqual(error.exception.code, expected["error"])

    def test_bad_response_corpus_cannot_become_success(self):
        for case in GOLD["responses"]["bad"]:
            with self.subTest(name=case["name"]):
                data = payload(case["wire_utf8"])
                method = ("status" if "spotify" in data or data.get("ok") is False else
                          "authorize" if "authorization_url" in data else
                          "callback" if "accepted" in data else "hello")
                device = FixtureSerial(lambda _: case["wire_utf8"].encode()).device()
                device.request_id = data["id"] - 1
                with self.assertRaises(helper.SetupError):
                    device.request(method)

    def test_authorization_url_corpus_uses_runtime_profile_check(self):
        for case in GOLD["authorization_urls"]["good"]:
            with self.subTest(name=case["name"]):
                self.assertEqual(helper.validate_authorization_url(case["url"]), case["expected_state"])
        for case in GOLD["authorization_urls"]["bad"]:
            with self.subTest(name=case["name"]), self.assertRaises(helper.SetupError):
                helper.validate_authorization_url(case["url"])

    def test_response_id_and_method_correlation_goldens(self):
        cases = [case for case in GOLD["semantic_cases"] if "request" in case and "response" in case]
        self.assertTrue(cases)
        for case in cases:
            with self.subTest(name=case["name"]):
                response = case["response"]
                first = helper.PREFIX + json.dumps(response).encode() + b"\n"
                # An unrelated ID must be skipped before the genuine answer.
                if case["expected"] == "ignore":
                    matching = {**response, "id": case["request"]["id"]}
                    first += helper.PREFIX + json.dumps(matching).encode() + b"\n"
                device = FixtureSerial(lambda _: first).device()
                device.request_id = case["request"]["id"] - 1
                if case["expected"] == "reject":
                    with self.assertRaises(helper.SetupError):
                        device.request(case["request"]["method"])
                else:
                    result = device.request(case["request"]["method"])
                    self.assertEqual(result["id"], case["request"]["id"])

    def test_attempt_confirmation_goldens_through_real_helper(self):
        cases = [case for case in GOLD["semantic_cases"] if "expected_authorization_id" in case]
        self.assertTrue(cases)
        for case in cases:
            with self.subTest(name=case["name"]):
                source = ScriptedDevice()
                app = helper.SetupApp(source.device)
                sid, _ = app.session(None, create=True)
                app.authorize(sid)
                app.callback(urlencode({"state": case["expected_authorization_id"], "code": "FAKE-fixture"}))
                if "spotify" in case["response"]:
                    source.status_reply = deepcopy(case["response"])
                else:
                    source.status_reply["spotify"]["state"] = "authorizing"
                result = app.status(sid)
                outcome = case["expected"]
                if outcome in {"confirm-current-attempt", "link-confirmed-but-not-ready"}:
                    self.assertEqual(result["authorization_status"], "confirmed")
                    if outcome == "link-confirmed-but-not-ready":
                        self.assertNotEqual(result["spotify"]["state"], "ready")
                elif outcome == "wait-not-success":
                    self.assertEqual(result["authorization_status"], "waiting")
                else:
                    self.assertNotEqual(result["authorization_status"], "confirmed")
                self.assertNotIn("authorization_id", json.dumps(result))
                self.assertNotIn(case["expected_authorization_id"], json.dumps(result))

    def test_expiry_duplicate_and_cancel_goldens_do_not_forward_second_code(self):
        for case in GOLD["semantic_cases"]:
            if "events" not in case and "elapsed_ms_since_authorize" not in case:
                continue
            with self.subTest(name=case["name"]):
                now = [100.0]
                source = ScriptedDevice()
                app = helper.SetupApp(source.device, lambda: now[0])
                sid, _ = app.session(None, create=True)
                app.authorize(sid)
                state = app.pending.state
                query = urlencode({"code": "FAKE-fixture", "state": state})
                if "elapsed_ms_since_authorize" in case:
                    now[0] += case["elapsed_ms_since_authorize"] / 1000
                    app.callback(query)
                    expected_callbacks = 0
                elif "cancel" in case["events"]:
                    app.cancel(sid)
                    app.callback(query)
                    expected_callbacks = 0
                else:
                    app.callback(query)
                    app.callback(query)
                    expected_callbacks = 1
                actual = sum(request["method"] == "callback" for request in source.transport.received)
                self.assertEqual(actual, expected_callbacks)

    def test_closed_window_wire_failure_does_not_authorize(self):
        case = next(case for case in GOLD["semantic_cases"] if case.get("setup_open") is False)
        response = case["expected_response"]
        device = FixtureSerial(lambda _: helper.PREFIX + json.dumps(response).encode() + b"\n").device()
        device.request_id = case["request"]["id"] - 1
        with self.assertRaises(helper.SetupError) as error:
            device.request(case["request"]["method"])
        self.assertEqual(error.exception.code, "setup_closed")

    def test_false_callback_ack_cannot_mark_local_flow_accepted(self):
        source = ScriptedDevice()
        app = helper.SetupApp(source.device)
        sid, session = app.session(None, create=True)
        app.authorize(sid)
        state = app.pending.state
        source.callback_reply = payload(next(case["wire_utf8"] for case in GOLD["responses"]["bad"] if case["name"] == "accepted-false"))
        app.callback(urlencode({"state": state, "code": "FAKE-fixture"}))
        self.assertEqual(session.authorization_status, "failed")
        self.assertEqual(session.notice, "usb_protocol")
        self.assertIsNone(app.pending)


class NativeRequestGoldenTests(unittest.TestCase):
    def test_shared_request_vectors_against_real_c_decoder(self):
        compiler = shutil.which("cc")
        idf = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf-v5.4.3"))
        cjson = Path(os.environ.get("CJSON_DIR", idf / "components/json/cJSON"))
        if not compiler or not (cjson / "cJSON.c").is_file():
            if os.environ.get("IDF_PATH") or os.environ.get("CJSON_DIR"):
                self.fail("Explicit native conformance environment lacks compiler or cJSON source")
            self.skipTest("C conformance NOT RUN: compiler and pinned IDF_PATH/CJSON_DIR required")
        with tempfile.TemporaryDirectory(prefix="pw-setup-goldens-") as directory:
            binary = Path(directory) / "decoder"
            args = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
                    "-fsanitize=address,undefined", "-g"]
            for include in (ROOT / "firmware/components/pw_setup_usb/include", ROOT / "firmware/components/pw_app/include", cjson):
                args += ["-I" + str(include)]
            args += [str(ROOT / "firmware/components/pw_setup_usb/pw_setup_protocol.c"),
                     str(ROOT / "firmware/components/pw_app/pw_validation.c"), str(cjson / "cJSON.c"),
                     str(Path(__file__).with_name("c_request_driver.c")), "-lm", "-o", str(binary)]
            subprocess.run(args, check=True, capture_output=True)
            cases, records = [], bytearray()
            for outcome in ("good", "bad"):
                for case in GOLD["requests"][outcome]:
                    line = case["wire_utf8"]
                    if not line.endswith("\n"):
                        continue  # Byte-stream delimiter ownership is outside pw_setup_decode.
                    line = line[:-1]
                    if line.endswith("\r"):
                        line = line[:-1]
                    data = line.encode("utf-8")
                    records.extend(struct.pack("<I", len(data)) + data)
                    cases.append((outcome, case))
            env = {**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"}
            result = subprocess.run([str(binary)], input=records, capture_output=True, check=True, env=env)
            lines = result.stdout.decode("ascii").splitlines()
            self.assertEqual(len(lines), len(cases))
            methods = {"hello": 0, "status": 1, "authorize": 2, "callback": 3, "cancel": 4}
            for (outcome, case), line in zip(cases, lines):
                with self.subTest(name=case["name"]):
                    valid, request_id, method = map(int, line.split())
                    self.assertEqual(bool(valid), outcome == "good")
                    if valid:
                        parsed = payload(case["wire_utf8"])
                        self.assertEqual(request_id, parsed["id"])
                        self.assertEqual(method, methods[parsed["method"]])
                    else:
                        self.assertEqual(request_id, 0)


if __name__ == "__main__":
    unittest.main()
