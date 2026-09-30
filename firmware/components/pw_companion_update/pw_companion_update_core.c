// SPDX-License-Identifier: MIT
#include "pw_companion_update_core.h"
#include "mbedtls/sha256.h"
#include <stdlib.h>
#include <string.h>

#define PAIR_HEALTH_TTL_MS 30000u
#define UNCONFIRMED_BOOT_TIMEOUT_MS (10u * 60u * 1000u)
struct journal {
    pw_ota_phase_t phase;
    uint32_t generation;
    uint8_t tx[16], manifest[32], target_hash[32], s3_hash[32], old_hash[32], old_s3_hash[32],
        trust_hash[32];
    uint32_t old_address, new_address, target_bytes, old_bytes, received;
    uint16_t security_version, old_security_version;
    char old_version[49], version[49], release_id[65];
};
struct pw_companion_update_core {
    const pw_companion_update_config_t *config;
    pw_companion_update_ops_t ops;
    struct journal j;
    pw_companion_slot_t running, inactive;
    uint32_t boot_nonce, peer_session, last_session, last_sequence, reply_sequence;
    uint64_t created_ms, peer_health_ms;
    bool trusted, stage_open, reboot, have_last, pending_boot, storage_ok;
    uint8_t current_s3_hash[32];
    uint8_t *metadata;
    uint16_t manifest_size, metadata_size, metadata_received;
    uint8_t signature_size;
    pw_update_manifest_t *manifest;
    pw_update_image_check_t *image;
    pw_frame_t last_request, last_reply;
};
static uint16_t u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(n >> (i * 8));
}
static bool nonzero(const uint8_t *p, size_t n) {
    unsigned sum = 0;
    for (size_t i = 0; i < n; ++i)
        sum |= p[i];
    return sum != 0;
}
static void encode(const struct journal *j, uint8_t out[PW_COMPANION_JOURNAL_BYTES]) {
    memset(out, 0, PW_COMPANION_JOURNAL_BYTES);
    memcpy(out, "PWUJ", 4);
    out[4] = 1;
    out[5] = (uint8_t)j->phase;
    put16(out + 6, j->old_security_version);
    put32(out + 8, j->generation);
    memcpy(out + 12, j->tx, 16);
    memcpy(out + 28, j->manifest, 32);
    memcpy(out + 60, j->target_hash, 32);
    memcpy(out + 92, j->s3_hash, 32);
    memcpy(out + 124, j->old_hash, 32);
    memcpy(out + 156, j->old_s3_hash, 32);
    put32(out + 188, j->old_address);
    put32(out + 192, j->new_address);
    put32(out + 196, j->target_bytes);
    put32(out + 200, j->old_bytes);
    put32(out + 204, j->received);
    put16(out + 208, j->security_version);
    memcpy(out + 210, j->old_version, 49);
    memcpy(out + 259, j->version, 49);
    memcpy(out + 308, j->release_id, 65);
    memcpy(out + 373, j->trust_hash, 32);
    put16(out + 406, pw_crc16(out, 406));
}
static bool decode(const uint8_t in[PW_COMPANION_JOURNAL_BYTES], struct journal *j) {
    if (memcmp(in, "PWUJ", 4) || in[4] != 1 || in[5] > PW_OTA_FAILED ||
        u16(in + 406) != pw_crc16(in, 406) || !memchr(in + 210, 0, 49) ||
        !memchr(in + 259, 0, 49) || !memchr(in + 308, 0, 65))
        return false;
    memset(j, 0, sizeof(*j));
    j->phase = (pw_ota_phase_t)in[5];
    j->old_security_version = u16(in + 6);
    memcpy(j->trust_hash, in + 373, 32);
    j->generation = u32(in + 8);
    memcpy(j->tx, in + 12, 16);
    memcpy(j->manifest, in + 28, 32);
    memcpy(j->target_hash, in + 60, 32);
    memcpy(j->s3_hash, in + 92, 32);
    memcpy(j->old_hash, in + 124, 32);
    memcpy(j->old_s3_hash, in + 156, 32);
    j->old_address = u32(in + 188);
    j->new_address = u32(in + 192);
    j->target_bytes = u32(in + 196);
    j->old_bytes = u32(in + 200);
    j->received = u32(in + 204);
    j->security_version = u16(in + 208);
    memcpy(j->old_version, in + 210, 49);
    memcpy(j->version, in + 259, 49);
    memcpy(j->release_id, in + 308, 65);
    return true;
}
static bool save_phase(pw_companion_update_core_t *c, pw_ota_phase_t phase) {
    c->j.phase = phase;
    ++c->j.generation;
    uint8_t bytes[PW_COMPANION_JOURNAL_BYTES];
    encode(&c->j, bytes);
    if (!c->ops.save(c->ops.context, bytes)) {
        c->j.phase = PW_OTA_RECOVERY;
        c->storage_ok = false;
        return false;
    }
    c->storage_ok = true;
    return true;
}
static void clear_staging(pw_companion_update_core_t *c) {
    if (c->stage_open)
        c->ops.abort(c->ops.context);
    c->stage_open = false;
    pw_update_image_abort(&c->image);
    pw_update_manifest_free(c->manifest);
    c->manifest = NULL;
    free(c->metadata);
    c->metadata = NULL;
    c->metadata_received = c->metadata_size = c->manifest_size = 0;
    c->signature_size = 0;
}
static bool slot_hash(pw_companion_update_core_t *c, uint32_t address, uint32_t count,
                      const uint8_t expected[32]) {
    if (!count || count > 16777216)
        return false;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    uint8_t buffer[1024], hash[32];
    bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
    for (uint32_t at = 0; ok && at < count;) {
        size_t n = count - at > sizeof(buffer) ? sizeof(buffer) : count - at;
        ok = c->ops.read(c->ops.context, address, at, buffer, n) &&
             mbedtls_sha256_update(&sha, buffer, n) == 0;
        at += (uint32_t)n;
    }
    if (ok)
        ok = mbedtls_sha256_finish(&sha, hash) == 0 && !memcmp(hash, expected, 32);
    mbedtls_sha256_free(&sha);
    return ok;
}
static bool persist_proof(pw_companion_update_core_t *c) {
    size_t size = 8u + c->metadata_size;
    uint8_t *proof = malloc(size);
    if (!proof)
        return false;
    memcpy(proof, "PWP1", 4);
    put16(proof + 4, c->manifest_size);
    proof[6] = c->signature_size;
    proof[7] = 0;
    memcpy(proof + 8, c->metadata, c->metadata_size);
    bool ok = c->ops.save_proof(c->ops.context, proof, size);
    free(proof);
    return ok;
}
static bool authenticate_journal(pw_companion_update_core_t *c) {
    // CRC detects interrupted/corrupt storage, not authenticity. Reverify the
    // original signature instead of treating a persisted phase as authorization.
    if (c->j.old_address == c->j.new_address ||
        !((c->j.old_address == c->running.address && c->j.new_address == c->inactive.address) ||
          (c->j.new_address == c->running.address && c->j.old_address == c->inactive.address)))
        return false;
    uint8_t *proof = malloc(PW_COMPANION_PROOF_MAX);
    if (!proof)
        return false;
    size_t size = PW_COMPANION_PROOF_MAX;
    bool ok = c->ops.load_proof(c->ops.context, proof, &size) && size >= 16 &&
              size <= PW_COMPANION_PROOF_MAX && !memcmp(proof, "PWP1", 4) && proof[7] == 0;
    pw_update_manifest_t *manifest = NULL;
    if (ok) {
        uint16_t json_size = u16(proof + 4);
        uint8_t signature_size = proof[6];
        ok = json_size && json_size <= PW_UPDATE_MANIFEST_MAX && signature_size >= 8 &&
             signature_size <= 72 && size == 8u + json_size + signature_size;
        if (ok) {
            pw_update_policy_t policy = c->config->policy;
            policy.roles[PW_UPDATE_COMPANION].running_version = c->running.version;
            policy.roles[PW_UPDATE_COMPANION].app_slot_bytes =
                c->j.new_address == c->running.address ? c->running.capacity : c->inactive.capacity;
            ok = pw_update_verify_manifest(proof + 8, json_size, proof + 8 + json_size,
                                           signature_size, c->config->public_key,
                                           c->config->public_key_bytes, c->config->key_id, &policy,
                                           &manifest) == PW_UPDATE_OK;
        }
    }
    if (ok) {
        const pw_update_manifest_info_t *m = pw_update_manifest_info(manifest);
        const pw_update_image_info_t *target = &m->images[PW_UPDATE_COMPANION];
        ok = !memcmp(c->j.manifest, m->manifest_sha256, 32) &&
             !memcmp(c->j.target_hash, target->sha256, 32) &&
             !memcmp(c->j.s3_hash, m->images[PW_UPDATE_S3].sha256, 32) &&
             c->j.target_bytes == target->bytes &&
             c->j.security_version == target->security_version &&
             !strcmp(c->j.version, m->version) && !strcmp(c->j.release_id, m->release_id);
    }
    pw_update_manifest_free(manifest);
    free(proof);
    return ok;
}
static bool tx_matches(const pw_companion_update_core_t *c, const pw_frame_t *f) {
    return !memcmp(f->payload + 1, c->j.tx, 16);
}
static bool commit_binding(const pw_companion_update_core_t *c, const pw_frame_t *f) {
    return f->length >= 53 && !memcmp(f->payload + 17, c->j.manifest, 32) &&
           u32(f->payload + 49) == c->boot_nonce;
}
static void ack(pw_companion_update_core_t *c, const pw_frame_t *f, pw_frame_t *r,
                pw_ota_result_t result) {
    memset(r, 0, sizeof(*r));
    r->role = PW_ROLE_COMPANION;
    r->kind = PW_MSG_OTA_ACK;
    r->session = c->boot_nonce;
    r->sequence = ++c->reply_sequence;
    r->length = PW_OTA_ACK_BYTES;
    r->payload[0] = PW_UPDATE_WIRE_VERSION;
    if (f->length >= 17)
        memcpy(r->payload + 1, f->payload + 1, 16);
    r->payload[17] = f->kind;
    r->payload[18] = (uint8_t)result;
    r->payload[19] = (uint8_t)(c->trusted ? c->j.phase : PW_OTA_LOCKED);
    put32(r->payload + 20, f->sequence);
    put32(r->payload + 24, c->j.phase == PW_OTA_META ? c->metadata_received : c->j.received);
    put32(r->payload + 28, c->boot_nonce);
    memcpy(r->payload + 32, c->j.manifest, 32);
    put32(r->payload + 64, c->j.target_bytes);
}
static bool base_valid(const pw_frame_t *f) {
    return f->length >= 17 && f->payload[0] == PW_UPDATE_WIRE_VERSION &&
           nonzero(f->payload + 1, 16);
}
static pw_ota_result_t begin(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 20)
        return PW_OTA_ERR_FORMAT;
    if (c->pending_boot || (c->j.phase != PW_OTA_IDLE && c->j.phase != PW_OTA_COMPLETE))
        return PW_OTA_ERR_STATE;
    uint16_t length = u16(f->payload + 17);
    uint8_t signature = f->payload[19];
    if (!length || length > PW_UPDATE_MANIFEST_MAX || signature < 8 || signature > 72)
        return PW_OTA_ERR_FORMAT;
    clear_staging(c);
    c->metadata = malloc((size_t)length + signature);
    if (!c->metadata)
        return PW_OTA_ERR_MEMORY;
    c->manifest_size = length;
    c->signature_size = signature;
    c->metadata_size = length + signature;
    c->metadata_received = 0;
    uint32_t generation = c->j.generation;
    memset(&c->j, 0, sizeof(c->j));
    c->j.generation = generation;
    memcpy(c->j.tx, f->payload + 1, 16);
    c->peer_session = f->session;
    if (!save_phase(c, PW_OTA_META)) {
        clear_staging(c);
        return PW_OTA_ERR_JOURNAL;
    }
    return PW_OTA_OK;
}
static pw_ota_result_t metadata_chunk(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (c->j.phase != PW_OTA_META || !c->metadata)
        return PW_OTA_ERR_STATE;
    if (f->length < 20)
        return PW_OTA_ERR_FORMAT;
    uint32_t offset = u16(f->payload + 17);
    size_t count = f->length - 19;
    if (offset > c->metadata_size || count > c->metadata_size - offset)
        return PW_OTA_ERR_OFFSET;
    if (offset < c->metadata_received) {
        if (count > c->metadata_received - offset)
            return PW_OTA_ERR_OFFSET;
        return memcmp(c->metadata + offset, f->payload + 19, count) ? PW_OTA_ERR_CONFLICT
                                                                    : PW_OTA_OK;
    }
    if (offset != c->metadata_received)
        return PW_OTA_ERR_OFFSET;
    memcpy(c->metadata + offset, f->payload + 19, count);
    c->metadata_received += (uint16_t)count;
    return PW_OTA_OK;
}
static pw_ota_result_t metadata_verify(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 17)
        return PW_OTA_ERR_FORMAT;
    if (c->j.phase != PW_OTA_META || c->metadata_received != c->metadata_size)
        return PW_OTA_ERR_STATE;
    if (!c->ops.slots(c->ops.context, &c->running, &c->inactive) ||
        c->running.address == c->inactive.address || !c->running.image_bytes)
        return PW_OTA_ERR_FLASH;
    pw_update_policy_t policy = c->config->policy;
    policy.roles[PW_UPDATE_COMPANION].app_slot_bytes = c->inactive.capacity;
    policy.roles[PW_UPDATE_COMPANION].running_version = c->running.version;
    pw_update_result_t result = pw_update_verify_manifest(
        c->metadata, c->manifest_size, c->metadata + c->manifest_size, c->signature_size,
        c->config->public_key, c->config->public_key_bytes, c->config->key_id, &policy,
        &c->manifest);
    if (result != PW_UPDATE_OK) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_VERIFY;
    }
    const pw_update_manifest_info_t *m = pw_update_manifest_info(c->manifest);
    memcpy(c->j.manifest, m->manifest_sha256, 32);
    memcpy(c->j.target_hash, m->images[PW_UPDATE_COMPANION].sha256, 32);
    memcpy(c->j.s3_hash, m->images[PW_UPDATE_S3].sha256, 32);
    memcpy(c->j.old_hash, c->running.image_sha256, 32);
    memcpy(c->j.old_s3_hash, c->current_s3_hash, 32);
    if (mbedtls_sha256(c->config->public_key, c->config->public_key_bytes, c->j.trust_hash, 0)) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_VERIFY;
    }
    c->j.old_security_version = c->running.security_version;
    c->j.old_address = c->running.address;
    c->j.new_address = c->inactive.address;
    c->j.old_bytes = c->running.image_bytes;
    c->j.target_bytes = m->images[PW_UPDATE_COMPANION].bytes;
    c->j.security_version = m->images[PW_UPDATE_COMPANION].security_version;
    memcpy(c->j.old_version, c->running.version, sizeof(c->j.old_version));
    strcpy(c->j.version, m->version);
    strcpy(c->j.release_id, m->release_id);
    if (pw_update_image_begin(c->manifest, PW_UPDATE_COMPANION, &c->image) != PW_UPDATE_OK) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_MEMORY;
    }
    // Persist the actual signed proof and then its transaction binding BEFORE
    // first erase/write. A power cut between these writes cannot authorize boot.
    if (!persist_proof(c)) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_JOURNAL;
    }
    if (!save_phase(c, PW_OTA_RECEIVING)) {
        clear_staging(c);
        return PW_OTA_ERR_JOURNAL;
    }
    if (!c->ops.begin(c->ops.context, c->j.new_address, c->j.target_bytes)) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_FLASH;
    }
    c->stage_open = true;
    free(c->metadata);
    c->metadata = NULL;
    return PW_OTA_OK;
}
static pw_ota_result_t image_chunk(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (c->j.phase != PW_OTA_RECEIVING || !c->stage_open || !c->image)
        return PW_OTA_ERR_STATE;
    if (f->length < 22)
        return PW_OTA_ERR_FORMAT;
    uint32_t offset = u32(f->payload + 17);
    size_t count = f->length - 21;
    if (offset > c->j.target_bytes || count > c->j.target_bytes - offset)
        return PW_OTA_ERR_OFFSET;
    if (offset < c->j.received) {
        if (count > c->j.received - offset)
            return PW_OTA_ERR_OFFSET;
        uint8_t existing[171];
        if (!c->ops.read(c->ops.context, c->j.new_address, offset, existing, count))
            return PW_OTA_ERR_FLASH;
        return memcmp(existing, f->payload + 21, count) ? PW_OTA_ERR_CONFLICT : PW_OTA_OK;
    }
    if (offset != c->j.received)
        return PW_OTA_ERR_OFFSET;
    if (!c->ops.write(c->ops.context, offset, f->payload + 21, count) ||
        pw_update_image_feed(c->image, f->payload + 21, count) != PW_UPDATE_OK) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_FLASH;
    }
    uint32_t before = c->j.received;
    c->j.received += (uint32_t)count;
    // Journal checkpoints are diagnostic, never promises that hashing resumes.
    if (before / 4096 != c->j.received / 4096 && !save_phase(c, PW_OTA_RECEIVING)) {
        clear_staging(c);
        return PW_OTA_ERR_JOURNAL;
    }
    return PW_OTA_OK;
}
static pw_ota_result_t image_verify(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 17)
        return PW_OTA_ERR_FORMAT;
    if (c->j.phase != PW_OTA_RECEIVING || c->j.received != c->j.target_bytes || !c->image)
        return PW_OTA_ERR_STATE;
    if (pw_update_image_finish(&c->image) != PW_UPDATE_OK) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_VERIFY;
    }
    if (!c->ops.finish(c->ops.context)) {
        c->stage_open = false;
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_FLASH;
    }
    c->stage_open = false;
    // Re-read actual flash; verifying only the UART bytes is insufficient.
    if (!slot_hash(c, c->j.new_address, c->j.target_bytes, c->j.target_hash) ||
        !c->ops.validate_slot(c->ops.context, c->j.new_address)) {
        clear_staging(c);
        save_phase(c, PW_OTA_FAILED);
        return PW_OTA_ERR_VERIFY;
    }
    if (!save_phase(c, PW_OTA_VERIFIED))
        return PW_OTA_ERR_JOURNAL;
    return PW_OTA_OK;
}
static pw_ota_result_t activate(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 53 || !commit_binding(c, f))
        return PW_OTA_ERR_TRANSACTION;
    if (c->j.phase != PW_OTA_VERIFIED)
        return PW_OTA_ERR_STATE;
    if (!slot_hash(c, c->j.new_address, c->j.target_bytes, c->j.target_hash) ||
        !c->ops.validate_slot(c->ops.context, c->j.new_address))
        return PW_OTA_ERR_VERIFY;
    if (!save_phase(c, PW_OTA_ACTIVATING))
        return PW_OTA_ERR_JOURNAL;
    if (!c->ops.set_boot(c->ops.context, c->j.new_address)) {
        save_phase(c, PW_OTA_RECOVERY);
        return PW_OTA_ERR_FLASH;
    }
    c->reboot = true;
    return PW_OTA_OK;
}
static pw_ota_result_t pair_health(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 93 || !commit_binding(c, f) || u32(f->payload + 53) != f->session)
        return PW_OTA_ERR_TRANSACTION;
    if (c->j.phase != PW_OTA_BOOT_PENDING && c->j.phase != PW_OTA_PEER_HEALTHY &&
        c->j.phase != PW_OTA_COMPLETE)
        return PW_OTA_ERR_STATE;
    if (memcmp(f->payload + 57, c->j.s3_hash, 32) ||
        (u32(f->payload + 89) & PW_OTA_REQUIRED_HEALTH) != PW_OTA_REQUIRED_HEALTH ||
        c->running.address != c->j.new_address || !c->storage_ok)
        return PW_OTA_ERR_HEALTH;
    c->peer_session = f->session;
    c->peer_health_ms = c->ops.monotonic_ms(c->ops.context);
    if (c->j.phase != PW_OTA_COMPLETE && !save_phase(c, PW_OTA_PEER_HEALTHY))
        return PW_OTA_ERR_JOURNAL;
    return PW_OTA_OK;
}
static pw_ota_result_t commit(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 53 || !commit_binding(c, f))
        return PW_OTA_ERR_TRANSACTION;
    if (c->j.phase == PW_OTA_COMPLETE)
        return PW_OTA_OK;
    uint64_t now = c->ops.monotonic_ms(c->ops.context);
    if (c->j.phase != PW_OTA_PEER_HEALTHY || c->peer_session != f->session || !c->peer_health_ms ||
        now < c->peer_health_ms || now - c->peer_health_ms > PAIR_HEALTH_TTL_MS)
        return PW_OTA_ERR_HEALTH;
    if (!save_phase(c, PW_OTA_COMMITTING))
        return PW_OTA_ERR_JOURNAL;
    // Never called by HELLO/HEARTBEAT or before both images have actual pair health.
    if (!c->ops.mark_valid(c->ops.context)) {
        save_phase(c, PW_OTA_RECOVERY);
        return PW_OTA_ERR_FLASH;
    }
    c->pending_boot = false;
    if (!save_phase(c, PW_OTA_COMPLETE))
        return PW_OTA_ERR_JOURNAL;
    memcpy(c->current_s3_hash, c->j.s3_hash, 32);
    return PW_OTA_OK;
}
static pw_ota_result_t rollback(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 89 || !commit_binding(c, f) || u32(f->payload + 85) != f->session)
        return PW_OTA_ERR_TRANSACTION;
    if (c->j.phase < PW_OTA_ACTIVATING || c->j.phase == PW_OTA_LOCKED || !c->j.old_address ||
        c->j.old_security_version < c->config->policy.roles[PW_UPDATE_COMPANION].security_floor ||
        !nonzero(c->j.old_s3_hash, 32) || memcmp(f->payload + 53, c->j.old_s3_hash, 32))
        return PW_OTA_ERR_HEALTH;
    if (!slot_hash(c, c->j.old_address, c->j.old_bytes, c->j.old_hash) ||
        !c->ops.validate_slot(c->ops.context, c->j.old_address))
        return PW_OTA_ERR_VERIFY;
    if (!save_phase(c, PW_OTA_ROLLBACK_PENDING))
        return PW_OTA_ERR_JOURNAL;
    if (!c->ops.set_boot(c->ops.context, c->j.old_address)) {
        save_phase(c, PW_OTA_RECOVERY);
        return PW_OTA_ERR_FLASH;
    }
    c->reboot = true;
    return PW_OTA_OK;
}
static pw_ota_result_t abort_update(pw_companion_update_core_t *c, const pw_frame_t *f) {
    if (f->length != 17)
        return PW_OTA_ERR_FORMAT;
    if (c->pending_boot || c->running.address == c->j.new_address ||
        c->j.phase == PW_OTA_ACTIVATING || c->j.phase == PW_OTA_ROLLBACK_PENDING)
        return PW_OTA_ERR_STATE;
    clear_staging(c);
    uint32_t generation = c->j.generation;
    memset(&c->j, 0, sizeof(c->j));
    c->j.generation = generation;
    return save_phase(c, PW_OTA_IDLE) ? PW_OTA_OK : PW_OTA_ERR_JOURNAL;
}
pw_companion_update_core_t *
pw_companion_update_core_create(const pw_companion_update_config_t *config,
                                const pw_companion_update_ops_t *ops, uint32_t boot_nonce) {
    if (!ops || !boot_nonce || !ops->load || !ops->save || !ops->load_proof || !ops->save_proof ||
        !ops->slots || !ops->begin || !ops->write || !ops->read || !ops->finish || !ops->abort ||
        !ops->validate_slot || !ops->set_boot || !ops->mark_valid || !ops->pending_verify ||
        !ops->monotonic_ms)
        return NULL;
    pw_companion_update_core_t *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->config = config;
    c->ops = *ops;
    c->boot_nonce = boot_nonce;
    c->created_ms = ops->monotonic_ms(ops->context);
    c->trusted = config && config->public_key && config->public_key_bytes && config->key_id &&
                 nonzero(config->current_s3_sha256, 32);
    if (c->trusted)
        memcpy(c->current_s3_hash, config->current_s3_sha256, 32);
    c->pending_boot = ops->pending_verify(ops->context);
    uint8_t bytes[PW_COMPANION_JOURNAL_BYTES];
    int loaded = ops->load(ops->context, bytes);
    c->storage_ok = loaded >= 0;
    bool slots = ops->slots(ops->context, &c->running, &c->inactive);
    if (loaded < 0 || !slots || (loaded == 1 && !decode(bytes, &c->j))) {
        c->j.phase = PW_OTA_RECOVERY;
        c->storage_ok = false;
        return c;
    }
    if (loaded == 0) {
        c->j.phase = c->pending_boot ? PW_OTA_RECOVERY : PW_OTA_IDLE;
        return c;
    }
    if (c->j.phase == PW_OTA_IDLE) {
        if (c->pending_boot)
            c->j.phase = PW_OTA_RECOVERY;
        return c;
    }
    if (c->j.target_bytes && c->trusted) {
        uint8_t trust[32];
        if (mbedtls_sha256(config->public_key, config->public_key_bytes, trust, 0)) {
            c->trusted = false;
            c->j.phase = PW_OTA_RECOVERY;
            return c;
        }
        if (memcmp(trust, c->j.trust_hash, 32) ||
            c->j.security_version < config->policy.roles[PW_UPDATE_COMPANION].security_floor ||
            !authenticate_journal(c)) {
            c->trusted = false;
            c->j.phase = PW_OTA_RECOVERY;
            return c;
        }
    }
    if (c->running.address == c->j.new_address &&
        slot_hash(c, c->j.new_address, c->j.target_bytes, c->j.target_hash)) {
        memcpy(c->running.image_sha256, c->j.target_hash, 32);
        c->running.image_bytes = c->j.target_bytes;
        if (c->j.phase != PW_OTA_COMPLETE || c->pending_boot) {
            c->peer_health_ms = 0;
            save_phase(c, PW_OTA_BOOT_PENDING);
        } else
            memcpy(c->current_s3_hash, c->j.s3_hash, 32);
    } else {
        // A partial stage, old S3 rollback or lost post-commit write is not success.
        // Metadata/hash state is intentionally restarted from an authenticated BEGIN.
        if (c->running.address == c->j.old_address &&
            slot_hash(c, c->j.old_address, c->j.old_bytes, c->j.old_hash))
            memcpy(c->current_s3_hash, c->j.old_s3_hash, 32);
        save_phase(c, PW_OTA_RECOVERY);
    }
    return c;
}
void pw_companion_update_core_destroy(pw_companion_update_core_t *c) {
    if (c) {
        clear_staging(c);
        free(c);
    }
}
bool pw_companion_update_core_handle(pw_companion_update_core_t *c, const pw_frame_t *f,
                                     pw_frame_t *r) {
    if (!c || !f || !r || f->role != PW_ROLE_S3 || f->kind < PW_MSG_OTA_BEGIN ||
        f->kind > PW_MSG_OTA_STATUS)
        return false;
    pw_ota_result_t result;
    if (!f->session || f->length > PW_PAYLOAD_MAX || !base_valid(f)) {
        ack(c, f, r, PW_OTA_ERR_FORMAT);
        return true;
    }
    bool same_session = c->have_last && c->last_session == f->session;
    if (same_session && f->sequence == c->last_sequence) {
        if (f->kind == c->last_request.kind && f->length == c->last_request.length &&
            !memcmp(f->payload, c->last_request.payload, f->length)) {
            *r = c->last_reply;
            return true;
        }
        ack(c, f, r, PW_OTA_ERR_REPLAY);
        return true;
    }
    if (same_session && (int32_t)(f->sequence - c->last_sequence) <= 0) {
        ack(c, f, r, PW_OTA_ERR_REPLAY);
        return true;
    }
    if (f->kind == PW_MSG_OTA_STATUS)
        result = f->length == 17 ? PW_OTA_OK : PW_OTA_ERR_FORMAT;
    else if (!c->trusted)
        result = PW_OTA_ERR_LOCKED;
    else if (f->kind == PW_MSG_OTA_BEGIN)
        result = begin(c, f);
    else if (f->kind == PW_MSG_OTA_ABORT && c->j.phase == PW_OTA_RECOVERY &&
             !nonzero(c->j.tx, 16) && !c->pending_boot)
        result = abort_update(c, f);
    else if (!tx_matches(c, f))
        result = PW_OTA_ERR_TRANSACTION;
    else if (f->kind <= PW_MSG_OTA_ACTIVATE && f->session != c->peer_session)
        result = PW_OTA_ERR_TRANSACTION;
    else
        switch (f->kind) {
        case PW_MSG_OTA_META_CHUNK:
            result = metadata_chunk(c, f);
            break;
        case PW_MSG_OTA_META_VERIFY:
            result = metadata_verify(c, f);
            break;
        case PW_MSG_OTA_IMAGE_CHUNK:
            result = image_chunk(c, f);
            break;
        case PW_MSG_OTA_IMAGE_VERIFY:
            result = image_verify(c, f);
            break;
        case PW_MSG_OTA_ACTIVATE:
            result = activate(c, f);
            break;
        case PW_MSG_OTA_PAIR_HEALTH:
            result = pair_health(c, f);
            break;
        case PW_MSG_OTA_COMMIT:
            result = commit(c, f);
            break;
        case PW_MSG_OTA_ROLLBACK:
            result = rollback(c, f);
            break;
        case PW_MSG_OTA_ABORT:
            result = abort_update(c, f);
            break;
        default:
            result = PW_OTA_ERR_FORMAT;
            break;
        }
    ack(c, f, r, result);
    c->have_last = true;
    c->last_session = f->session;
    c->last_sequence = f->sequence;
    c->last_request = *f;
    c->last_reply = *r;
    return true;
}
void pw_companion_update_core_report(pw_companion_update_core_t *c, pw_frame_t *r) {
    if (!c || !r)
        return;
    memset(r, 0, sizeof(*r));
    r->role = PW_ROLE_COMPANION;
    r->kind = PW_MSG_OTA_BOOT_REPORT;
    r->session = c->boot_nonce;
    r->sequence = ++c->reply_sequence;
    r->length = PW_OTA_BOOT_REPORT_BYTES;
    r->payload[0] = PW_UPDATE_WIRE_VERSION;
    memcpy(r->payload + 1, c->j.tx, 16);
    r->payload[17] = (uint8_t)(c->trusted ? c->j.phase : PW_OTA_LOCKED);
    put32(r->payload + 18, c->boot_nonce);
    memcpy(r->payload + 22, c->j.manifest, 32);
    memcpy(r->payload + 54, c->running.image_sha256, 32);
    put32(r->payload + 86, c->running.address);
    put32(r->payload + 90, c->storage_ok && c->running.image_bytes ? 3 : 0);
    memcpy(r->payload + 94, c->running.version, 49);
    if (!c->trusted || c->j.phase == PW_OTA_IDLE) {
        memset(r->payload + 22, 0, 64);
    }
}
void pw_companion_update_core_tick(pw_companion_update_core_t *c) {
    if (!c || c->reboot)
        return;
    uint64_t now = c->ops.monotonic_ms(c->ops.context);
    if (c->j.phase == PW_OTA_PEER_HEALTHY && now - c->peer_health_ms > PAIR_HEALTH_TTL_MS) {
        c->peer_health_ms = 0;
        save_phase(c, PW_OTA_BOOT_PENDING);
    }
    if (c->pending_boot && now - c->created_ms > UNCONFIRMED_BOOT_TIMEOUT_MS) {
        // Reset an unconfirmed app: IDF bootloader selects its existing valid fallback.
        // No mark-valid and no inferred pair health merely because UART stayed alive.
        c->reboot = true;
    }
}
bool pw_companion_update_core_reboot_requested(pw_companion_update_core_t *c) {
    return c && c->reboot;
}
