// SPDX-License-Identifier: MIT
#include "pw_update_verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        exit(4);
    if (fseek(f, 0, SEEK_END))
        exit(4);
    long n = ftell(f);
    if (n < 0 || n > 16777217)
        exit(4);
    rewind(f);
    unsigned char *bytes = malloc((size_t)n + 1);
    if (!bytes)
        exit(4);
    if (fread(bytes, 1, (size_t)n, f) != (size_t)n)
        exit(4);
    fclose(f);
    bytes[n] = 0;
    *size = (size_t)n;
    return bytes;
}
int main(int argc, char **argv) {
    if (argc != 7 && argc != 10)
        return 4;
    size_t nj, ns, nk;
    unsigned char *j = file(argv[1], &nj), *s = file(argv[2], &ns), *k = file(argv[3], &nk);
    pw_update_policy_t policy = {
        .roles =
            {
                {.hardware_id = "jc3636k518c-esp32",
                 .bootloader_version = "1.0.0",
                 .running_version = "0.1.0",
                 .app_slot_bytes = 1048576,
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
                 .accepted_peer_max = 2},
            },
        .current_config_schema = 1,
        .allow_beta = true,
        .allow_version_downgrade = false,
    };
    if (!strcmp(argv[6], "stable_only"))
        policy.allow_beta = false;
    if (!strcmp(argv[6], "small_slot"))
        policy.roles[0].app_slot_bytes = 512;
    if (!strcmp(argv[6], "security2"))
        policy.roles[0].security_floor = 2;
    if (!strcmp(argv[6], "old_bootloader"))
        policy.roles[0].bootloader_version = "0.0.1";
    if (!strcmp(argv[6], "new_running"))
        policy.roles[0].running_version = "0.3.0";
    if (!strcmp(argv[6], "pre_running"))
        policy.roles[0].running_version = "0.2.0-beta.10";
    if (!strcmp(argv[6], "narrow_old_s3"))
        policy.roles[1].accepted_peer_max = 1;
    if (!strcmp(argv[6], "narrow_old_companion"))
        policy.roles[0].accepted_peer_max = 1;
    if (!strcmp(argv[6], "missing_policy"))
        policy.roles[0].hardware_id = NULL;
    pw_update_manifest_t *verified = NULL;
    pw_update_result_t result =
        pw_update_verify_manifest(j, nj, s, ns, k, nk, "test-p256", &policy, &verified);
    int expected = atoi(argv[4]);
    if ((int)result != expected || (result != PW_UPDATE_OK && verified)) {
        fprintf(stderr, "%s: manifest got %s (%d), expected %d\n", argv[5],
                pw_update_result_name(result), result, expected);
        return 1;
    }
    if (result == PW_UPDATE_OK) {
        const pw_update_manifest_info_t *info = pw_update_manifest_info(verified);
        if (!info || info->images[0].role != PW_UPDATE_COMPANION ||
            info->images[1].role != PW_UPDATE_S3)
            return 1;
    }
    if (argc == 10 && verified) {
        int image_expected = atoi(argv[9]);
        for (unsigned role = 0; role < 2; ++role) {
            size_t n;
            unsigned char *bytes = file(argv[7 + role], &n);
            pw_update_image_check_t *check = NULL;
            result = pw_update_image_begin(verified, (pw_update_role_t)role, &check);
            for (size_t at = 0; result == PW_UPDATE_OK && at < n;) {
                size_t count = n - at > 137 ? 137 : n - at;
                result = pw_update_image_feed(check, bytes + at, count);
                at += count;
            }
            if (result == PW_UPDATE_OK)
                result = pw_update_image_finish(&check);
            else
                pw_update_image_abort(&check);
            free(bytes);
            if ((int)result != image_expected || check) {
                fprintf(stderr, "%s: image role %u got %s (%d), expected %d\n", argv[5], role,
                        pw_update_result_name(result), result, image_expected);
                return 1;
            }
        }
    }
    pw_update_manifest_free(verified);
    free(j);
    free(s);
    free(k);
    return 0;
}
