"""Isolated bounded transport recognition; never opens USB, HTTP or a browser."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics import MAX_LINE_BYTES, parse_firmware_diagnostic


def log(tag, text, level="E"):
    return f"{level} (12345) {tag}: {text}\r\n".encode("ascii")


class DiagnosticsTests(unittest.TestCase):
    def test_playback_device_and_transport_metadata(self):
        for addressable in (0, 1):
            for restricted in (0, 1):
                self.assertEqual(parse_firmware_diagnostic(log("pw_spotify",
                    f"playback_device addressable={addressable} restricted={restricted}", "I")),
                    {"event": "spotify_playback_device", "addressable": addressable, "restricted": restricted})
        base = "playback_transport phase=3 detail=-28679 errno=11 elapsed=6000"
        self.assertEqual(parse_firmware_diagnostic(log("pw_spotify", base, "I")),
            {"event": "spotify_playback_transport", "phase": 3, "detail": -28679,
             "errno": 11, "elapsed_ms": 6000})
        for phase in range(5):
            self.assertIsNotNone(parse_firmware_diagnostic(log("pw_spotify",
                f"playback_transport phase={phase} detail=-65535 errno=4095 elapsed=600000", "I")))
        for invalid in (base + " token=SECRET", "SECRET " + base,
                        base.replace("phase=3", "phase=5"), base.replace("phase=3", "phase=true"),
                        base.replace("detail=-28679", "detail=-65536"),
                        base.replace("detail=-28679", "detail=SECRET"),
                        base.replace("errno=11", "errno=4096"),
                        base.replace("errno=11", "errno=-1"),
                        base.replace("elapsed=6000", "elapsed=600001"),
                        "playback_device addressable=2 restricted=0",
                        "playback_device addressable=0 restricted=false",
                        "playback_device addressable=1 restricted=0 id=SECRET"):
            self.assertIsNone(parse_firmware_diagnostic(log("pw_spotify", invalid, "I")))

    def test_playback_validation_has_only_bounded_numeric_reason(self):
        for reason in range(1, 32):
            with self.subTest(reason=reason):
                result = parse_firmware_diagnostic(log(
                    "pw_spotify", f"playback_validation reason={reason}", "W"))
                self.assertEqual(result, {"event": "spotify_playback_validation", "reason": reason})
                self.assertIs(type(result["reason"]), int)

    def test_playback_validation_rejects_secrets_types_and_ranges(self):
        for value in ("", "0", "-1", "32", "99", "100", "65535", "01", "+1",
                      "1.0", "true", "null", '"1"', "SECRET"):
            with self.subTest(value=value):
                self.assertIsNone(parse_firmware_diagnostic(log(
                    "pw_spotify", f"playback_validation reason={value}", "W")))
        message = "playback_validation reason=1"
        known = log("pw_spotify", message, "W")[:-2]
        for secret in (b"name=SECRET-ROOM", b"id=SECRET-ID", b"Bearer SECRET-TOKEN"):
            for line in (known + b" " + secret, secret + b" " + known,
                         known + b"\n" + secret, known + b"\x00" + secret):
                self.assertIsNone(parse_firmware_diagnostic(line))
        for line in (log("wrong-tag", message, "W"), known + b" reason=2",
                     None, True, 1, known.decode("ascii"), bytearray(known)):
            self.assertIsNone(parse_firmware_diagnostic(line))

    def test_playback_metadata_has_only_fixed_bounded_fields(self):
        flags = ("valid", "known", "playing", "listed", "selected")
        for mask in range(32):
            values = {name: (mask >> bit) & 1 for bit, name in enumerate(flags)}
            message = "playback http=200 transport=0 " + " ".join(
                f"{name}={value}" for name, value in values.items())
            with self.subTest(mask=mask):
                result = parse_firmware_diagnostic(log("pw_spotify", message, "I"))
                self.assertEqual(result, {"event": "spotify_playback", "http_status": 200,
                                          "transport": 0, **values})
                for key, value in result.items():
                    if key != "event":
                        self.assertIs(type(value), int)

    def test_playback_http_and_transport_boundaries(self):
        tail = " valid=0 known=0 playing=0 listed=0 selected=0"
        for http in (0, 100, 204, 403, 599):
            for transport in (-65535, -1, 0, 1, 65535):
                with self.subTest(http=http, transport=transport):
                    message = f"playback http={http} transport={transport}" + tail
                    result = parse_firmware_diagnostic(log("pw_spotify", message, "I"))
                    self.assertEqual(result["http_status"], http)
                    self.assertEqual(result["transport"], transport)
        for field, invalid in (("http", (-1, 1, 99, 600, 1000)),
                               ("transport", (-65536, 65536, 1000000))):
            for value in invalid:
                message = "playback http=200 transport=0" + tail
                message = message.replace(f"{field}={200 if field == 'http' else 0}",
                                          f"{field}={value}")
                with self.subTest(field=field, value=value):
                    self.assertIsNone(parse_firmware_diagnostic(log("pw_spotify", message, "I")))

    def test_playback_flags_reject_ranges_and_wrong_types(self):
        message = "playback http=200 transport=0 valid=1 known=1 playing=1 listed=1 selected=1"
        for field in ("valid", "known", "playing", "listed", "selected"):
            for value in ("-1", "2", "01", "1.0", "true", "null", '"1"', "SECRET"):
                with self.subTest(field=field, value=value):
                    changed = message.replace(f"{field}=1", f"{field}={value}")
                    self.assertIsNone(parse_firmware_diagnostic(log("pw_spotify", changed, "I")))
        raw = log("pw_spotify", message, "I")
        for value in (None, True, 1, raw.decode("ascii"), bytearray(raw), memoryview(raw)):
            with self.subTest(value_type=type(value).__name__):
                self.assertIsNone(parse_firmware_diagnostic(value))

    def test_playback_rejects_suffixes_secrets_and_wrong_context(self):
        message = "playback http=200 transport=0 valid=1 known=1 playing=1 listed=1 selected=1"
        known = log("pw_spotify", message, "I")[:-2]
        for secret in (b"name=SECRET-ROOM", b"id=SECRET-ID", b"Bearer SECRET-TOKEN",
                       b"code=SECRET-CODE", b"https://secret.example"):
            for line in (known + b" " + secret, secret + b" " + known,
                         known + b"\n" + secret, known + b"\x00" + secret):
                self.assertIsNone(parse_firmware_diagnostic(line))
        for line in (log("wrong-tag", message, "I"), log("pw_spotify", message, "D"),
                     known + b" selected=0", known.replace(b"playing=1 ", b""),
                     known.replace(b"http=200", b"http=SECRET"),
                     known.replace(b"transport=0", b"transport=SECRET")):
            self.assertIsNone(parse_firmware_diagnostic(line))

    def test_playback_accepts_only_supported_colour_and_line_framing(self):
        known = log("pw_spotify", "playback http=204 transport=0 valid=1 known=0 "
                    "playing=0 listed=0 selected=0", "I")[:-2]
        expected = {"event": "spotify_playback", "http_status": 204, "transport": 0,
                    "valid": 1, "known": 0, "playing": 0, "listed": 0, "selected": 0}
        for ending in (b"", b"\r", b"\n", b"\r\n"):
            self.assertEqual(parse_firmware_diagnostic(b"\x1b[0;32m" + known + b"\x1b[0m" + ending),
                             expected)
        for line in (known + b"\n\n", known + b"\x1b[2J", known + b"\t",
                     known + b"\xff", known + b"\x1b]0;SECRET\x07"):
            self.assertIsNone(parse_firmware_diagnostic(line))

    def test_device_fetch_metadata_and_rejected_payloads(self):
        valid = "devices http=200 transport=0 valid=1 listed=2 count=1"
        self.assertEqual(parse_firmware_diagnostic(log("pw_spotify", valid, "I")),
                         {"event": "spotify_devices", "http_status": 200, "transport": 0,
                          "valid": 1, "listed": 2, "count": 1})
        for message in ("devices http=200 transport=0 valid=1 listed=0 count=0",
                        "devices http=403 transport=0 valid=0 listed=0 count=0",
                        "devices http=0 transport=258 valid=0 listed=0 count=0",
                        "devices http=200 transport=0 valid=0 listed=1 count=0"):
            result = parse_firmware_diagnostic(log("pw_spotify", message, "I"))
            self.assertIsNotNone(result)
            self.assertEqual(result["event"], "spotify_devices")
        for changed in (valid + " name=SECRET", "SECRET " + valid,
                        valid.replace("count=1", "count=17"),
                        valid.replace("listed=2", "listed=65536"),
                        valid.replace("valid=1", "valid=2"),
                        valid.replace("http=200", "http=99"),
                        valid.replace("http=200", "http=600"),
                        valid.replace("transport=0", "transport=-65536"),
                        valid.replace("listed=2", "listed=SECRET")):
            self.assertIsNone(parse_firmware_diagnostic(log("pw_spotify", changed, "I")))

    def test_known_pinned_idf_events(self):
        cases = (
            ("esp-tls-mbedtls", "mbedtls_ssl_handshake returned -0x2700", {"event": "tls_handshake", "code": -9984}),
            ("esp-tls-mbedtls", "mbedtls_ssl_setup returned -0x7F00", {"event": "tls_setup", "code": -32512}),
            ("esp-tls-mbedtls", "Failed to verify peer certificate!", {"event": "certificate_verify"}),
            ("esp-x509-crt-bundle", "Failed to verify certificate", {"event": "certificate_verify"}),
            ("esp-x509-crt-bundle", "Certificate matched but signature verification failed", {"event": "certificate_verify"}),
            ("esp-tls", "couldn't get hostname for :accounts.spotify.com: getaddrinfo() returns 202, addrinfo=0x0", {"event": "dns_lookup", "code": 202}),
            ("esp-tls", "couldn't get hostname for :api.spotify.com: getaddrinfo() returns -4, addrinfo=(nil)", {"event": "dns_lookup", "code": -4}),
            ("esp-tls", "[sock=54] select() timeout", {"event": "tcp_timeout"}),
            ("esp-tls", "[sock=54] connect() error: Connection refused", {"event": "tcp_connect"}),
            ("esp-tls", "[sock=54] delayed connect error: No route to host", {"event": "tcp_connect"}),
            ("esp-tls", "Failed to open new connection in specified timeout", {"event": "connection_timeout"}),
            ("HTTP_CLIENT", "Connection failed, sock < 0", {"event": "connection_open"}),
            ("HTTP_CLIENT", "Connection timed out before data was ready!", {"event": "http_receive_timeout"}),
            ("HTTP_CLIENT", "esp_transport_read returned:-2 and errno:113 ", {"event": "http_transport_read", "code": -2, "errno": 113}),
            ("HTTP_CLIENT", "transport_read: error - 32771 | ESP_ERR_TCP_TRANSPORT_CONNECTION_FAILED", {"event": "http_transport_read", "code": 32771}),
        )
        for tag, message, expected in cases:
            with self.subTest(tag=tag, message=message):
                self.assertEqual(parse_firmware_diagnostic(log(tag, message)), expected)

    def test_ansi_colour_and_line_endings(self):
        plain = log("esp-tls-mbedtls", "mbedtls_ssl_handshake returned -0x2700")[:-2]
        expected = {"event": "tls_handshake", "code": -9984}
        for ending in (b"", b"\r", b"\n", b"\r\n"):
            self.assertEqual(parse_firmware_diagnostic(b"\x1b[0;31m" + plain + b"\x1b[0m" + ending), expected)
        self.assertEqual(parse_firmware_diagnostic(log("esp-tls-mbedtls", "Failed to verify peer certificate!", "I")), {"event": "certificate_verify"})

    def test_unknown_and_protocol_lines_are_discarded(self):
        for line in (b"", b"PWSET1 {\"access_token\":\"SECRET\"}\n",
                     log("wrong-tag", "mbedtls_ssl_handshake returned -0x2700"),
                     log("esp-tls-mbedtls", "Certificate verified."),
                     log("wifi", "ssid=SECRET password=SECRET"),
                     log("HTTP_CLIENT", "Authorization: Bearer SECRET"),
                     log("HTTP_CLIENT", "HTTP/1.1 401 Unauthorized"),
                     log("esp-tls", "[sock=54] connect() error: SECRET"),
                     log("esp-tls-mbedtls", "verification info: SECRET")):
            self.assertIsNone(parse_firmware_diagnostic(line))

    def test_secret_text_is_never_reflected(self):
        secrets = ("SECRET-HOST", "SECRET-CODE", "SECRET-TOKEN", "SECRET-STATE",
                   "SECRET-SSID", "SECRET-COOKIE", "secret.example")
        for secret in secrets:
            # Even an adversarial value in a legitimate discarded field yields
            # only the fixed event and numeric error, never that field's text.
            line = log("esp-tls", f"couldn't get hostname for :{secret}: getaddrinfo() returns 202, addrinfo=0xdeadbeef")
            result = parse_firmware_diagnostic(line)
            self.assertEqual(result, {"event": "dns_lookup", "code": 202})
            self.assertNotIn(secret, json.dumps(result))
            self.assertNotIn("deadbeef", json.dumps(result))
            known = log("esp-tls-mbedtls", "mbedtls_ssl_handshake returned -0x2700")[:-2]
            for changed in (secret.encode() + known, known + secret.encode(),
                            known + b"\n" + secret.encode(), known + b" " + secret.encode()):
                self.assertIsNone(parse_firmware_diagnostic(changed))

    def test_bounded_numbers_and_input(self):
        invalid = (
            log("esp-tls-mbedtls", "mbedtls_ssl_setup returned -0x10000"),
            log("esp-tls-mbedtls", "mbedtls_ssl_setup returned -0xDEADBEEF"),
            log("HTTP_CLIENT", "esp_transport_read returned:-999999 and errno:113 "),
            log("HTTP_CLIENT", "esp_transport_read returned:-2 and errno:4096 "),
            log("esp-tls", "couldn't get hostname for :example.com: getaddrinfo() returns 32768, addrinfo=0x0"),
            b"x" * (MAX_LINE_BYTES + 1), b"x" * MAX_LINE_BYTES,
            b"\xff\xfe\x00", None, "E (1) HTTP_CLIENT: Connection failed, sock < 0",
        )
        for line in invalid:
            self.assertIsNone(parse_firmware_diagnostic(line))

    def test_controls_and_spliced_lines_are_discarded(self):
        known = log("HTTP_CLIENT", "Connection failed, sock < 0")[:-2]
        for control in (b"\x00", b"\x07", b"\r", b"\n", b"\t", b"\x1b[2J", b"\x1b]0;SECRET\x07"):
            for line in (control + known, known + control, known + control + known):
                # One terminal newline is a framing delimiter, not an injection.
                if line in (known + b"\r", known + b"\n"):
                    continue
                self.assertIsNone(parse_firmware_diagnostic(line))
        self.assertIsNone(parse_firmware_diagnostic(known + b"\n\n"))

    def test_return_schema_has_no_free_text_or_unbounded_fields(self):
        samples = (
            log("HTTP_CLIENT", "esp_transport_read returned:-2 and errno:113 "),
            log("esp-tls", "[sock=65535] select() timeout"),
            log("esp-tls-mbedtls", "mbedtls_ssl_setup returned -0x7F00"),
        )
        events = {"http_transport_read", "tcp_timeout", "tls_setup"}
        for line in samples:
            result = parse_firmware_diagnostic(line)
            self.assertLessEqual(set(result), {"event", "code", "errno"})
            self.assertIn(result["event"], events)
            for key, value in result.items():
                if key != "event":
                    self.assertIs(type(value), int)
                    self.assertLessEqual(abs(value), 65535)
            self.assertLess(len(json.dumps(result)), 90)


if __name__ == "__main__":
    unittest.main()
