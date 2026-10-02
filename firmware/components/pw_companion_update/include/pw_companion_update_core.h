// SPDX-License-Identifier: MIT
#pragma once
#include "pw_protocol.h"
#include "pw_update_verify.h"
#include "pw_update_wire.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PW_COMPANION_JOURNAL_BYTES 408u
#define PW_COMPANION_PROOF_MAX (8u + PW_UPDATE_MANIFEST_MAX + 72u)
typedef struct {
    uint32_t address, capacity, image_bytes;
    uint16_t security_version;
    uint8_t image_sha256[32];
    char version[49];
} pw_companion_slot_t;
typedef struct {
    void *context;
    // load: 1 existing, 0 absent, -1 storage/corruption error.
    int (*load)(void *, uint8_t out[PW_COMPANION_JOURNAL_BYTES]);
    bool (*save)(void *, const uint8_t in[PW_COMPANION_JOURNAL_BYTES]);
    // Original signed manifest capsule, persisted before any image erase.
    // load_proof receives output capacity in *size and returns actual length.
    bool (*load_proof)(void *, uint8_t *out, size_t *size);
    bool (*save_proof)(void *, const uint8_t *in, size_t size);
    bool (*slots)(void *, pw_companion_slot_t *running, pw_companion_slot_t *inactive);
    bool (*begin)(void *, uint32_t inactive_address, uint32_t size);
    bool (*write)(void *, uint32_t offset, const uint8_t *data, size_t size);
    bool (*read)(void *, uint32_t address, uint32_t offset, uint8_t *data, size_t size);
    bool (*finish)(void *); // Full ESP/bootloader image verification.
    void (*abort)(void *);
    bool (*validate_slot)(void *, uint32_t address);
    bool (*set_boot)(void *, uint32_t address);
    bool (*mark_valid)(void *);
    bool (*pending_verify)(void *);
    uint64_t (*monotonic_ms)(void *);
} pw_companion_update_ops_t;
typedef struct {
    const uint8_t *public_key;
    size_t public_key_bytes;
    const char *key_id;
    pw_update_policy_t policy;
    // Factory/prior verified pair evidence; never populate from browser upload.
    uint8_t current_s3_sha256[32];
} pw_companion_update_config_t;
typedef struct pw_companion_update_core pw_companion_update_core_t;
// NULL config is deliberately closed. Ops and key buffers must outlive context.
pw_companion_update_core_t *
pw_companion_update_core_create(const pw_companion_update_config_t *config,
                                const pw_companion_update_ops_t *ops, uint32_t boot_nonce);
void pw_companion_update_core_destroy(pw_companion_update_core_t *core);
// One task owns core. true means an OTA command was handled and reply contains ACK.
bool pw_companion_update_core_handle(pw_companion_update_core_t *core, const pw_frame_t *request,
                                     pw_frame_t *reply);
void pw_companion_update_core_report(pw_companion_update_core_t *core, pw_frame_t *report);
void pw_companion_update_core_tick(pw_companion_update_core_t *core);
bool pw_companion_update_core_reboot_requested(pw_companion_update_core_t *core);
#ifdef __cplusplus
}
#endif
