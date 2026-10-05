// SPDX-License-Identifier: MIT
#pragma once
#include "pw_update_verify.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PW_STAGE_JOURNAL_BYTES 144u
#define PW_STAGE_CHUNK_BYTES 4096u
typedef enum {
    PW_STAGE_LOCKED,
    PW_STAGE_IDLE,
    PW_STAGE_RECEIVING,
    PW_STAGE_COMPANION,
    PW_STAGE_S3,
    PW_STAGE_LOCAL_READY,
    PW_STAGE_PEER,
    PW_STAGE_PREPARED,
    PW_STAGE_FAILED,
    PW_STAGE_RECOVERY
} pw_stage_phase_t;
typedef struct {
    const uint8_t *public_key;
    size_t public_key_bytes;
    const char *key_id;
    pw_update_policy_t policy;
    /* Qualified LOCAL factory/lab evidence, never populated from HTTP/manifest. */
    bool policy_qualified;
} pw_stage_config_t;
typedef struct {
    void *context;
    int (*load)(void *, uint8_t out[PW_STAGE_JOURNAL_BYTES]); /* 1, absent 0, error -1 */
    bool (*save)(void *, const uint8_t data[PW_STAGE_JOURNAL_BYTES]);
    bool (*begin)(void *, pw_update_role_t role, uint32_t bytes);
    bool (*write)(void *, pw_update_role_t role, uint32_t offset, const uint8_t *, size_t);
    bool (*finish)(void *, pw_update_role_t role); /* S3: full IDF image validation */
    bool (*read)(void *, pw_update_role_t role, uint32_t offset, uint8_t *, size_t);
    void (*abort)(void *);
} pw_stage_ops_t;
typedef struct {
    pw_stage_phase_t phase;
    pw_update_result_t error;
    uint32_t received, total, peer_received, generation;
    uint8_t transaction[16], manifest_sha256[32];
    uint32_t image_bytes[2];
    char version[49];
} pw_stage_view_t;
typedef struct pw_update_stage pw_update_stage_t;

pw_update_stage_t *pw_update_stage_create(const pw_stage_config_t *, const pw_stage_ops_t *);
void pw_update_stage_destroy(pw_update_stage_t *);
bool pw_update_stage_begin(pw_update_stage_t *, uint32_t total, const uint8_t transaction[16]);
bool pw_update_stage_feed(pw_update_stage_t *, const uint8_t *, size_t);
bool pw_update_stage_finish(pw_update_stage_t *);
void pw_update_stage_abort(pw_update_stage_t *, pw_update_result_t);
void pw_update_stage_view(const pw_update_stage_t *, pw_stage_view_t *);
const uint8_t *pw_update_stage_metadata(const pw_update_stage_t *, size_t *manifest,
                                        size_t *signature);
bool pw_update_stage_peer_begin(pw_update_stage_t *);
void pw_update_stage_peer_progress(pw_update_stage_t *, uint32_t offset);
bool pw_update_stage_peer_verified(pw_update_stage_t *);
const char *pw_update_stage_phase_name(pw_stage_phase_t);
/* No activation/commit/boot-slot API exists in this implementation. */
