// SPDX-License-Identifier: MIT
#include "mbedtls/sha256.h"
#include "pw_companion_update_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OLD_ADDR 0x10000u
#define NEW_ADDR 0x1e0000u
#define CAPACITY 4096u
static unsigned assertions;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++assertions;                                                                              \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
typedef struct {
    uint8_t slots[2][CAPACITY], journal[PW_COMPANION_JOURNAL_BYTES];
    uint8_t proof[PW_COMPANION_PROOF_MAX];
    size_t proof_size;
    size_t lengths[2];
    unsigned active, boot, writes, erases, boots, marks, saves;
    bool has_journal, stage, pending, fail_save, fail_complete, fail_boot, fail_write, fail_proof;
    uint64_t ms;
} mock_t;
static uint8_t *json, *signature, *pub, *old_image, *new_image;
static size_t json_size, signature_size, pub_size, old_size, new_size;
static uint8_t manifest_hash[32], s3_hash[32];
static pw_companion_update_config_t config;
static unsigned sequence;
static uint32_t peer_session = 0x12345678;
static unsigned slot(uint32_t address) {
    return address == OLD_ADDR ? 0 : address == NEW_ADDR ? 1 : 99;
}
static int load(void *p, uint8_t *out) {
    mock_t *m = p;
    if (!m->has_journal)
        return 0;
    memcpy(out, m->journal, sizeof(m->journal));
    return 1;
}
static bool save(void *p, const uint8_t *in) {
    mock_t *m = p;
    ++m->saves;
    if (m->fail_save || (m->fail_complete && in[5] == PW_OTA_COMPLETE))
        return false;
    memcpy(m->journal, in, sizeof(m->journal));
    m->has_journal = true;
    return true;
}
static bool load_proof(void *p, uint8_t *out, size_t *size) {
    mock_t *m = p;
    if (!m->proof_size || m->proof_size > *size)
        return false;
    memcpy(out, m->proof, m->proof_size);
    *size = m->proof_size;
    return true;
}
static bool save_proof(void *p, const uint8_t *in, size_t size) {
    mock_t *m = p;
    if (m->fail_proof || size > sizeof(m->proof))
        return false;
    memcpy(m->proof, in, size);
    m->proof_size = size;
    return true;
}
static bool slots(void *p, pw_companion_slot_t *r, pw_companion_slot_t *i) {
    mock_t *m = p;
    memset(r, 0, sizeof(*r));
    memset(i, 0, sizeof(*i));
    r->address = m->active ? NEW_ADDR : OLD_ADDR;
    r->capacity = CAPACITY;
    r->image_bytes = (uint32_t)m->lengths[m->active];
    r->security_version = 1;
    memcpy(r->version, m->slots[m->active] + 48, 32);
    mbedtls_sha256(m->slots[m->active], r->image_bytes, r->image_sha256, 0);
    i->address = m->active ? OLD_ADDR : NEW_ADDR;
    i->capacity = CAPACITY;
    return true;
}
static bool begin(void *p, uint32_t address, uint32_t n) {
    mock_t *m = p;
    unsigned s = slot(address);
    if (s > 1 || s == m->active || n > CAPACITY)
        return false;
    memset(m->slots[s], 0xff, CAPACITY);
    m->lengths[s] = n;
    m->stage = true;
    ++m->erases;
    return true;
}
static bool write_chunk(void *p, uint32_t at, const uint8_t *bytes, size_t n) {
    mock_t *m = p;
    if (!m->stage || m->fail_write || at + n > CAPACITY)
        return false;
    memcpy(m->slots[1 - m->active] + at, bytes, n);
    ++m->writes;
    return true;
}
static bool read_slot(void *p, uint32_t address, uint32_t at, uint8_t *out, size_t n) {
    mock_t *m = p;
    unsigned s = slot(address);
    if (s > 1 || at + n > CAPACITY)
        return false;
    memcpy(out, m->slots[s] + at, n);
    return true;
}
static bool finish(void *p) {
    mock_t *m = p;
    if (!m->stage)
        return false;
    m->stage = false;
    return true;
}
static void abort_stage(void *p) {
    ((mock_t *)p)->stage = false;
}
static bool validate(void *p, uint32_t address) {
    mock_t *m = p;
    unsigned s = slot(address);
    return s < 2 && m->slots[s][0] == 0xe9;
}
static bool set_boot(void *p, uint32_t address) {
    mock_t *m = p;
    unsigned s = slot(address);
    if (m->fail_boot || s > 1)
        return false;
    m->boot = s;
    ++m->boots;
    return true;
}
static bool mark(void *p) {
    mock_t *m = p;
    ++m->marks;
    m->pending = false;
    return true;
}
static bool pending(void *p) {
    return ((mock_t *)p)->pending;
}
static uint64_t now(void *p) {
    return ((mock_t *)p)->ms;
}
static void init_mock(mock_t *m) {
    memset(m, 0, sizeof(*m));
    memcpy(m->slots[0], old_image, old_size);
    m->lengths[0] = old_size;
    m->ms = 100;
    sequence = 0;
    peer_session = 0x12345678;
}
static pw_companion_update_core_t *create(mock_t *m, uint32_t nonce, bool trusted) {
    pw_companion_update_ops_t ops = {.context = m,
                                     .load = load,
                                     .save = save,
                                     .load_proof = load_proof,
                                     .save_proof = save_proof,
                                     .slots = slots,
                                     .begin = begin,
                                     .write = write_chunk,
                                     .read = read_slot,
                                     .finish = finish,
                                     .abort = abort_stage,
                                     .validate_slot = validate,
                                     .set_boot = set_boot,
                                     .mark_valid = mark,
                                     .pending_verify = pending,
                                     .monotonic_ms = now};
    pw_companion_update_core_t *c =
        pw_companion_update_core_create(trusted ? &config : NULL, &ops, nonce);
    CHECK(c);
    return c;
}
static void p16(uint8_t *p, unsigned n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static void p32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(n >> (8 * i));
}
static uint32_t g32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static pw_frame_t request(uint8_t kind, uint16_t size) {
    pw_frame_t f = {.role = PW_ROLE_S3,
                    .kind = kind,
                    .session = peer_session,
                    .sequence = ++sequence,
                    .length = size};
    f.payload[0] = 1;
    memset(f.payload + 1, 0x33, 16);
    return f;
}
static pw_frame_t send(pw_companion_update_core_t *c, pw_frame_t *f, pw_ota_result_t expected) {
    pw_frame_t reply;
    CHECK(pw_companion_update_core_handle(c, f, &reply));
    CHECK(reply.kind == PW_MSG_OTA_ACK && reply.length == PW_OTA_ACK_BYTES);
    if (reply.payload[18] != expected) {
        fprintf(stderr, "kind=%u got=%u expected=%u phase=%u\n", f->kind, reply.payload[18],
                expected, reply.payload[19]);
        exit(1);
    }
    ++assertions;
    CHECK(reply.payload[17] == f->kind && g32(reply.payload + 20) == f->sequence);
    return reply;
}
static void start_result(pw_companion_update_core_t *c, pw_ota_result_t result) {
    pw_frame_t f = request(PW_MSG_OTA_BEGIN, 20);
    p16(f.payload + 17, (unsigned)json_size);
    f.payload[19] = (uint8_t)signature_size;
    send(c, &f, PW_OTA_OK);
    uint8_t metadata[16456];
    memcpy(metadata, json, json_size);
    memcpy(metadata + json_size, signature, signature_size);
    for (size_t at = 0; at < json_size + signature_size;) {
        size_t n = json_size + signature_size - at > 173 ? 173 : json_size + signature_size - at;
        f = request(PW_MSG_OTA_META_CHUNK, (uint16_t)(19 + n));
        p16(f.payload + 17, (unsigned)at);
        memcpy(f.payload + 19, metadata + at, n);
        send(c, &f, PW_OTA_OK);
        at += n;
    }
    f = request(PW_MSG_OTA_META_VERIFY, 17);
    send(c, &f, result);
}
static void start(pw_companion_update_core_t *c) {
    start_result(c, PW_OTA_OK);
}
static void stream(pw_companion_update_core_t *c) {
    for (size_t at = 0; at < new_size;) {
        size_t n = new_size - at > 171 ? 171 : new_size - at;
        pw_frame_t f = request(PW_MSG_OTA_IMAGE_CHUNK, (uint16_t)(21 + n));
        p32(f.payload + 17, (uint32_t)at);
        memcpy(f.payload + 21, new_image + at, n);
        send(c, &f, PW_OTA_OK);
        at += n;
    }
}
static void verified(pw_companion_update_core_t *c) {
    start(c);
    stream(c);
    pw_frame_t f = request(PW_MSG_OTA_IMAGE_VERIFY, 17);
    send(c, &f, PW_OTA_OK);
}
static pw_frame_t bound(uint8_t kind, uint16_t size, uint32_t nonce) {
    pw_frame_t f = request(kind, size);
    memcpy(f.payload + 17, manifest_hash, 32);
    p32(f.payload + 49, nonce);
    return f;
}
static pw_companion_update_core_t *boot_new(mock_t *m, pw_companion_update_core_t *c,
                                            uint32_t oldnonce, uint32_t newnonce) {
    pw_frame_t f = bound(PW_MSG_OTA_ACTIVATE, 53, oldnonce);
    send(c, &f, PW_OTA_OK);
    CHECK(pw_companion_update_core_reboot_requested(c));
    CHECK(m->marks == 0 && m->active == 0 && m->boot == 1);
    pw_companion_update_core_destroy(c);
    m->active = m->boot;
    m->pending = true;
    m->ms += 100;
    sequence = 0;
    peer_session = 0x12345679;
    return create(m, newnonce, true);
}
static void health(pw_companion_update_core_t *c, uint32_t nonce, pw_ota_result_t expected,
                   bool wrong_hash) {
    pw_frame_t f = bound(PW_MSG_OTA_PAIR_HEALTH, 93, nonce);
    p32(f.payload + 53, f.session);
    memcpy(f.payload + 57, s3_hash, 32);
    if (wrong_hash)
        f.payload[57] ^= 1;
    p32(f.payload + 89, PW_OTA_REQUIRED_HEALTH);
    send(c, &f, expected);
}
static uint8_t *read_file(const char *name, size_t *size) {
    FILE *f = fopen(name, "rb");
    CHECK(f);
    CHECK(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    rewind(f);
    CHECK(n > 0);
    uint8_t *p = malloc((size_t)n + 1);
    CHECK(p);
    CHECK(fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    p[n] = 0;
    *size = (size_t)n;
    return p;
}
int main(int argc, char **argv) {
    CHECK(argc == 6);
    json = read_file(argv[1], &json_size);
    signature = read_file(argv[2], &signature_size);
    pub = read_file(argv[3], &pub_size);
    old_image = read_file(argv[4], &old_size);
    new_image = read_file(argv[5], &new_size);
    CHECK(new_size <= CAPACITY && old_size <= CAPACITY);
    config =
        (pw_companion_update_config_t){.public_key = pub,
                                       .public_key_bytes = pub_size,
                                       .key_id = "test-p256",
                                       .policy = {.roles = {{.hardware_id = "jc3636k518c-esp32",
                                                             .bootloader_version = "1.0.0",
                                                             .running_version = "0.1.0",
                                                             .app_slot_bytes = CAPACITY,
                                                             .security_floor = 1,
                                                             .emitted_protocol = 1,
                                                             .accepted_peer_min = 1,
                                                             .accepted_peer_max = 2},
                                                            {.hardware_id = "jc3636k518c-s3",
                                                             .bootloader_version = "1.0.0",
                                                             .running_version = "0.1.0",
                                                             .app_slot_bytes = 6291456,
                                                             .security_floor = 1,
                                                             .emitted_protocol = 1,
                                                             .accepted_peer_min = 1,
                                                             .accepted_peer_max = 2}},
                                                  .current_config_schema = 1,
                                                  .allow_beta = true}};
    memset(config.current_s3_sha256, 0xa5, 32);
    pw_update_manifest_t *v = NULL;
    CHECK(pw_update_verify_manifest(json, json_size, signature, signature_size, pub, pub_size,
                                    "test-p256", &config.policy, &v) == PW_UPDATE_OK);
    memcpy(manifest_hash, pw_update_manifest_info(v)->manifest_sha256, 32);
    memcpy(s3_hash, pw_update_manifest_info(v)->images[1].sha256, 32);
    pw_update_manifest_free(v);
    mock_t m;
    init_mock(&m);
    pw_companion_update_core_t *c = create(&m, 11, false);
    pw_frame_t f = request(PW_MSG_OTA_BEGIN, 20);
    p16(f.payload + 17, (unsigned)json_size);
    f.payload[19] = (uint8_t)signature_size;
    send(c, &f, PW_OTA_ERR_LOCKED);
    CHECK(!m.erases && !m.writes && !m.boots && !m.marks);
    pw_frame_t report;
    pw_companion_update_core_report(c, &report);
    CHECK(report.payload[17] == PW_OTA_LOCKED && g32(report.payload + 18) == 11);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 111, true);
    signature[0] ^= 1;
    start_result(c, PW_OTA_ERR_VERIFY);
    signature[0] ^= 1;
    CHECK(!m.erases && !m.writes && !m.boots);
    f = request(PW_MSG_OTA_STATUS, 193);
    send(c, &f, PW_OTA_ERR_FORMAT);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 12, true);
    start(c);
    CHECK(m.erases == 1 && !m.boots);
    f = request(PW_MSG_OTA_IMAGE_CHUNK, 31);
    p32(f.payload + 17, 0);
    memcpy(f.payload + 21, new_image, 10);
    pw_frame_t first = send(c, &f, PW_OTA_OK), again = send(c, &f, PW_OTA_OK);
    CHECK(!memcmp(first.payload, again.payload, first.length) && m.writes == 1);
    f.payload[21] ^= 1;
    send(c, &f, PW_OTA_ERR_REPLAY);
    f.sequence = ++sequence;
    send(c, &f, PW_OTA_ERR_CONFLICT);
    CHECK(m.writes == 1);
    f.payload[21] ^= 1;
    f.sequence = ++sequence;
    send(c, &f, PW_OTA_OK);
    CHECK(m.writes == 1);
    f.sequence = ++sequence;
    p32(f.payload + 17, 11);
    send(c, &f, PW_OTA_ERR_OFFSET);
    f = request(PW_MSG_OTA_IMAGE_VERIFY, 17);
    send(c, &f, PW_OTA_ERR_STATE);
    CHECK(!m.boots && !m.marks && !memcmp(m.slots[0], old_image, old_size));
    pw_companion_update_core_destroy(c);
    c = create(&m, 13, true);
    pw_companion_update_core_report(c, &report);
    CHECK(report.payload[17] == PW_OTA_RECOVERY);
    f = request(PW_MSG_OTA_ABORT, 17);
    send(c, &f, PW_OTA_OK);
    verified(c);
    CHECK(!m.boots);
    m.slots[1][400] ^= 1;
    f = bound(PW_MSG_OTA_ACTIVATE, 53, 13);
    send(c, &f, PW_OTA_ERR_VERIFY);
    CHECK(!m.boots);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 20, true);
    verified(c);
    m.fail_save = true;
    f = bound(PW_MSG_OTA_ACTIVATE, 53, 20);
    send(c, &f, PW_OTA_ERR_JOURNAL);
    CHECK(!m.boots);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 30, true);
    verified(c);
    c = boot_new(&m, c, 30, 31);
    pw_companion_update_core_report(c, &report);
    CHECK(report.payload[17] == PW_OTA_BOOT_PENDING && g32(report.payload + 18) == 31 && !m.marks);
    f = bound(PW_MSG_OTA_COMMIT, 53, 31);
    send(c, &f, PW_OTA_ERR_HEALTH);
    health(c, 30, PW_OTA_ERR_TRANSACTION, false);
    health(c, 31, PW_OTA_ERR_HEALTH, true);
    health(c, 31, PW_OTA_OK, false);
    CHECK(!m.marks);
    m.ms += 30001;
    f = bound(PW_MSG_OTA_COMMIT, 53, 31);
    send(c, &f, PW_OTA_ERR_HEALTH);
    health(c, 31, PW_OTA_OK, false);
    f = bound(PW_MSG_OTA_COMMIT, 53, 31);
    send(c, &f, PW_OTA_OK);
    CHECK(m.marks == 1 && !m.pending && m.journal[5] == PW_OTA_COMPLETE);
    send(c, &f, PW_OTA_OK);
    CHECK(m.marks == 1);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 40, true);
    verified(c);
    c = boot_new(&m, c, 40, 41);
    health(c, 41, PW_OTA_OK, false);
    m.fail_complete = true;
    f = bound(PW_MSG_OTA_COMMIT, 53, 41);
    send(c, &f, PW_OTA_ERR_JOURNAL);
    CHECK(m.marks == 1 && !m.pending && m.journal[5] == PW_OTA_COMMITTING);
    pw_companion_update_core_destroy(c);
    m.fail_complete = false;
    c = create(&m, 42, true);
    pw_companion_update_core_report(c, &report);
    CHECK(report.payload[17] == PW_OTA_BOOT_PENDING && m.marks == 1);
    health(c, 42, PW_OTA_OK, false);
    f = bound(PW_MSG_OTA_COMMIT, 53, 42);
    send(c, &f, PW_OTA_OK);
    CHECK(m.marks == 2 && m.journal[5] == PW_OTA_COMPLETE);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 50, true);
    verified(c);
    c = boot_new(&m, c, 50, 51);
    f = bound(PW_MSG_OTA_ROLLBACK, 89, 51);
    memcpy(f.payload + 53, s3_hash, 32);
    p32(f.payload + 85, f.session);
    send(c, &f, PW_OTA_ERR_HEALTH);
    memcpy(f.payload + 53, config.current_s3_sha256, 32);
    f.sequence = ++sequence;
    send(c, &f, PW_OTA_OK);
    CHECK(m.boot == 0 && m.journal[5] == PW_OTA_ROLLBACK_PENDING && !m.marks);
    pw_companion_update_core_destroy(c);
    m.active = 0;
    m.pending = false;
    c = create(&m, 52, true);
    pw_companion_update_core_report(c, &report);
    CHECK(report.payload[17] == PW_OTA_RECOVERY);
    CHECK(!memcmp(m.slots[0], old_image, old_size));
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 60, true);
    verified(c);
    c = boot_new(&m, c, 60, 61);
    m.ms += 600001;
    pw_companion_update_core_tick(c);
    CHECK(pw_companion_update_core_reboot_requested(c) && !m.marks);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 70, true);
    start(c);
    stream(c);
    m.slots[1][500] ^= 1;
    f = request(PW_MSG_OTA_IMAGE_VERIFY, 17);
    send(c, &f, PW_OTA_ERR_VERIFY);
    CHECK(!m.boots && !m.marks);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    m.has_journal = true;
    memset(m.journal, 0, sizeof(m.journal));
    c = create(&m, 80, true);
    pw_companion_update_core_report(c, &report);
    CHECK(report.payload[17] == PW_OTA_RECOVERY && !m.boots);
    f = request(PW_MSG_OTA_ABORT, 17);
    send(c, &f, PW_OTA_OK);
    CHECK(m.journal[5] == PW_OTA_IDLE && !m.erases && !m.boots);
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 90, true);
    verified(c);
    c = boot_new(&m, c, 90, 91);
    pw_companion_update_core_destroy(c);
    pub[30] ^= 1; // A different locally provisioned trust anchor cannot reuse this proof.
    c = create(&m, 92, true);
    pw_companion_update_core_report(c, &report);
    CHECK(report.payload[17] == PW_OTA_LOCKED && !m.marks);
    pub[30] ^= 1;
    pw_companion_update_core_destroy(c);
    init_mock(&m);
    c = create(&m, 100, true);
    m.fail_proof = true;
    start_result(c, PW_OTA_ERR_JOURNAL);
    CHECK(!m.erases && !m.writes && !m.boots && !m.marks);
    pw_companion_update_core_destroy(c);
    for (unsigned corruption = 0; corruption < 3; ++corruption) {
        init_mock(&m);
        c = create(&m, 110, true);
        verified(c);
        c = boot_new(&m, c, 110, 111);
        pw_companion_update_core_destroy(c);
        if (corruption == 0)
            m.proof_size = 0;
        else if (corruption == 1)
            m.proof[m.proof_size - 1] ^= 1;
        else {
            m.journal[60] ^= 1; // A valid CRC cannot authenticate a changed target hash.
            p16(m.journal + 406, pw_crc16(m.journal, 406));
        }
        c = create(&m, 112, true);
        pw_companion_update_core_report(c, &report);
        CHECK(report.payload[17] == PW_OTA_LOCKED && !m.marks);
        health(c, 112, PW_OTA_ERR_LOCKED, false);
        f = bound(PW_MSG_OTA_COMMIT, 53, 112);
        send(c, &f, PW_OTA_ERR_LOCKED);
        CHECK(!m.marks && m.pending);
        pw_companion_update_core_destroy(c);
    }
    free(json);
    free(signature);
    free(pub);
    free(old_image);
    free(new_image);
    printf("companion receiver: %u assertions passed (real crypto; flash/NVS/powercut model)\n",
           assertions);
    return 0;
}
