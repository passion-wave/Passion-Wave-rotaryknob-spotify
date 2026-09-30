// SPDX-License-Identifier: MIT
#include "pw_update_stage.h"
#include "pw_protocol.h"
#include <stdlib.h>
#include <string.h>

struct pw_update_stage {
    const pw_stage_config_t *config;
    pw_stage_ops_t ops;
    pw_stage_view_t view;
    bool storage_ok, image_open;
    uint8_t header[14];
    size_t header_used, metadata_used, manifest_size, signature_size;
    uint8_t *metadata;
    pw_update_manifest_t *manifest;
    pw_update_image_check_t *image_check;
    pw_update_role_t role;
    uint32_t image_received;
};
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(value >> (i * 8));
}
static bool persist(pw_update_stage_t *s, pw_stage_phase_t phase) {
    if (!s->storage_ok || s->view.generation == UINT32_MAX)
        return false;
    uint8_t journal[PW_STAGE_JOURNAL_BYTES] = {0};
    memcpy(journal, "PWS3STG1", 8);
    put32(journal + 8, s->view.generation + 1);
    journal[12] = (uint8_t)phase;
    memcpy(journal + 16, s->view.transaction, 16);
    memcpy(journal + 32, s->view.manifest_sha256, 32);
    put32(journal + 64, s->view.total);
    put32(journal + 68, s->view.received);
    put32(journal + 72, s->view.image_bytes[0]);
    put32(journal + 76, s->view.image_bytes[1]);
    memcpy(journal + 80, s->view.version, 49);
    put32(journal + 132, s->view.peer_received);
    uint16_t crc = pw_crc16(journal, PW_STAGE_JOURNAL_BYTES - 2);
    journal[142] = (uint8_t)crc;
    journal[143] = (uint8_t)(crc >> 8);
    if (!s->ops.save(s->ops.context, journal)) {
        s->storage_ok = false;
        return false;
    }
    s->view.generation++;
    s->view.phase = phase;
    return true;
}
static void release_upload(pw_update_stage_t *s) {
    pw_update_image_abort(&s->image_check);
    pw_update_manifest_free(s->manifest);
    s->manifest = NULL;
    free(s->metadata);
    s->metadata = NULL;
    if (s->image_open)
        s->ops.abort(s->ops.context);
    s->image_open = false;
}
void pw_update_stage_abort(pw_update_stage_t *s, pw_update_result_t error) {
    if (!s || s->view.phase == PW_STAGE_LOCKED)
        return;
    release_upload(s);
    s->view.error = error;
    if (!persist(s, PW_STAGE_FAILED))
        s->view.phase = PW_STAGE_LOCKED;
}
static bool fail(pw_update_stage_t *s, pw_update_result_t error) {
    pw_update_stage_abort(s, error);
    return false;
}
pw_update_stage_t *pw_update_stage_create(const pw_stage_config_t *config,
                                          const pw_stage_ops_t *ops) {
    if (!ops || !ops->load || !ops->save || !ops->begin || !ops->write || !ops->finish ||
        !ops->read || !ops->abort)
        return NULL;
    pw_update_stage_t *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->config = config;
    s->ops = *ops;
    s->view.phase = PW_STAGE_LOCKED;
    uint8_t journal[PW_STAGE_JOURNAL_BYTES];
    int loaded = ops->load(ops->context, journal);
    if (loaded < 0)
        return s;
    s->storage_ok = true;
    if (loaded) {
        uint16_t crc = (uint16_t)journal[142] | (uint16_t)journal[143] << 8;
        if (memcmp(journal, "PWS3STG1", 8) || pw_crc16(journal, 142) != crc || !le32(journal + 8) ||
            journal[12] < PW_STAGE_RECEIVING || journal[12] > PW_STAGE_RECOVERY || journal[13] ||
            journal[14] || journal[15] || !memchr(journal + 80, 0, 49)) {
            s->storage_ok = false;
            return s;
        }
        s->view.generation = le32(journal + 8);
        memcpy(s->view.transaction, journal + 16, 16);
        memcpy(s->view.manifest_sha256, journal + 32, 32);
        s->view.total = le32(journal + 64);
        s->view.received = le32(journal + 68);
        s->view.image_bytes[0] = le32(journal + 72);
        s->view.image_bytes[1] = le32(journal + 76);
        memcpy(s->view.version, journal + 80, 49);
        s->view.peer_received = le32(journal + 132);
        if (s->view.received > s->view.total || s->view.peer_received > s->view.image_bytes[0]) {
            s->storage_ok = false;
            return s;
        }
        s->view.phase = PW_STAGE_RECOVERY; /* Never resume or trust staged flash after reset. */
    } else
        s->view.phase = PW_STAGE_IDLE;
    if (!config || !config->policy_qualified || !config->public_key || !config->public_key_bytes ||
        !config->key_id || !*config->key_id)
        s->view.phase = PW_STAGE_LOCKED;
    return s;
}
void pw_update_stage_destroy(pw_update_stage_t *s) {
    if (s) {
        release_upload(s);
        free(s);
    }
}
bool pw_update_stage_begin(pw_update_stage_t *s, uint32_t total, const uint8_t transaction[16]) {
    if (!s || !transaction || s->view.phase == PW_STAGE_LOCKED ||
        (s->view.phase != PW_STAGE_IDLE && s->view.phase != PW_STAGE_FAILED &&
         s->view.phase != PW_STAGE_RECOVERY && s->view.phase != PW_STAGE_PREPARED))
        return false;
    uint8_t nonzero = 0;
    for (unsigned i = 0; i < 16; i++)
        nonzero |= transaction[i];
    uint64_t maximum = 14u + PW_UPDATE_MANIFEST_MAX + 72u;
    maximum += s->config->policy.roles[0].app_slot_bytes;
    maximum += s->config->policy.roles[1].app_slot_bytes;
    if (!nonzero || total < 14u + 8u + 576u || total > maximum)
        return false;
    release_upload(s);
    uint32_t generation = s->view.generation;
    memset(&s->view, 0, sizeof s->view);
    s->view.generation = generation;
    s->view.total = total;
    memcpy(s->view.transaction, transaction, 16);
    s->header_used = s->metadata_used = s->manifest_size = s->signature_size = 0;
    s->image_received = 0;
    s->role = PW_UPDATE_COMPANION;
    if (!persist(s, PW_STAGE_RECEIVING)) {
        s->view.phase = PW_STAGE_LOCKED;
        return false;
    }
    return true;
}
static bool authenticate(pw_update_stage_t *s) {
    pw_update_result_t result = pw_update_verify_manifest(
        s->metadata, s->manifest_size, s->metadata + s->manifest_size, s->signature_size,
        s->config->public_key, s->config->public_key_bytes, s->config->key_id, &s->config->policy,
        &s->manifest);
    if (result != PW_UPDATE_OK)
        return fail(s, result);
    const pw_update_manifest_info_t *info = pw_update_manifest_info(s->manifest);
    uint64_t expected = 14u + s->manifest_size + s->signature_size;
    for (unsigned i = 0; i < 2; i++) {
        s->view.image_bytes[i] = info->images[i].bytes;
        expected += info->images[i].bytes;
    }
    if (expected != s->view.total)
        return fail(s, PW_UPDATE_SIZE);
    memcpy(s->view.manifest_sha256, info->manifest_sha256, 32);
    memcpy(s->view.version, info->version, sizeof s->view.version);
    return true;
}
static bool begin_image(pw_update_stage_t *s) {
    /* Journal before any erase/write. Metadata has already been authenticated. */
    if (!persist(s, s->role == PW_UPDATE_COMPANION ? PW_STAGE_COMPANION : PW_STAGE_S3))
        return fail(s, PW_UPDATE_STATE);
    pw_update_result_t result = pw_update_image_begin(s->manifest, s->role, &s->image_check);
    if (result != PW_UPDATE_OK)
        return fail(s, result);
    if (!s->ops.begin(s->ops.context, s->role, s->view.image_bytes[s->role]))
        return fail(s, PW_UPDATE_STATE);
    s->image_open = true;
    s->image_received = 0;
    return true;
}
static bool finish_image(pw_update_stage_t *s) {
    pw_update_result_t result = pw_update_image_finish(&s->image_check);
    if (result != PW_UPDATE_OK)
        return fail(s, result);
    if (!s->ops.finish(s->ops.context, s->role))
        return fail(s, PW_UPDATE_IMAGE_HEADER);
    s->image_open = false;
    /* Re-read actual staged flash; a successful network hash is insufficient. */
    result = pw_update_image_begin(s->manifest, s->role, &s->image_check);
    if (result != PW_UPDATE_OK)
        return fail(s, result);
    uint8_t *buffer = malloc(PW_STAGE_CHUNK_BYTES);
    if (!buffer)
        return fail(s, PW_UPDATE_MEMORY);
    for (uint32_t at = 0; at < s->view.image_bytes[s->role];) {
        size_t count = s->view.image_bytes[s->role] - at;
        if (count > PW_STAGE_CHUNK_BYTES)
            count = PW_STAGE_CHUNK_BYTES;
        if (!s->ops.read(s->ops.context, s->role, at, buffer, count)) {
            free(buffer);
            return fail(s, PW_UPDATE_STATE);
        }
        result = pw_update_image_feed(s->image_check, buffer, count);
        if (result != PW_UPDATE_OK) {
            free(buffer);
            return fail(s, result);
        }
        at += count;
    }
    free(buffer);
    result = pw_update_image_finish(&s->image_check);
    if (result != PW_UPDATE_OK)
        return fail(s, result);
    return true;
}
bool pw_update_stage_feed(pw_update_stage_t *s, const uint8_t *data, size_t count) {
    if (!s || s->view.phase == PW_STAGE_LOCKED)
        return false;
    if (s->view.phase == PW_STAGE_LOCAL_READY && count)
        return fail(s, PW_UPDATE_SIZE);
    if (!data || !count || count > PW_STAGE_CHUNK_BYTES || s->view.phase < PW_STAGE_RECEIVING ||
        s->view.phase > PW_STAGE_S3)
        return false;
    if (count > s->view.total - s->view.received)
        return fail(s, PW_UPDATE_SIZE);
    while (count) {
        size_t take;
        if (s->header_used < sizeof s->header) {
            take = sizeof s->header - s->header_used;
            if (take > count)
                take = count;
            memcpy(s->header + s->header_used, data, take);
            s->header_used += take;
            if (s->header_used == sizeof s->header) {
                s->manifest_size = le32(s->header + 8);
                s->signature_size = (size_t)s->header[12] | (size_t)s->header[13] << 8;
                if (memcmp(s->header, "PWOTA1\r\n", 8) || !s->manifest_size ||
                    s->manifest_size > PW_UPDATE_MANIFEST_MAX || s->signature_size < 8 ||
                    s->signature_size > 72)
                    return fail(s, PW_UPDATE_FORMAT);
                s->metadata = malloc(s->manifest_size + s->signature_size);
                if (!s->metadata)
                    return fail(s, PW_UPDATE_MEMORY);
            }
        } else if (s->metadata_used < s->manifest_size + s->signature_size) {
            take = s->manifest_size + s->signature_size - s->metadata_used;
            if (take > count)
                take = count;
            memcpy(s->metadata + s->metadata_used, data, take);
            s->metadata_used += take;
            if (s->metadata_used == s->manifest_size + s->signature_size && !authenticate(s))
                return false;
        } else {
            if (!s->image_open && !begin_image(s))
                return false;
            take = s->view.image_bytes[s->role] - s->image_received;
            if (take > count)
                take = count;
            pw_update_result_t result = pw_update_image_feed(s->image_check, data, take);
            if (result != PW_UPDATE_OK)
                return fail(s, result);
            if (!s->ops.write(s->ops.context, s->role, s->image_received, data, take))
                return fail(s, PW_UPDATE_STATE);
            s->image_received += take;
            if (s->image_received == s->view.image_bytes[s->role]) {
                if (!finish_image(s))
                    return false;
                if (s->role == PW_UPDATE_COMPANION) {
                    s->role = PW_UPDATE_S3;
                    s->image_received = 0;
                } else { /* total length was already checked against signed metadata. */
                    s->view.received += take;
                    if (count != take || s->view.received != s->view.total)
                        return fail(s, PW_UPDATE_SIZE);
                    if (!persist(s, PW_STAGE_LOCAL_READY))
                        return fail(s, PW_UPDATE_STATE);
                    return true;
                }
            }
        }
        s->view.received += take;
        data += take;
        count -= take;
    }
    return true;
}
bool pw_update_stage_finish(pw_update_stage_t *s) {
    if (!s)
        return false;
    if (s->view.phase != PW_STAGE_LOCAL_READY || s->view.received != s->view.total)
        return fail(s, PW_UPDATE_SIZE);
    return true;
}
void pw_update_stage_view(const pw_update_stage_t *s, pw_stage_view_t *out) {
    if (s && out)
        *out = s->view;
}
const uint8_t *pw_update_stage_metadata(const pw_update_stage_t *s, size_t *manifest,
                                        size_t *signature) {
    if (!s || !manifest || !signature || !s->manifest || !s->metadata)
        return NULL;
    *manifest = s->manifest_size;
    *signature = s->signature_size;
    return s->metadata;
}
bool pw_update_stage_peer_begin(pw_update_stage_t *s) {
    return s && s->view.phase == PW_STAGE_LOCAL_READY && persist(s, PW_STAGE_PEER);
}
void pw_update_stage_peer_progress(pw_update_stage_t *s, uint32_t offset) {
    if (s && s->view.phase == PW_STAGE_PEER && offset >= s->view.peer_received &&
        offset <= s->view.image_bytes[0])
        s->view.peer_received = offset;
}
bool pw_update_stage_peer_verified(pw_update_stage_t *s) {
    return s && s->view.phase == PW_STAGE_PEER && s->view.peer_received == s->view.image_bytes[0] &&
           persist(s, PW_STAGE_PREPARED);
}
const char *pw_update_stage_phase_name(pw_stage_phase_t phase) {
    static const char *const names[] = {"locked",
                                        "idle",
                                        "receiving",
                                        "staging_companion",
                                        "staging_s3",
                                        "local_ready",
                                        "transferring_companion",
                                        "prepared",
                                        "failed",
                                        "recovery"};
    return (unsigned)phase <= PW_STAGE_RECOVERY ? names[phase] : "locked";
}
