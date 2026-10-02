// SPDX-License-Identifier: MIT
#include "pw_storage.h"
#include "bootloader_random.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "mbedtls/platform_util.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <string.h>

#if !CONFIG_NVS_ENCRYPTION
#error "PassionWave requires NVS encryption"
#endif

static const esp_partition_t *key_partition;
static bool initial_boot;
static bool initialized;
static const uint8_t key_format[16] = {'P', 'W', '-', 'N', 'V', 'S', '-', 'L',
                                       'A', 'B', 1,   0,   0,   0,   0,   0};

static esp_err_t read_keys(const void *context, nvs_sec_cfg_t *keys) {
    const esp_partition_t *part = context;
    esp_err_t err = nvs_flash_read_security_cfg(part, keys);
    if (err != ESP_OK)
        return err;
    uint8_t marker[sizeof key_format];
    err = esp_partition_read_raw(part, 80, marker, sizeof marker);
    if (err != ESP_OK)
        return err;
    /* A format guard, not authentication. Never adopt an unknown legacy key. */
    return memcmp(marker, key_format, sizeof marker) ? ESP_ERR_NVS_CORRUPT_KEY_PART : ESP_OK;
}

static esp_err_t require_erased(const esp_partition_t *part) {
    if (!part)
        return ESP_ERR_NOT_FOUND;
    uint8_t block[256];
    for (size_t offset = 0; offset < part->size; offset += sizeof block) {
        size_t count = part->size - offset;
        if (count > sizeof block)
            count = sizeof block;
        esp_err_t err = esp_partition_read_raw(part, offset, block, count);
        if (err != ESP_OK)
            return err;
        for (size_t i = 0; i < count; i++)
            if (block[i] != 0xff)
                return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t require_fresh_layout(const esp_partition_t *keys) {
    esp_err_t err = require_erased(keys);
    const char *names[] = {"nvs", "settings", "journal"};
    for (unsigned i = 0; err == ESP_OK && i < 3; i++)
        err = require_erased(esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                      ESP_PARTITION_SUBTYPE_DATA_NVS, names[i]));
    return err;
}

static esp_err_t generate_blank_keys(const void *context, nvs_sec_cfg_t *keys) {
    /* Never rotate a valid key or destroy a corrupt key's recovery evidence. */
    esp_err_t err = read_keys(context, keys);
    if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
        if (!initial_boot)
            return ESP_ERR_INVALID_STATE;
        /* secure_init may repair/discard pages. Never offer it legacy contents
         * under a newly generated key. First installation must explicitly
         * prepare these partitions after a verified original-flash backup. */
        err = require_fresh_layout(context);
        if (err != ESP_OK)
            return err;
        /* IDF 5.4.3 nvs_flash_generate_keys() relies on the flash encryption
         * engine to randomize fixed raw patterns. This laboratory device does
         * not enable flash encryption, so supply independent hardware-random
         * AES-XTS keys explicitly. The startup caller has not enabled Wi-Fi,
         * ADC or I2S; the temporary SAR entropy source is safe at this point. */
        bootloader_random_enable();
        esp_fill_random(keys, sizeof(*keys));
        bootloader_random_disable();
        uint8_t stored[96];
        memset(stored, 0xff, sizeof stored);
        memcpy(stored, keys->eky, NVS_KEY_SIZE);
        memcpy(stored + NVS_KEY_SIZE, keys->tky, NVS_KEY_SIZE);
        uint32_t crc = esp_rom_crc32_le(0xffffffff, stored, 2 * NVS_KEY_SIZE);
        memcpy(stored + 2 * NVS_KEY_SIZE, &crc, sizeof crc);
        memcpy(stored + 80, key_format, sizeof key_format);
        /* The key region was checked as erased. Do not erase the partition or
         * replace a partial/corrupt key after an interrupted write. */
        err = esp_partition_write((const esp_partition_t *)context, 0, stored, sizeof stored);
        mbedtls_platform_zeroize(stored, sizeof stored);
        if (err == ESP_OK)
            err = read_keys(context, keys);
    }
    return err;
}

esp_err_t pw_storage_init(void) {
    if (initialized)
        return ESP_OK;
    key_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                             ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "nvs_keys");
    if (!key_partition)
        return ESP_ERR_NOT_FOUND;

    /* ESP-IDF registers its configured provider at startup. Replace that provider
     * BEFORE any NVS initialization, so even a later nvs_flash_init() can only use
     * these flash-partition callbacks. Neither callback touches an eFuse.
     * AES-XTS protects NVS contents. This lab's key partition remains extractable;
     * factory flash encryption is deliberately not enabled on a recovery device. */
    nvs_sec_scheme_t scheme = {
        .scheme_id = 0x50574c42,
        .scheme_data = (void *)key_partition,
        .nvs_flash_key_gen = generate_blank_keys,
        .nvs_flash_read_cfg = read_keys,
    };
    esp_err_t err = nvs_flash_register_security_scheme(&scheme);
    if (err != ESP_OK)
        return err;
    nvs_sec_cfg_t keys = {0};
    initial_boot = true;
    err = generate_blank_keys(key_partition, &keys);
    initial_boot = false;
    if (err == ESP_OK)
        err = nvs_flash_secure_init_partition("nvs", &keys);
    if (err == ESP_OK)
        err = nvs_flash_secure_init_partition("settings", &keys);
    if (err == ESP_OK)
        err = nvs_flash_secure_init_partition("journal", &keys);
    mbedtls_platform_zeroize(&keys, sizeof keys);
    initialized = err == ESP_OK;
    return err;
}
