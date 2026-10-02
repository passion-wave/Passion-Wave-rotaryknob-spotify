// SPDX-License-Identifier: MIT
#include "pw_update_stage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint8_t images[2][4096], journal[PW_STAGE_JOURNAL_BYTES];
    uint32_t bytes[2];
    unsigned erases, writes, saves, finishes;
    bool has_journal, journal_fail, erase_fail, write_fail, finish_fail, corrupt_read;
} fixture_t;
static unsigned assertions;
static void check(bool ok, const char *description) {
    if (!ok)
        fprintf(stderr, "FAIL: %s\n", description);
    assert(ok);
    assertions++;
}
static int load(void *context, uint8_t *out) {
    fixture_t *f = context;
    if (f->has_journal)
        memcpy(out, f->journal, PW_STAGE_JOURNAL_BYTES);
    return f->has_journal;
}
static bool save(void *context, const uint8_t *data) {
    fixture_t *f = context;
    f->saves++;
    if (f->journal_fail)
        return false;
    memcpy(f->journal, data, PW_STAGE_JOURNAL_BYTES);
    f->has_journal = true;
    return true;
}
static bool begin(void *context, pw_update_role_t role, uint32_t bytes) {
    fixture_t *f = context;
    f->erases++;
    if (f->erase_fail || role > 1 || bytes > sizeof f->images[role])
        return false;
    f->bytes[role] = bytes;
    memset(f->images[role], 0xff, sizeof f->images[role]);
    return true;
}
static bool write_image(void *context, pw_update_role_t role, uint32_t offset, const uint8_t *data,
                        size_t count) {
    fixture_t *f = context;
    f->writes++;
    if (f->write_fail || role > 1 || offset > f->bytes[role] || count > f->bytes[role] - offset)
        return false;
    memcpy(f->images[role] + offset, data, count);
    return true;
}
static bool finish_image(void *context, pw_update_role_t role) {
    (void)role;
    fixture_t *f = context;
    f->finishes++;
    return !f->finish_fail;
}
static bool read_image(void *context, pw_update_role_t role, uint32_t offset, uint8_t *data,
                       size_t count) {
    fixture_t *f = context;
    if (role > 1 || offset > f->bytes[role] || count > f->bytes[role] - offset)
        return false;
    memcpy(data, f->images[role] + offset, count);
    if (f->corrupt_read && count)
        data[count - 1] ^= 1;
    return true;
}
static void abort_image(void *context) {
    (void)context;
}
static pw_update_stage_t *create(fixture_t *f, const pw_stage_config_t *config) {
    pw_stage_ops_t ops = {f, load, save, begin, write_image, finish_image, read_image, abort_image};
    pw_update_stage_t *s = pw_update_stage_create(config, &ops);
    assert(s);
    return s;
}
static uint8_t *read_file(const char *name, size_t *size) {
    FILE *file = fopen(name, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long count = ftell(file);
    assert(count > 0);
    rewind(file);
    uint8_t *bytes = malloc((size_t)count + 1);
    assert(bytes);
    assert(fread(bytes, 1, (size_t)count, file) == (size_t)count);
    fclose(file);
    bytes[count] = 0;
    *size = (size_t)count;
    return bytes;
}
static bool feed(pw_update_stage_t *s, const uint8_t *bytes, size_t total, size_t chunk) {
    for (size_t offset = 0; offset < total;) {
        size_t count = total - offset;
        if (count > chunk)
            count = chunk;
        if (!pw_update_stage_feed(s, bytes + offset, count))
            return false;
        offset += count;
    }
    return true;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    size_t bundle_size, key_size;
    uint8_t *bundle = read_file(argv[1], &bundle_size), *key = read_file(argv[2], &key_size);
    const uint8_t transaction[16] = {1, 2, 3};
    pw_stage_config_t config = {
        .public_key = key,
        .public_key_bytes = key_size + 1,
        .key_id = "test-p256",
        .policy_qualified = true,
        .policy = {.roles = {{"jc3636k518c-esp32", "1.0.0", "0.1.0", 4096, 0, 1, 1, 2},
                             {"jc3636k518c-s3", "1.0.0", "0.1.0", 4096, 0, 1, 1, 2}},
                   .current_config_schema = 1}};
    fixture_t f = {0};
    pw_update_stage_t *s = create(&f, NULL);
    check(!pw_update_stage_begin(s, bundle_size, transaction),
          "missing local trust rejects upload");
    check(!pw_update_stage_feed(s, bundle, 1), "closed parser rejects data");
    check(!pw_update_stage_finish(s), "closed parser rejects finish");
    check(f.saves == 0 && f.erases == 0, "closed gate causes no persistence or flash mutation");
    pw_update_stage_destroy(s);
    const size_t chunks[] = {1, 13, 17, 173, 4096};
    for (unsigned i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
        memset(&f, 0, sizeof f);
        s = create(&f, &config);
        check(pw_update_stage_begin(s, bundle_size, transaction), "eligible upload accepted");
        check(!pw_update_stage_begin(s, bundle_size, transaction), "concurrent upload rejected");
        check(feed(s, bundle, bundle_size, chunks[i]), "split boundaries preserve signed bundle");
        check(pw_update_stage_finish(s), "both staged streams complete");
        pw_stage_view_t v;
        pw_update_stage_view(s, &v);
        check(v.phase == PW_STAGE_LOCAL_READY && v.received == bundle_size,
              "local ready is not pair complete");
        check(f.erases == 2 && f.finishes == 2, "both roles staged and finalized independently");
        check(pw_update_stage_peer_begin(s),
              "peer transfer starts only after full local verification");
        check(!pw_update_stage_peer_verified(s),
              "peer cannot be marked verified without its complete stream");
        pw_update_stage_peer_progress(s, v.image_bytes[0]);
        check(pw_update_stage_peer_verified(s), "qualified peer verification marks prepared only");
        pw_update_stage_view(s, &v);
        check(v.phase == PW_STAGE_PREPARED, "no activation state exists");
        pw_update_stage_destroy(s);
        s = create(&f, &config);
        pw_update_stage_view(s, &v);
        check(v.phase == PW_STAGE_RECOVERY, "restart never trusts or resumes stale staged bytes");
        check(pw_update_stage_metadata(s, &key_size, &key_size) == NULL,
              "restart has no fabricated manifest proof");
        pw_update_stage_destroy(s);
    }
    for (unsigned mode = 0; mode < 7; mode++) {
        memset(&f, 0, sizeof f);
        f.journal_fail = mode == 0;
        f.erase_fail = mode == 1;
        f.write_fail = mode == 2;
        f.finish_fail = mode == 3;
        f.corrupt_read = mode == 4;
        s = create(&f, &config);
        bool started = pw_update_stage_begin(s, bundle_size + (mode == 5 ? 1 : 0), transaction);
        bool accepted = started && feed(s, bundle, bundle_size - (mode == 6 ? 1 : 0), 173) &&
                        pw_update_stage_finish(s);
        check(!accepted, "journal/flash/ESP/hash/length failure never reaches ready");
        pw_stage_view_t v;
        pw_update_stage_view(s, &v);
        check(v.phase == PW_STAGE_FAILED || v.phase == PW_STAGE_LOCKED,
              "failure has explicit safe state");
        if (mode == 0 || mode == 5)
            check(f.erases == 0, "journal failure and mismatched body length reject before erase");
        pw_update_stage_destroy(s);
    }
    memset(&f, 0, sizeof f);
    s = create(&f, &config);
    check(pw_update_stage_begin(s, bundle_size, transaction) && feed(s, bundle, bundle_size, 173),
          "trailing-data setup");
    check(!pw_update_stage_feed(s, bundle, 1), "bytes after complete bundle rejected");
    check(!pw_update_stage_finish(s), "trailing bytes cannot leave a ready job");
    pw_update_stage_destroy(s);
    uint8_t *damaged = malloc(bundle_size);
    assert(damaged);
    for (unsigned mode = 0; mode < 4; mode++) {
        memset(&f, 0, sizeof f);
        s = create(&f, &config);
        memcpy(damaged, bundle, bundle_size);
        if (mode == 0)
            damaged[0] ^= 1;
        if (mode == 1)
            memset(damaged + 8, 0xff, 4);
        if (mode == 2)
            damaged[12] = 0;
        if (mode == 3)
            damaged[25] ^= 1;
        check(pw_update_stage_begin(s, bundle_size, transaction),
              "malformed data has only bounded initial reservation");
        check(!feed(s, damaged, bundle_size, 173), "bad framing or signature rejected");
        check(f.erases == 0, "unauthenticated bytes never erase a slot");
        pw_update_stage_destroy(s);
    }
    memset(&f, 0, sizeof f);
    s = create(&f, &config);
    check(pw_update_stage_begin(s, bundle_size, transaction), "journal corruption setup");
    pw_update_stage_destroy(s);
    f.journal[16] ^= 1;
    s = create(&f, &config);
    check(!pw_update_stage_begin(s, bundle_size, transaction),
          "corrupt journal locks rather than erases recovery evidence");
    pw_update_stage_destroy(s);
    check(!strcmp(pw_update_stage_phase_name((pw_stage_phase_t)-1), "locked"),
          "invalid phase cannot index before name table");
    free(damaged);
    free(bundle);
    free(key);
    printf("%u staging checks passed (ASan/UBSan; synthetic flash and descriptors, no hardware).\n",
           assertions);
    return 0;
}
