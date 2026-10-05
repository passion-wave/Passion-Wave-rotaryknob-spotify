// SPDX-License-Identifier: MIT
#include "pw_update_verify.h"
#include "cJSON.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERIFIED_MAGIC 0x50575556u
struct pw_update_manifest {
    uint32_t magic;
    pw_update_manifest_info_t info;
};
struct pw_update_image_check {
    mbedtls_sha256_context sha;
    uint32_t expected_bytes, received;
    uint8_t expected_hash[32], header[288];
    size_t header_size;
    uint16_t security_version;
    pw_update_role_t role;
    char version[49];
    bool failed;
};
static const cJSON *get(const cJSON *j, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(j, key);
}
static const char *string(const cJSON *j) {
    return cJSON_IsString(j) ? j->valuestring : NULL;
}
static bool exact_keys(const cJSON *j, const char *const *keys, size_t count) {
    if (!cJSON_IsObject(j) || (size_t)cJSON_GetArraySize(j) != count)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!get(j, keys[i]))
            return false;
    return true;
}
static bool short_id(const char *s, size_t max) {
    if (!s || !*s || strlen(s) > max)
        return false;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '_' || *s == '-'))
            return false;
    return true;
}
static bool uint_field(const cJSON *j, const char *key, uint32_t min, uint32_t max, uint32_t *out) {
    const cJSON *value = get(j, key);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < min ||
        value->valuedouble > max || floor(value->valuedouble) != value->valuedouble)
        return false;
    *out = (uint32_t)value->valuedouble;
    return true;
}
static bool copy_string(const cJSON *j, const char *key, char *out, size_t size) {
    const char *s = string(get(j, key));
    if (!s || !*s || strlen(s) >= size)
        return false;
    for (const char *p = s; *p; ++p)
        if ((unsigned char)*p < 32 || (unsigned char)*p > 126)
            return false;
    memcpy(out, s, strlen(s) + 1);
    return true;
}
static bool url(const char *s) {
    if (!s || strncmp(s, "https://", 8) || !s[8] || strlen(s) > 2048 || s[8] == '/' || s[8] == '.')
        return false;
    for (const char *p = s + 8; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p > 126 || *p == '\\' || *p == '@' ||
            *p == '#')
            return false;
    const char *p = s + 8, *label = p;
    while (*p && *p != '/' && *p != '?' && *p != ':') {
        if (*p == '.') {
            if (p == label || p[-1] == '-')
                return false;
            label = p + 1;
        } else if (!isalnum((unsigned char)*p) && *p != '-')
            return false;
        else if (*p == '-' && p == label)
            return false;
        ++p;
    }
    if (p == label || p[-1] == '-' || p - (s + 8) > 253)
        return false;
    if (*p == ':') {
        ++p;
        unsigned port = 0;
        const char *start = p;
        while (isdigit((unsigned char)*p)) {
            port = port * 10 + (unsigned)(*p++ - '0');
            if (port > 65535)
                return false;
        }
        if (p == start || !port || (*p && *p != '/' && *p != '?'))
            return false;
    }
    return true;
}
typedef struct {
    uint32_t major, minor, patch;
    const char *pre;
} version_t;
static bool version_part(const char **p, uint32_t *value) {
    if (!isdigit((unsigned char)**p))
        return false;
    const char *start = *p;
    uint64_t n = 0;
    while (isdigit((unsigned char)**p)) {
        n = n * 10 + (unsigned)(*(*p)++ - '0');
        if (n > UINT32_MAX)
            return false;
    }
    if (*p - start > 1 && *start == '0')
        return false;
    *value = (uint32_t)n;
    return true;
}
static bool version_parse(const char *s, version_t *out) {
    if (!s || !*s || strlen(s) > 48)
        return false;
    const char *p = s;
    if (!version_part(&p, &out->major) || *p++ != '.' || !version_part(&p, &out->minor) ||
        *p++ != '.' || !version_part(&p, &out->patch))
        return false;
    out->pre = NULL;
    if (!*p)
        return true;
    if (*p++ != '-' || !*p)
        return false;
    out->pre = p;
    while (*p) {
        const char *start = p;
        bool digits = true;
        while (*p && *p != '.') {
            if (!isalnum((unsigned char)*p) && *p != '-')
                return false;
            if (!isdigit((unsigned char)*p))
                digits = false;
            ++p;
        }
        if (start == p || (digits && p - start > 1 && *start == '0'))
            return false;
        if (*p == '.' && !*++p)
            return false;
    }
    return true;
}
static int compare_identifier(const char *a, size_t na, const char *b, size_t nb) {
    bool da = true, db = true;
    for (size_t i = 0; i < na; ++i)
        if (!isdigit((unsigned char)a[i]))
            da = false;
    for (size_t i = 0; i < nb; ++i)
        if (!isdigit((unsigned char)b[i]))
            db = false;
    if (da != db)
        return da ? -1 : 1;
    if (da && na != nb)
        return na < nb ? -1 : 1;
    int c = memcmp(a, b, na < nb ? na : nb);
    if (c)
        return c < 0 ? -1 : 1;
    return na == nb ? 0 : na < nb ? -1 : 1;
}
static int version_compare(version_t a, version_t b) {
#define PART(k)                                                                                    \
    if (a.k != b.k)                                                                                \
    return a.k < b.k ? -1 : 1
    PART(major);
    PART(minor);
    PART(patch);
#undef PART
    if (!a.pre || !b.pre)
        return !a.pre && !b.pre ? 0 : !a.pre ? 1 : -1;
    const char *pa = a.pre, *pb = b.pre;
    while (*pa && *pb) {
        size_t na = strcspn(pa, "."), nb = strcspn(pb, ".");
        int cmp = compare_identifier(pa, na, pb, nb);
        if (cmp)
            return cmp;
        pa += na;
        pb += nb;
        if (*pa)
            ++pa;
        if (*pb)
            ++pb;
    }
    return !*pa && !*pb ? 0 : !*pa ? -1 : 1;
}
static bool sorted_tree(const cJSON *j, unsigned depth) {
    if (depth > 8)
        return false;
    if (cJSON_IsObject(j)) {
        const char *previous = NULL;
        for (const cJSON *p = j->child; p; p = p->next) {
            if (!p->string || (previous && strcmp(previous, p->string) >= 0))
                return false;
            previous = p->string;
            if (!sorted_tree(p, depth + 1))
                return false;
        }
    } else if (cJSON_IsArray(j)) {
        for (const cJSON *p = j->child; p; p = p->next)
            if (!sorted_tree(p, depth + 1))
                return false;
    }
    return true;
}
static bool safe_depth(const uint8_t *p, size_t n) {
    unsigned depth = 0;
    bool quoted = false, escape = false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = p[i];
        if (c < 32 || c > 126)
            return false;
        if (quoted) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 8)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !depth && !quoted && !escape;
}
static bool hex_hash(const char *s, uint8_t out[32]) {
    if (!s || strlen(s) != 64)
        return false;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned n = 0;
        for (unsigned j = 0; j < 2; ++j) {
            unsigned char c = s[i * 2 + j];
            if (c >= '0' && c <= '9')
                n = n * 16 + c - '0';
            else if (c >= 'a' && c <= 'f')
                n = n * 16 + c - 'a' + 10;
            else
                return false;
        }
        out[i] = (uint8_t)n;
    }
    return true;
}
static bool in_range(uint16_t value, uint16_t min, uint16_t max) {
    return value >= min && value <= max;
}
static bool bounded_range(uint16_t min, uint16_t max) {
    return min >= 1 && min <= max && max - min < 8;
}
static pw_update_result_t parse_image(const cJSON *j, pw_update_image_info_t *out) {
    static const char *const keys[] = {"role",
                                       "version",
                                       "release_id",
                                       "hardware_id",
                                       "bytes",
                                       "sha256",
                                       "url",
                                       "emitted_protocol",
                                       "accepted_peer_protocol_min",
                                       "accepted_peer_protocol_max",
                                       "readable_config_schema_min",
                                       "readable_config_schema_max",
                                       "security_version",
                                       "minimum_bootloader"};
    if (!exact_keys(j, keys, sizeof(keys) / sizeof(keys[0])))
        return PW_UPDATE_FORMAT;
    const char *role = string(get(j, "role"));
    if (role && !strcmp(role, "companion_esp32"))
        out->role = PW_UPDATE_COMPANION;
    else if (role && !strcmp(role, "controller_s3"))
        out->role = PW_UPDATE_S3;
    else
        return PW_UPDATE_ROLE;
    if (!copy_string(j, "version", out->version, sizeof(out->version)) ||
        !copy_string(j, "release_id", out->release_id, sizeof(out->release_id)) ||
        !copy_string(j, "hardware_id", out->hardware_id, sizeof(out->hardware_id)) ||
        !copy_string(j, "url", out->url, sizeof(out->url)) ||
        !copy_string(j, "minimum_bootloader", out->minimum_bootloader,
                     sizeof(out->minimum_bootloader)) ||
        !short_id(out->release_id, 64) || !short_id(out->hardware_id, 64) || !url(out->url) ||
        !hex_hash(string(get(j, "sha256")), out->sha256))
        return PW_UPDATE_FORMAT;
    version_t parsed;
    if (!version_parse(out->version, &parsed) || !version_parse(out->minimum_bootloader, &parsed))
        return PW_UPDATE_FORMAT;
    uint32_t value;
    if (!uint_field(j, "bytes", 288, 16777216, &out->bytes))
        return PW_UPDATE_SIZE;
#define UINT16(key, dest, lo)                                                                      \
    do {                                                                                           \
        if (!uint_field(j, key, lo, 65535, &value))                                                \
            return PW_UPDATE_FORMAT;                                                               \
        out->dest = (uint16_t)value;                                                               \
    } while (0)
    UINT16("emitted_protocol", emitted_protocol, 1);
    UINT16("accepted_peer_protocol_min", accepted_peer_min, 1);
    UINT16("accepted_peer_protocol_max", accepted_peer_max, 1);
    UINT16("readable_config_schema_min", readable_config_min, 1);
    UINT16("readable_config_schema_max", readable_config_max, 1);
    UINT16("security_version", security_version, 0);
#undef UINT16
    if (!bounded_range(out->accepted_peer_min, out->accepted_peer_max))
        return PW_UPDATE_PROTOCOL;
    if (!bounded_range(out->readable_config_min, out->readable_config_max))
        return PW_UPDATE_CONFIG_SCHEMA;
    return PW_UPDATE_OK;
}
static pw_update_result_t policy_check(const pw_update_manifest_info_t *m,
                                       const pw_update_policy_t *policy) {
    if (!policy->current_config_schema)
        return PW_UPDATE_INVALID_ARGUMENT;
    if (!policy->allow_beta && !strcmp(m->channel, "beta"))
        return PW_UPDATE_CHANNEL;
    for (unsigned i = 0; i < 2; ++i) {
        const pw_update_image_info_t *image = &m->images[i];
        const pw_update_role_policy_t *current = &policy->roles[i];
        if (!short_id(current->hardware_id, 64) || !current->app_slot_bytes ||
            !current->emitted_protocol ||
            !bounded_range(current->accepted_peer_min, current->accepted_peer_max))
            return PW_UPDATE_INVALID_ARGUMENT;
        if (strcmp(image->hardware_id, current->hardware_id))
            return PW_UPDATE_HARDWARE;
        if (strcmp(image->version, m->version) || strcmp(image->release_id, m->release_id))
            return PW_UPDATE_RELEASE;
        if (image->bytes > current->app_slot_bytes)
            return PW_UPDATE_SIZE;
        if (image->security_version < current->security_floor)
            return PW_UPDATE_SECURITY_VERSION;
        if (!in_range(policy->current_config_schema, image->readable_config_min,
                      image->readable_config_max) ||
            !in_range(m->config_schema, image->readable_config_min, image->readable_config_max))
            return PW_UPDATE_CONFIG_SCHEMA;
        version_t target, installed, boot, required;
        if (!version_parse(current->running_version, &installed) ||
            !version_parse(current->bootloader_version, &boot) ||
            !version_parse(image->minimum_bootloader, &required) ||
            !version_parse(image->version, &target))
            return PW_UPDATE_INVALID_ARGUMENT;
        if (version_compare(boot, required) < 0)
            return PW_UPDATE_BOOTLOADER;
        if (!policy->allow_version_downgrade && version_compare(target, installed) < 0)
            return PW_UPDATE_DOWNGRADE;
    }
    const pw_update_image_info_t *c = &m->images[PW_UPDATE_COMPANION],
                                 *s = &m->images[PW_UPDATE_S3];
    const pw_update_role_policy_t *old_s3 = &policy->roles[PW_UPDATE_S3],
                                  *old_comp = &policy->roles[PW_UPDATE_COMPANION];
    // Final pair AND mandatory intermediate pair (old S3 + updated companion).
    if (!in_range(c->emitted_protocol, s->accepted_peer_min, s->accepted_peer_max) ||
        !in_range(s->emitted_protocol, c->accepted_peer_min, c->accepted_peer_max) ||
        !in_range(c->emitted_protocol, old_s3->accepted_peer_min, old_s3->accepted_peer_max) ||
        !in_range(old_s3->emitted_protocol, c->accepted_peer_min, c->accepted_peer_max) ||
        !in_range(old_comp->emitted_protocol, s->accepted_peer_min, s->accepted_peer_max) ||
        !in_range(s->emitted_protocol, old_comp->accepted_peer_min, old_comp->accepted_peer_max))
        return PW_UPDATE_PROTOCOL;
    return PW_UPDATE_OK;
}
pw_update_result_t pw_update_verify_manifest(const uint8_t *json, size_t n, const uint8_t *sig,
                                             size_t sig_size, const uint8_t *key_data,
                                             size_t key_size, const char *key_id,
                                             const pw_update_policy_t *policy,
                                             pw_update_manifest_t **out) {
    if (out)
        *out = NULL;
    if (!out || !json || !n || n > PW_UPDATE_MANIFEST_MAX || !sig || sig_size < 8 ||
        sig_size > 72 || !key_data || !key_size || key_size > 4096 || !short_id(key_id, 64) ||
        !policy)
        return PW_UPDATE_INVALID_ARGUMENT;
    uint8_t digest[32];
    if (mbedtls_sha256(json, n, digest, 0))
        return PW_UPDATE_SIGNATURE;
    uint8_t *key_copy = malloc(key_size + 1);
    if (!key_copy)
        return PW_UPDATE_MEMORY;
    memcpy(key_copy, key_data, key_size);
    key_copy[key_size] = 0;
    bool pem = key_size >= 11 && !memcmp(key_data, "-----BEGIN ", 11);
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int result = mbedtls_pk_parse_public_key(
        &key, key_copy, pem ? (key_data[key_size - 1] ? key_size + 1 : key_size) : key_size);
    free(key_copy);
    if (result || !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) || !mbedtls_pk_ec(key) ||
        mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(key)) != MBEDTLS_ECP_DP_SECP256R1) {
        mbedtls_pk_free(&key);
        return PW_UPDATE_UNTRUSTED_KEY;
    }
    result = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig, sig_size);
    mbedtls_pk_free(&key);
    if (result)
        return PW_UPDATE_SIGNATURE;
    // Only authenticated bytes reach JSON parsing. Bound nesting before cJSON.
    if (!safe_depth(json, n))
        return PW_UPDATE_FORMAT;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts((const char *)json, n, &end, false);
    if (!root || end != (const char *)json + n) {
        cJSON_Delete(root);
        return PW_UPDATE_FORMAT;
    }
    char *canonical = sorted_tree(root, 0) ? cJSON_PrintUnformatted(root) : NULL;
    if (!canonical || strlen(canonical) != n || memcmp(canonical, json, n)) {
        cJSON_free(canonical);
        cJSON_Delete(root);
        return PW_UPDATE_NONCANONICAL;
    }
    cJSON_free(canonical);
    pw_update_manifest_t *verified = calloc(1, sizeof(*verified));
    if (!verified) {
        cJSON_Delete(root);
        return PW_UPDATE_MEMORY;
    }
    pw_update_manifest_info_t *m = &verified->info;
    static const char *const top_keys[] = {"manifest_version", "product_id", "example_only",
                                           "release_id",       "version",    "channel",
                                           "config_schema",    "images",     "signing"};
    static const char *const signing_keys[] = {"key_id", "manifest_signature_url", "encoding"};
    pw_update_result_t failure = PW_UPDATE_FORMAT;
    uint32_t value = 0;
    version_t parsed;
    if (!exact_keys(root, top_keys, sizeof(top_keys) / sizeof(top_keys[0])) ||
        !uint_field(root, "manifest_version", 1, 1, &value))
        goto done;
    if (!string(get(root, "product_id")) ||
        strcmp(string(get(root, "product_id")), PW_UPDATE_PRODUCT_ID)) {
        failure = PW_UPDATE_PRODUCT;
        goto done;
    }
    if (!cJSON_IsBool(get(root, "example_only")))
        goto done;
    if (cJSON_IsTrue(get(root, "example_only"))) {
        failure = PW_UPDATE_EXAMPLE;
        goto done;
    }
    if (!copy_string(root, "release_id", m->release_id, sizeof(m->release_id)) ||
        !short_id(m->release_id, 64) ||
        !copy_string(root, "version", m->version, sizeof(m->version)) ||
        !version_parse(m->version, &parsed) ||
        !copy_string(root, "channel", m->channel, sizeof(m->channel)) ||
        (strcmp(m->channel, "stable") && strcmp(m->channel, "beta")) ||
        !uint_field(root, "config_schema", 1, 65535, &value))
        goto done;
    m->config_schema = (uint16_t)value;
    const cJSON *signing = get(root, "signing");
    if (!exact_keys(signing, signing_keys, sizeof(signing_keys) / sizeof(signing_keys[0])) ||
        !copy_string(signing, "key_id", m->key_id, sizeof(m->key_id)) || !short_id(m->key_id, 64) ||
        !url(string(get(signing, "manifest_signature_url"))) || !string(get(signing, "encoding")) ||
        strcmp(string(get(signing, "encoding")), "detached-over-canonical-manifest-v1"))
        goto done;
    if (strcmp(m->key_id, key_id)) {
        failure = PW_UPDATE_UNTRUSTED_KEY;
        goto done;
    }
    const cJSON *images = get(root, "images");
    if (!cJSON_IsArray(images) || cJSON_GetArraySize(images) != 2) {
        failure = PW_UPDATE_ROLE;
        goto done;
    }
    unsigned roles = 0;
    for (unsigned i = 0; i < 2; ++i) {
        const cJSON *item = cJSON_GetArrayItem(images, (int)i);
        const char *role_name = string(get(item, "role"));
        unsigned role;
        if (role_name && !strcmp(role_name, "companion_esp32"))
            role = PW_UPDATE_COMPANION;
        else if (role_name && !strcmp(role_name, "controller_s3"))
            role = PW_UPDATE_S3;
        else {
            failure = PW_UPDATE_ROLE;
            goto done;
        }
        if (roles & (1u << role)) {
            failure = PW_UPDATE_ROLE;
            goto done;
        }
        roles |= 1u << role;
        failure = parse_image(item, &m->images[role]);
        if (failure != PW_UPDATE_OK)
            goto done;
    }
    failure = policy_check(m, policy);
    if (failure != PW_UPDATE_OK)
        goto done;
    memcpy(m->manifest_sha256, digest, sizeof(digest));
    verified->magic = VERIFIED_MAGIC;
    *out = verified;
done:
    cJSON_Delete(root);
    if (failure != PW_UPDATE_OK)
        free(verified);
    return failure;
}
const pw_update_manifest_info_t *pw_update_manifest_info(const pw_update_manifest_t *m) {
    return m && m->magic == VERIFIED_MAGIC ? &m->info : NULL;
}
void pw_update_manifest_free(pw_update_manifest_t *m) {
    if (m) {
        m->magic = 0;
        free(m);
    }
}
pw_update_result_t pw_update_image_begin(const pw_update_manifest_t *m, pw_update_role_t role,
                                         pw_update_image_check_t **out) {
    if (out)
        *out = NULL;
    if (!out || !m || m->magic != VERIFIED_MAGIC ||
        (role != PW_UPDATE_COMPANION && role != PW_UPDATE_S3))
        return PW_UPDATE_INVALID_ARGUMENT;
    const pw_update_image_info_t *image = &m->info.images[role];
    pw_update_image_check_t *check = calloc(1, sizeof(*check));
    if (!check)
        return PW_UPDATE_MEMORY;
    mbedtls_sha256_init(&check->sha);
    if (mbedtls_sha256_starts(&check->sha, 0)) {
        free(check);
        return PW_UPDATE_STATE;
    }
    check->expected_bytes = image->bytes;
    memcpy(check->expected_hash, image->sha256, 32);
    check->role = role;
    check->security_version = image->security_version;
    strcpy(check->version, image->version);
    *out = check;
    return PW_UPDATE_OK;
}
pw_update_result_t pw_update_image_feed(pw_update_image_check_t *check, const void *data,
                                        size_t n) {
    if (!check || (!data && n))
        return PW_UPDATE_INVALID_ARGUMENT;
    if (check->failed)
        return PW_UPDATE_STATE;
    if (n > check->expected_bytes - check->received) {
        check->failed = true;
        return PW_UPDATE_SIZE;
    }
    size_t to_copy = sizeof(check->header) - check->header_size;
    if (to_copy > n)
        to_copy = n;
    if (to_copy) {
        memcpy(check->header + check->header_size, data, to_copy);
        check->header_size += to_copy;
    }
    if (mbedtls_sha256_update(&check->sha, data, n)) {
        check->failed = true;
        return PW_UPDATE_STATE;
    }
    check->received += (uint32_t)n;
    return PW_UPDATE_OK;
}
static uint32_t little32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool fixed_string_equal(const uint8_t *p, size_t size, const char *expected) {
    const uint8_t *end = memchr(p, 0, size);
    return end && (size_t)(end - p) == strlen(expected) && !memcmp(p, expected, strlen(expected));
}
pw_update_result_t pw_update_image_finish(pw_update_image_check_t **handle) {
    if (!handle || !*handle)
        return PW_UPDATE_INVALID_ARGUMENT;
    pw_update_image_check_t *c = *handle;
    pw_update_result_t result = PW_UPDATE_OK;
    uint8_t hash[32];
    if (c->failed)
        result = PW_UPDATE_STATE;
    else if (c->received != c->expected_bytes || c->header_size != sizeof(c->header))
        result = PW_UPDATE_SIZE;
    else if (mbedtls_sha256_finish(&c->sha, hash))
        result = PW_UPDATE_STATE;
    else {
        unsigned diff = 0;
        for (unsigned i = 0; i < 32; ++i)
            diff |= hash[i] ^ c->expected_hash[i];
        if (diff)
            result = PW_UPDATE_IMAGE_HASH;
        else if (c->header[0] != 0xe9 || c->header[1] == 0 || c->header[1] > 16 ||
                 (unsigned)(c->header[12] | ((unsigned)c->header[13] << 8)) !=
                     (c->role == PW_UPDATE_S3 ? 9u : 0u) ||
                 little32(c->header + 32) != 0xabcd5432u || little32(c->header + 28) < 256)
            result = PW_UPDATE_IMAGE_HEADER;
        else if (!fixed_string_equal(c->header + 48, 32, c->version) ||
                 !fixed_string_equal(c->header + 80, 32,
                                     c->role == PW_UPDATE_S3 ? "passionwave_spotify"
                                                             : "passionwave_companion"))
            result = PW_UPDATE_IMAGE_VERSION;
        else if (little32(c->header + 36) != c->security_version)
            result = PW_UPDATE_SECURITY_VERSION;
    }
    pw_update_image_abort(handle);
    return result;
}
void pw_update_image_abort(pw_update_image_check_t **handle) {
    if (!handle || !*handle)
        return;
    mbedtls_sha256_free(&(*handle)->sha);
    memset(*handle, 0, sizeof(**handle));
    free(*handle);
    *handle = NULL;
}
const char *pw_update_result_name(pw_update_result_t code) {
    static const char *names[] = {"ok",
                                  "invalid_argument",
                                  "memory",
                                  "signature",
                                  "untrusted_key",
                                  "format",
                                  "noncanonical",
                                  "example",
                                  "product",
                                  "role",
                                  "release",
                                  "channel",
                                  "hardware",
                                  "size",
                                  "protocol",
                                  "config_schema",
                                  "security_version",
                                  "bootloader",
                                  "downgrade",
                                  "image_hash",
                                  "image_header",
                                  "image_version",
                                  "state"};
    return (unsigned)code < sizeof(names) / sizeof(names[0]) ? names[code] : "unknown";
}
