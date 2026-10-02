"""Lossy, secret-free recognition of fixed IDF errors and Spotify metadata.

Only fixed event names and bounded numeric metadata survive. This is not a
log sanitizer: unrecognized lines, contexts, names and suffixes are discarded.
Patterns mirror esp-tls/{esp_tls,esp_tls_mbedtls}.c, esp_http_client.c and
mbedtls/esp_crt_bundle/esp_crt_bundle.c in the pinned IDF source tree, plus the
fixed pw_spotify device-fetch and playback summaries. Generic IDF events do not
identify the requesting firmware task. Device-fetch results and playback metadata
do not prove audible playback.
"""
import re

MAX_LINE_BYTES = 8192
_SGR = re.compile(rb"\x1b\[[0-9;]{0,16}m")


def _pattern(tag, message):
    # Neither timestamp nor tag nor any variable text is captured.
    return re.compile(rb"[EWI] \([0-9]{1,20}\) " + re.escape(tag) + rb": " + message)


# Each capture is numeric, with a fixed output key, radix, sign and range.
# DNS hostnames/pointers and socket descriptors are deliberately not captured.
_RULES = (
    (_pattern(b"pw_spotify", rb"playback http=(0|[1-5][0-9]{2}) transport=(-?[0-9]{1,6}) "
              rb"valid=([01]) known=([01]) playing=([01]) listed=([01]) selected=([01])"),
     "spotify_playback", (("http_status", 10, 1, 0, 599), ("transport", 10, 1, -65535, 65535),
                          ("valid", 10, 1, 0, 1), ("known", 10, 1, 0, 1),
                          ("playing", 10, 1, 0, 1), ("listed", 10, 1, 0, 1),
                          ("selected", 10, 1, 0, 1))),
    (_pattern(b"pw_spotify", rb"devices http=(0|[1-5][0-9]{2}) transport=(-?[0-9]{1,6}) "
              rb"valid=([01]) listed=([0-9]{1,5}) count=([0-9]{1,2})"),
     "spotify_devices", (("http_status", 10, 1, 0, 599), ("transport", 10, 1, -65535, 65535),
                         ("valid", 10, 1, 0, 1), ("listed", 10, 1, 0, 65535),
                         ("count", 10, 1, 0, 16))),
    (_pattern(b"esp-tls-mbedtls", rb"mbedtls_ssl_handshake returned -0x([0-9A-Fa-f]{4,8})"),
     "tls_handshake", (("code", 16, -1, 0, 65535),)),
    (_pattern(b"esp-tls-mbedtls", rb"mbedtls_ssl_setup returned -0x([0-9A-Fa-f]{4,8})"),
     "tls_setup", (("code", 16, -1, 0, 65535),)),
    (_pattern(b"esp-tls-mbedtls", rb"Failed to verify peer certificate!"),
     "certificate_verify", ()),
    (_pattern(b"esp-x509-crt-bundle", rb"Failed to verify certificate"),
     "certificate_verify", ()),
    (_pattern(b"esp-x509-crt-bundle", rb"Certificate matched but signature verification failed"),
     "certificate_verify", ()),
    (_pattern(b"esp-tls", rb"couldn't get hostname for :[A-Za-z0-9.-]{1,253}: "
              rb"getaddrinfo\(\) returns (-?[0-9]{1,5}), addrinfo=(?:0x[0-9A-Fa-f]{1,16}|\(nil\))"),
     "dns_lookup", (("code", 10, 1, -32768, 32767),)),
    (_pattern(b"esp-tls", rb"\[sock=[0-9]{1,5}\] select\(\) timeout"),
     "tcp_timeout", ()),
    (_pattern(b"esp-tls", rb"\[sock=[0-9]{1,5}\] (?:delayed connect|connect\(\)) error: "
              rb"(?:Connection refused|No route to host|Network is unreachable|"
              rb"Connection timed out|Connection reset by peer|Host is unreachable|"
              rb"Software caused connection abort|Broken pipe)"),
     "tcp_connect", ()),
    (_pattern(b"esp-tls", rb"Failed to open new connection in specified timeout"),
     "connection_timeout", ()),
    (_pattern(b"HTTP_CLIENT", rb"Connection failed, sock < 0"),
     "connection_open", ()),
    (_pattern(b"HTTP_CLIENT", rb"Connection timed out before data was ready!"),
     "http_receive_timeout", ()),
    (_pattern(b"HTTP_CLIENT", rb"esp_transport_read returned:(-?[0-9]{1,6}) and errno:([0-9]{1,4}) "),
     "http_transport_read", (("code", 10, 1, -65535, 65535), ("errno", 10, 1, 0, 4095))),
    (_pattern(b"HTTP_CLIENT", rb"transport_read: error - (-?[0-9]{1,6}) \| "
              rb"(?:ESP_FAIL|ESP_ERR_TCP_TRANSPORT_CONNECTION_"
              rb"(?:TIMEOUT|CLOSED_BY_FIN|FAILED))"),
     "http_transport_read", (("code", 10, 1, -65535, 65535),)),
)


def parse_firmware_diagnostic(line: bytes) -> dict | None:
    """Return a bounded event from one raw USB log line, or discard it entirely.

    No I/O, exceptions containing input, or reflected text. Only ANSI SGR colour
    escapes are removed; all other control sequences and multiline input fail.
    Callers must separately bound retention and correlate the setup attempt.
    """
    if not isinstance(line, bytes) or not 0 < len(line) <= MAX_LINE_BYTES:
        return None
    if line.endswith(b"\r\n"):
        line = line[:-2]
    elif line.endswith((b"\r", b"\n")):
        line = line[:-1]
    line = _SGR.sub(b"", line)
    if any(c < 32 or c > 126 for c in line):
        return None
    for pattern, event, fields in _RULES:
        match = pattern.fullmatch(line)
        if match is None:
            continue
        result = {"event": event}
        for captured, (key, radix, sign, minimum, maximum) in zip(match.groups(), fields):
            value = int(captured, radix)
            if not minimum <= value <= maximum:
                return None
            result[key] = value * sign
        return result
    return None
