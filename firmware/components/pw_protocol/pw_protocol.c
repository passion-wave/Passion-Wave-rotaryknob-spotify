#include "pw_protocol.h"
#include <string.h>
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t pw_crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (unsigned b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
size_t pw_frame_encode(const pw_frame_t *f, uint8_t *out, size_t cap) {
    if (!f || !out || cap < PW_FRAME_MAX || f->length > PW_PAYLOAD_MAX || !f->session || !f->kind ||
        (f->role != PW_ROLE_S3 && f->role != PW_ROLE_COMPANION))
        return 0;
    uint8_t raw[PW_PAYLOAD_MAX + 20] = {'P',     'W',     'S', 'P', PW_PROTOCOL_VERSION,
                                        f->role, f->kind, 0};
    put32(raw + 8, f->session);
    put32(raw + 12, f->sequence);
    raw[16] = (uint8_t)f->length;
    raw[17] = (uint8_t)(f->length >> 8);
    memcpy(raw + 18, f->payload, f->length);
    size_t n = 18 + f->length;
    uint16_t crc = pw_crc16(raw, n);
    raw[n++] = (uint8_t)crc;
    raw[n++] = (uint8_t)(crc >> 8);
    size_t write = 1, code_at = 0;
    uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (!raw[i]) {
            out[code_at] = code;
            code_at = write++;
            code = 1;
        } else {
            out[write++] = raw[i];
            if (++code == 0xff) {
                out[code_at] = code;
                code_at = write++;
                code = 1;
            }
        }
    }
    out[code_at] = code;
    out[write++] = 0;
    return write;
}
bool pw_frame_decode(const uint8_t *in, size_t n, pw_frame_t *out) {
    if (!in || !out || n < 2 || n >= PW_FRAME_MAX)
        return false;
    uint8_t raw[PW_PAYLOAD_MAX + 20];
    size_t r = 0, w = 0;
    while (r < n) {
        uint8_t code = in[r++];
        if (!code || (size_t)(code - 1) > n - r)
            return false;
        for (unsigned j = 1; j < code; j++) {
            if (w >= sizeof raw || !in[r])
                return false;
            raw[w++] = in[r++];
        }
        if (code < 255 && r < n) {
            if (w >= sizeof raw)
                return false;
            raw[w++] = 0;
        }
    }
    if (w < 20 || memcmp(raw, "PWSP", 4) || raw[4] != PW_PROTOCOL_VERSION || raw[7] || !raw[6] ||
        (raw[5] != PW_ROLE_S3 && raw[5] != PW_ROLE_COMPANION))
        return false;
    uint16_t len = (uint16_t)raw[16] | ((uint16_t)raw[17] << 8);
    if (len > PW_PAYLOAD_MAX || w != (size_t)len + 20 || !get32(raw + 8))
        return false;
    uint16_t crc = (uint16_t)raw[w - 2] | ((uint16_t)raw[w - 1] << 8);
    if (crc != pw_crc16(raw, w - 2))
        return false;
    memset(out, 0, sizeof *out);
    out->role = raw[5];
    out->kind = raw[6];
    out->session = get32(raw + 8);
    out->sequence = get32(raw + 12);
    out->length = len;
    memcpy(out->payload, raw + 18, len);
    return true;
}
bool pw_parser_feed(pw_parser_t *p, uint8_t b, pw_frame_t *out) {
    if (!p || !out)
        return false;
    if (!b) {
        bool ok = !p->overflow && p->used && pw_frame_decode(p->encoded, p->used, out);
        p->used = 0;
        p->overflow = false;
        return ok;
    }
    if (p->overflow)
        return false;
    if (p->used >= sizeof p->encoded) {
        p->overflow = true;
        p->used = 0;
        return false;
    }
    p->encoded[p->used++] = b;
    return false;
}
