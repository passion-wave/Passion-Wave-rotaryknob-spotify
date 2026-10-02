#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PW_PAYLOAD_MAX 192
#define PW_FRAME_MAX 216
#define PW_PROTOCOL_VERSION 1
#define PW_ROLE_S3 1
#define PW_ROLE_COMPANION 2
#define PW_MSG_HELLO 1
#define PW_MSG_HEARTBEAT 2
#define PW_MSG_HEALTH 3
#define PW_MSG_DIAGNOSTICS 5
#define PW_MSG_UNSUPPORTED 127
/* Wire: PWSP, version u8, sender u8, kind u8, reserved=0,
   session u32 LE, sequence u32 LE, length u16 LE, payload, CRC16 CCITT-FALSE LE.
   COBS framed with trailing zero. CRC is integrity, NOT authentication. */
typedef struct {
    uint8_t role, kind;
    uint32_t session, sequence;
    uint16_t length;
    uint8_t payload[PW_PAYLOAD_MAX];
} pw_frame_t;
typedef struct {
    uint8_t encoded[PW_FRAME_MAX];
    size_t used;
    bool overflow;
} pw_parser_t;
uint16_t pw_crc16(const uint8_t *data, size_t length);
size_t pw_frame_encode(const pw_frame_t *frame, uint8_t *out, size_t capacity);
bool pw_frame_decode(const uint8_t *encoded, size_t length, pw_frame_t *out);
/* true only when a whole valid frame completed; overflow discards to delimiter. */
bool pw_parser_feed(pw_parser_t *parser, uint8_t byte, pw_frame_t *out);
#ifdef __cplusplus
}
#endif
