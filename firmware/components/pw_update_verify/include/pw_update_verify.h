// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PW_UPDATE_MANIFEST_MAX 16384u
#define PW_UPDATE_PRODUCT_ID "passion-wave-rotaryknob-spotify"
typedef enum { PW_UPDATE_COMPANION = 0, PW_UPDATE_S3 = 1 } pw_update_role_t;
typedef enum {
    PW_UPDATE_OK = 0,
    PW_UPDATE_INVALID_ARGUMENT,
    PW_UPDATE_MEMORY,
    PW_UPDATE_SIGNATURE,
    PW_UPDATE_UNTRUSTED_KEY,
    PW_UPDATE_FORMAT,
    PW_UPDATE_NONCANONICAL,
    PW_UPDATE_EXAMPLE,
    PW_UPDATE_PRODUCT,
    PW_UPDATE_ROLE,
    PW_UPDATE_RELEASE,
    PW_UPDATE_CHANNEL,
    PW_UPDATE_HARDWARE,
    PW_UPDATE_SIZE,
    PW_UPDATE_PROTOCOL,
    PW_UPDATE_CONFIG_SCHEMA,
    PW_UPDATE_SECURITY_VERSION,
    PW_UPDATE_BOOTLOADER,
    PW_UPDATE_DOWNGRADE,
    PW_UPDATE_IMAGE_HASH,
    PW_UPDATE_IMAGE_HEADER,
    PW_UPDATE_IMAGE_VERSION,
    PW_UPDATE_STATE
} pw_update_result_t;

typedef struct {
    const char *hardware_id;        // Factory/board evidence, NEVER upload fields.
    const char *bootloader_version; // Qualified bootloader release, not app version.
    const char *running_version;
    uint32_t app_slot_bytes;
    uint16_t security_floor;
    uint16_t emitted_protocol, accepted_peer_min, accepted_peer_max;
} pw_update_role_policy_t;
typedef struct {
    pw_update_role_policy_t roles[2]; // Indexed by enum; exact role mapping.
    uint16_t current_config_schema;
    bool allow_beta, allow_version_downgrade;
} pw_update_policy_t;

typedef struct {
    pw_update_role_t role;
    char version[49], release_id[65], hardware_id[65], url[2049], minimum_bootloader[49];
    uint32_t bytes;
    uint8_t sha256[32];
    uint16_t emitted_protocol, accepted_peer_min, accepted_peer_max;
    uint16_t readable_config_min, readable_config_max, security_version;
} pw_update_image_info_t;
typedef struct {
    char version[49], release_id[65], channel[7], key_id[65];
    uint16_t config_schema;
    uint8_t manifest_sha256[32];
    pw_update_image_info_t images[2];
} pw_update_manifest_info_t;
// Opaque proof-bearing objects are produced only by the verifier, not JSON flags.
typedef struct pw_update_manifest pw_update_manifest_t;
typedef struct pw_update_image_check pw_update_image_check_t;

// Trust key/key_id/policy come from authenticated firmware/provisioning; never a
// browser upload, manifest URL, or a companion self-assertion alone. PEM or DER
// public key; ECDSA secp256r1 only. Signature is DER, over SHA256(exact JSON bytes).
// On ANY error, *manifest is NULL. No network/flash/activation/eFuse side effects.
pw_update_result_t pw_update_verify_manifest(const uint8_t *json, size_t json_size,
                                             const uint8_t *signature, size_t signature_size,
                                             const uint8_t *trusted_public_key, size_t key_size,
                                             const char *trusted_key_id,
                                             const pw_update_policy_t *policy,
                                             pw_update_manifest_t **manifest);
const pw_update_manifest_info_t *pw_update_manifest_info(const pw_update_manifest_t *manifest);
void pw_update_manifest_free(pw_update_manifest_t *manifest);

// Hash the actual inactive/staged image in order, independently on BOTH MCUs.
// Also binds ESP chip id, app descriptor version, project name and secure_version.
// IDF bootloader/full esp_image_verify and transactional activation remain required.
pw_update_result_t pw_update_image_begin(const pw_update_manifest_t *manifest,
                                         pw_update_role_t role, pw_update_image_check_t **check);
pw_update_result_t pw_update_image_feed(pw_update_image_check_t *check, const void *bytes,
                                        size_t length);
// Consumes and clears *check even on rejection. Wrong length/hash/header fails closed.
pw_update_result_t pw_update_image_finish(pw_update_image_check_t **check);
void pw_update_image_abort(pw_update_image_check_t **check);
const char *pw_update_result_name(pw_update_result_t result);
#ifdef __cplusplus
}
#endif
