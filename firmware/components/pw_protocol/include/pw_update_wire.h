// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#define PW_UPDATE_WIRE_VERSION 1
#define PW_MSG_OTA_BEGIN 0x20
#define PW_MSG_OTA_META_CHUNK 0x21
#define PW_MSG_OTA_META_VERIFY 0x22
#define PW_MSG_OTA_IMAGE_CHUNK 0x23
#define PW_MSG_OTA_IMAGE_VERIFY 0x24
#define PW_MSG_OTA_ACTIVATE 0x25
#define PW_MSG_OTA_PAIR_HEALTH 0x26
#define PW_MSG_OTA_COMMIT 0x27
#define PW_MSG_OTA_ROLLBACK 0x28
#define PW_MSG_OTA_ABORT 0x29
#define PW_MSG_OTA_STATUS 0x2a
#define PW_MSG_OTA_ACK 0x2b
#define PW_MSG_OTA_BOOT_REPORT 0x2c
#define PW_OTA_BASE_BYTES 17u
#define PW_OTA_ACK_BYTES 68u
#define PW_OTA_BOOT_REPORT_BYTES 143u
#define PW_OTA_REQUIRED_HEALTH 0x0fu /* UI, storage, web, peer; no Internet requirement. */
/* All messages: version u8 at 0, transaction id[16] at 1; multi-byte LE.
 BEGIN (20): manifest_length u16 @17, DER_length u8 @19.
 META_CHUNK (20..192): offset u16 @17, bytes @19; exact manifest then DER.
 META_VERIFY / IMAGE_VERIFY / ABORT / STATUS (17): base only.
 IMAGE_CHUNK (22..192): image offset u32 @17, 1..171 bytes @21.
 ACTIVATE / COMMIT (53): manifest_sha256 @17, current companion bootnonce u32 @49.
 PAIR_HEALTH (93): manifest_sha256 @17, companion nonce @49, S3 nonce @53,
   actual S3 image hash @57, local health flags u32 @89. S3 nonce==frame.session.
 ROLLBACK (89): manifest_sha256 @17, companion nonce @49,
   actual old S3 image hash @53, S3 nonce @85 (must equal frame.session).
 ACK (68): request kind @17, result @18, phase @19, request sequence u32 @20,
   expected stream offset u32 @24, companion nonce u32 @28, manifest digest @32,
   target image bytes u32 @64. Duplicate request reuses exact ACK payload.
 BOOT_REPORT (143): phase @17, companion nonce u32 @18, manifest digest @22,
   actual running-image SHA256 @54, running slot u32 @86, health flags u32 @90,
   NUL-terminated running version[49] @94.
 No existing PWSP HELLO/HEALTH layout changes. CRC remains integrity, not auth.
*/
typedef enum {
    PW_OTA_IDLE = 0,
    PW_OTA_META = 1,
    PW_OTA_RECEIVING = 2,
    PW_OTA_VERIFIED = 3,
    PW_OTA_ACTIVATING = 4,
    PW_OTA_BOOT_PENDING = 5,
    PW_OTA_PEER_HEALTHY = 6,
    PW_OTA_COMMITTING = 7,
    PW_OTA_COMPLETE = 8,
    PW_OTA_ROLLBACK_PENDING = 9,
    PW_OTA_RECOVERY = 10,
    PW_OTA_FAILED = 11,
    PW_OTA_LOCKED = 12
} pw_ota_phase_t;
typedef enum {
    PW_OTA_OK = 0,
    PW_OTA_ERR_LOCKED = 1,
    PW_OTA_ERR_FORMAT = 2,
    PW_OTA_ERR_STATE = 3,
    PW_OTA_ERR_TRANSACTION = 4,
    PW_OTA_ERR_OFFSET = 5,
    PW_OTA_ERR_REPLAY = 6,
    PW_OTA_ERR_VERIFY = 7,
    PW_OTA_ERR_FLASH = 8,
    PW_OTA_ERR_JOURNAL = 9,
    PW_OTA_ERR_HEALTH = 10,
    PW_OTA_ERR_MEMORY = 11,
    PW_OTA_ERR_CONFLICT = 12
} pw_ota_result_t;
