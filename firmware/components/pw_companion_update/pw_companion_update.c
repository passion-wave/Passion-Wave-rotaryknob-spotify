// SPDX-License-Identifier: MIT
#include "pw_companion_update.h"
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#if defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK) && CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#error "Automatic eFuse advancement is outside this OTA receiver; qualify provisioning first."
#endif

static struct {
    nvs_handle_t journal;
    esp_ota_handle_t ota;
    const esp_partition_t *staging;
    uint32_t written;
    uint8_t recent[16];
    size_t recent_size;
    bool open;
} platform;
static pw_companion_update_core_t *core;
static uint32_t nonce;
__attribute__((weak)) const pw_companion_update_config_t *
pw_companion_update_provisioned_config(void) {
    return NULL;
}
static const esp_partition_t *partition_at(uint32_t address) {
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->address == address) {
            esp_partition_iterator_release(it);
            return p;
        }
        it = esp_partition_next(it);
    }
    return NULL;
}
static int load(void *unused, uint8_t bytes[PW_COMPANION_JOURNAL_BYTES]) {
    (void)unused;
    nvs_type_t type;
    esp_err_t err = nvs_find_key(platform.journal, "state", &type);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return 0;
    if (err != ESP_OK || type != NVS_TYPE_BLOB)
        return -1;
    size_t size = PW_COMPANION_JOURNAL_BYTES;
    err = nvs_get_blob(platform.journal, "state", bytes, &size);
    return err == ESP_OK && size == PW_COMPANION_JOURNAL_BYTES ? 1 : -1;
}
static bool save(void *unused, const uint8_t bytes[PW_COMPANION_JOURNAL_BYTES]) {
    (void)unused;
    return nvs_set_blob(platform.journal, "state", bytes, PW_COMPANION_JOURNAL_BYTES) == ESP_OK &&
           nvs_commit(platform.journal) == ESP_OK;
}
static bool load_proof(void *unused, uint8_t *bytes, size_t *size) {
    (void)unused;
    nvs_type_t type;
    if (nvs_find_key(platform.journal, "proof", &type) != ESP_OK || type != NVS_TYPE_BLOB)
        return false;
    return nvs_get_blob(platform.journal, "proof", bytes, size) == ESP_OK;
}
static bool save_proof(void *unused, const uint8_t *bytes, size_t size) {
    (void)unused;
    return size <= PW_COMPANION_PROOF_MAX &&
           nvs_set_blob(platform.journal, "proof", bytes, size) == ESP_OK &&
           nvs_commit(platform.journal) == ESP_OK;
}
static bool read_slot(void *unused, uint32_t address, uint32_t offset, uint8_t *out, size_t n) {
    (void)unused;
    const esp_partition_t *p = partition_at(address);
    if (!p || offset > p->size || n > p->size - offset ||
        esp_partition_read(p, offset, out, n) != ESP_OK)
        return false;
    // IDF may retain a partial encrypted 16-byte block until end(). Compare
    // accepted bytes for retries; final verification reads actual flash only.
    if (platform.open && platform.staging && address == platform.staging->address &&
        platform.recent_size) {
        uint32_t recent_start = platform.written - (uint32_t)platform.recent_size;
        uint32_t first = offset > recent_start ? offset : recent_start;
        uint32_t last = offset + (uint32_t)n;
        if (last > platform.written)
            last = platform.written;
        if (last > first)
            memcpy(out + first - offset, platform.recent + first - recent_start, last - first);
    }
    return true;
}
static bool validate(void *unused, uint32_t address) {
    (void)unused;
    const esp_partition_t *p = partition_at(address);
    if (!p)
        return false;
    esp_image_metadata_t metadata = {0};
    esp_partition_pos_t pos = {.offset = p->address, .size = p->size};
    return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &metadata) == ESP_OK;
}
static bool running_info(const esp_partition_t *p, pw_companion_slot_t *out) {
    esp_image_metadata_t metadata = {0};
    esp_partition_pos_t pos = {.offset = p->address, .size = p->size};
    esp_app_desc_t descriptor;
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &metadata) != ESP_OK ||
        esp_ota_get_partition_description(p, &descriptor) != ESP_OK ||
        !memchr(descriptor.version, 0, sizeof(descriptor.version)) ||
        !memchr(descriptor.project_name, 0, sizeof(descriptor.project_name)) ||
        descriptor.secure_version > 65535 ||
        strcmp(descriptor.project_name, "passionwave_companion"))
        return false;
    memset(out, 0, sizeof(*out));
    out->address = p->address;
    out->capacity = p->size;
    out->image_bytes = metadata.image_len;
    out->security_version = (uint16_t)descriptor.secure_version;
    memcpy(out->version, descriptor.version, sizeof(descriptor.version));
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    uint8_t bytes[1024];
    bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
    for (uint32_t at = 0; ok && at < out->image_bytes;) {
        size_t n = out->image_bytes - at > sizeof(bytes) ? sizeof(bytes) : out->image_bytes - at;
        ok = esp_partition_read(p, at, bytes, n) == ESP_OK &&
             mbedtls_sha256_update(&sha, bytes, n) == 0;
        at += (uint32_t)n;
    }
    if (ok)
        ok = mbedtls_sha256_finish(&sha, out->image_sha256) == 0;
    mbedtls_sha256_free(&sha);
    return ok;
}
static bool slots(void *unused, pw_companion_slot_t *running, pw_companion_slot_t *inactive) {
    (void)unused;
    const esp_partition_t *r = esp_ota_get_running_partition(),
                          *next = esp_ota_get_next_update_partition(NULL);
    if (!r || !next || r->address == next->address || !running_info(r, running))
        return false;
    memset(inactive, 0, sizeof(*inactive));
    inactive->address = next->address;
    inactive->capacity = next->size;
    return true;
}
static bool begin_stage(void *unused, uint32_t address, uint32_t bytes) {
    (void)unused;
    if (platform.open)
        return false;
    const esp_partition_t *target = partition_at(address),
                          *running = esp_ota_get_running_partition(),
                          *next = esp_ota_get_next_update_partition(NULL);
    if (!target || !running || !next || target->address == running->address ||
        target->address != next->address || bytes > target->size)
        return false;
    if (esp_ota_begin(target, bytes, &platform.ota) != ESP_OK)
        return false;
    platform.staging = target;
    platform.open = true;
    platform.written = 0;
    platform.recent_size = 0;
    return true;
}
static bool write_stage(void *unused, uint32_t offset, const uint8_t *bytes, size_t n) {
    (void)unused;
    if (!platform.open || offset != platform.written)
        return false;
    if (esp_ota_write(platform.ota, bytes, n) != ESP_OK)
        return false;
    if (n >= sizeof(platform.recent)) {
        memcpy(platform.recent, bytes + n - sizeof(platform.recent), sizeof(platform.recent));
        platform.recent_size = sizeof(platform.recent);
    } else {
        size_t keep = platform.recent_size;
        if (keep > sizeof(platform.recent) - n)
            keep = sizeof(platform.recent) - n;
        memmove(platform.recent, platform.recent + platform.recent_size - keep, keep);
        memcpy(platform.recent + keep, bytes, n);
        platform.recent_size = keep + n;
    }
    platform.written += (uint32_t)n;
    return true;
}
static bool finish_stage(void *unused) {
    (void)unused;
    if (!platform.open)
        return false;
    esp_err_t err = esp_ota_end(platform.ota);
    platform.open = false;
    platform.ota = 0;
    return err == ESP_OK;
}
static void abort_stage(void *unused) {
    (void)unused;
    if (platform.open)
        esp_ota_abort(platform.ota);
    platform.open = false;
    platform.ota = 0;
    platform.staging = NULL;
    platform.written = 0;
}
static bool set_boot(void *unused, uint32_t address) {
    (void)unused;
    const esp_partition_t *p = partition_at(address);
    return p && esp_ota_set_boot_partition(p) == ESP_OK;
}
static bool mark_valid(void *unused) {
    (void)unused;
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}
static bool pending(void *unused) {
    (void)unused;
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}
static uint64_t now_ms(void *unused) {
    (void)unused;
    return (uint64_t)esp_timer_get_time() / 1000;
}
esp_err_t pw_companion_update_init(uint32_t boot_nonce) {
    if (core || !boot_nonce)
        return ESP_ERR_INVALID_STATE;
    nonce = boot_nonce;
    esp_err_t err = nvs_flash_init_partition("journal");
    if (err != ESP_OK)
        return err;
    err = nvs_open_from_partition("journal", "pw_pair", NVS_READWRITE, &platform.journal);
    if (err != ESP_OK)
        return err;
    const pw_companion_update_ops_t ops = {.context = NULL,
                                           .load = load,
                                           .save = save,
                                           .load_proof = load_proof,
                                           .save_proof = save_proof,
                                           .slots = slots,
                                           .begin = begin_stage,
                                           .write = write_stage,
                                           .read = read_slot,
                                           .finish = finish_stage,
                                           .abort = abort_stage,
                                           .validate_slot = validate,
                                           .set_boot = set_boot,
                                           .mark_valid = mark_valid,
                                           .pending_verify = pending,
                                           .monotonic_ms = now_ms};
    core =
        pw_companion_update_core_create(pw_companion_update_provisioned_config(), &ops, boot_nonce);
    if (!core) {
        nvs_close(platform.journal);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
bool pw_companion_update_handle(const pw_frame_t *request, pw_frame_t *reply) {
    if (core)
        return pw_companion_update_core_handle(core, request, reply);
    if (request->kind < PW_MSG_OTA_BEGIN || request->kind > PW_MSG_OTA_STATUS)
        return false;
    memset(reply, 0, sizeof(*reply));
    reply->role = PW_ROLE_COMPANION;
    reply->session = nonce;
    reply->kind = PW_MSG_OTA_ACK;
    reply->length = PW_OTA_ACK_BYTES;
    reply->payload[0] = PW_UPDATE_WIRE_VERSION;
    if (request->length >= 17)
        memcpy(reply->payload + 1, request->payload + 1, 16);
    reply->payload[17] = request->kind;
    reply->payload[18] = PW_OTA_ERR_LOCKED;
    reply->payload[19] = PW_OTA_LOCKED;
    for (unsigned i = 0; i < 4; ++i) {
        reply->payload[20 + i] = (uint8_t)(request->sequence >> (8 * i));
        reply->payload[28 + i] = (uint8_t)(nonce >> (8 * i));
    }
    return true;
}
void pw_companion_update_report(pw_frame_t *report) {
    if (core) {
        pw_companion_update_core_report(core, report);
        return;
    }
    memset(report, 0, sizeof(*report));
    report->role = PW_ROLE_COMPANION;
    report->session = nonce;
    report->kind = PW_MSG_OTA_BOOT_REPORT;
    report->length = PW_OTA_BOOT_REPORT_BYTES;
    report->payload[0] = PW_UPDATE_WIRE_VERSION;
    report->payload[17] = PW_OTA_LOCKED;
    for (unsigned i = 0; i < 4; ++i)
        report->payload[18 + i] = (uint8_t)(nonce >> (8 * i));
    memcpy(report->payload + 94, esp_app_get_description()->version, 32);
}
void pw_companion_update_tick(void) {
    if (core)
        pw_companion_update_core_tick(core);
}
bool pw_companion_update_reboot_requested(void) {
    return core && pw_companion_update_core_reboot_requested(core);
}
