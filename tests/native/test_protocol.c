#include "pw_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    assert(pw_crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    pw_frame_t a = {.role = PW_ROLE_S3, .kind = PW_MSG_HELLO, .session = 0x12345678, .sequence = 7},
               b;
    uint8_t buf[PW_FRAME_MAX];
    for (unsigned len = 0; len <= PW_PAYLOAD_MAX; len++) {
        a.length = len;
        for (unsigned i = 0; i < len; i++)
            a.payload[i] = (uint8_t)((i * 73 + len) % 256);
        size_t n = pw_frame_encode(&a, buf, sizeof buf);
        assert(n > 0 && n <= PW_FRAME_MAX && buf[n - 1] == 0);
        assert(pw_frame_decode(buf, n - 1, &b));
        assert(b.session == a.session && b.length == len && !memcmp(a.payload, b.payload, len));
        for (size_t i = 0; i < n - 1; i++) {
            uint8_t old = buf[i];
            buf[i] ^= 1;
            assert(!pw_frame_decode(buf, n - 1, &b));
            buf[i] = old;
        }
    }
    pw_parser_t parser = {0};
    for (unsigned i = 0; i < 1000; i++)
        assert(!pw_parser_feed(&parser, 42, &b));
    assert(!pw_parser_feed(&parser, 0, &b));
    size_t n = pw_frame_encode(&a, buf, sizeof buf);
    for (size_t i = 0; i < n; i++)
        assert(pw_parser_feed(&parser, buf[i], &b) == (i == n - 1));
    a.session = 0;
    assert(!pw_frame_encode(&a, buf, sizeof buf));
    a.session = 1;
    a.role = 0;
    assert(!pw_frame_encode(&a, buf, sizeof buf));
    puts("PW-S CRC, 193 lengths, all single-bit corruptions and stream recovery: OK");
}
