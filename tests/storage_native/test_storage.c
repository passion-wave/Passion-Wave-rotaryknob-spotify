// Host simulation of flash/NVS fault boundaries, not a hardware entropy test.
#include "../../firmware/components/pw_storage/pw_storage.c"
#include "test_platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static esp_partition_t partition = {.size = 4096, .id = 0};
static esp_partition_t targets[3] = {{4096, 1}, {4096, 2}, {4096, 3}};
static uint8_t flash[4096];
static uint8_t target_flash[3][4096];
static nvs_sec_scheme_t provider;
static int scenario, writes, secure_count, random_count;
static bool entropy;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *name) {
    assert(type == 1);
    if (subtype == 4) {
        assert(!strcmp(name, "nvs_keys"));
        return scenario == 1 ? NULL : &partition;
    }
    assert(subtype == 2);
    const char *names[] = {"nvs", "settings", "journal"};
    for (unsigned i = 0; i < 3; i++)
        if (!strcmp(name, names[i]))
            return &targets[i];
    assert(false);
    return NULL;
}
esp_err_t esp_partition_read_raw(const esp_partition_t *part, size_t offset, void *out,
                                 size_t size) {
    assert(offset + size <= 4096);
    if (scenario == 12)
        return ESP_FAIL;
    memcpy(out, (part->id ? target_flash[part->id - 1] : flash) + offset, size);
    return ESP_OK;
}
uint32_t esp_rom_crc32_le(uint32_t initial, const uint8_t *bytes, uint32_t size) {
    uint32_t crc = initial;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
esp_err_t nvs_flash_read_security_cfg(const esp_partition_t *part, nvs_sec_cfg_t *keys) {
    assert(part == &partition);
    bool blank = true;
    for (unsigned i = 0; i < 68; i++)
        if (flash[i] != 0xff)
            blank = false;
    if (blank)
        return ESP_ERR_NVS_KEYS_NOT_INITIALIZED;
    uint32_t crc;
    memcpy(&crc, flash + 64, 4);
    if (crc != esp_rom_crc32_le(0xffffffff, flash, 64))
        return ESP_ERR_NVS_CORRUPT_KEY_PART;
    memcpy(keys, flash, 64);
    return ESP_OK;
}
esp_err_t nvs_flash_register_security_scheme(nvs_sec_scheme_t *scheme) {
    assert(!entropy && writes == 0);
    provider = *scheme;
    return scenario == 2 ? ESP_FAIL : ESP_OK;
}
void bootloader_random_enable(void) {
    assert(!entropy && !secure_count);
    entropy = true;
}
void bootloader_random_disable(void) {
    assert(entropy);
    entropy = false;
}
void esp_fill_random(void *out, size_t size) {
    assert(entropy && size == 64);
    random_count++;
    for (size_t i = 0; i < size; i++)
        ((uint8_t *)out)[i] = (uint8_t)(i * 17 + 23);
}
esp_err_t esp_partition_write(const esp_partition_t *part, size_t offset, const void *bytes,
                              size_t size) {
    assert(part == &partition && offset == 0 && size == 96 && !entropy &&
           provider.nvs_flash_key_gen);
    writes++;
    memcpy(flash, bytes, scenario == 4 ? 8 : size);
    return scenario == 4 ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_flash_secure_init_partition(const char *name, nvs_sec_cfg_t *keys) {
    const char *names[] = {"nvs", "settings", "journal"};
    assert(secure_count < 3 && !strcmp(name, names[secure_count]));
    assert(!entropy && !memcmp(keys, flash, 64));
    secure_count++;
    return scenario == 5 && secure_count == 2 ? ESP_FAIL : ESP_OK;
}
void mbedtls_platform_zeroize(void *p, size_t size) {
    volatile uint8_t *bytes = p;
    while (size--)
        *bytes++ = 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = atoi(argv[1]);
    memset(flash, 0xff, sizeof flash);
    memset(target_flash, 0xff, sizeof target_flash);
    if (scenario == 3)
        flash[2] = 0; /* incomplete/corrupt key must never be erased */
    if (scenario == 6 || scenario == 11 || scenario == 13) {
        for (unsigned i = 0; i < 64; i++)
            flash[i] = (uint8_t)(31 + i);
        uint32_t crc = esp_rom_crc32_le(0xffffffff, flash, 64);
        memcpy(flash + 64, &crc, 4);
        if (scenario != 11)
            memcpy(flash + 80, key_format, sizeof key_format);
        if (scenario == 13)
            flash[82] ^= 1;
    }
    if (scenario == 7)
        flash[4095] = 0;
    if (scenario >= 8 && scenario <= 10)
        target_flash[scenario - 8][511] = 0;
    esp_err_t result = pw_storage_init();
    if (scenario == 0 || scenario == 6) {
        assert(result == ESP_OK && secure_count == 3);
        assert(writes == (scenario == 0) && random_count == (scenario == 0));
        assert(memcmp(flash, flash + 32, 32) != 0);
        assert(pw_storage_init() == ESP_OK && secure_count == 3); /* idempotent after peripherals */
        memset(flash, 0xff, sizeof flash);
        nvs_sec_cfg_t keys;
        assert(provider.nvs_flash_key_gen(provider.scheme_data, &keys) == ESP_ERR_INVALID_STATE);
        assert(writes == (scenario == 0)); /* a late callback must not start SAR/RF entropy */
    } else {
        assert(result != ESP_OK && !initialized && !entropy);
        if (scenario <= 3 || scenario >= 7)
            assert(writes == 0 && random_count == 0 && secure_count == 0);
        if (scenario == 4) {
            assert(writes == 1 && secure_count == 0);
            nvs_sec_cfg_t keys;
            assert(provider.nvs_flash_key_gen(provider.scheme_data, &keys) ==
                   ESP_ERR_NVS_CORRUPT_KEY_PART);
            assert(writes == 1);
        }
        if (scenario == 5)
            assert(secure_count == 2);
    }
    printf("Storage fault boundary %d: PASS\n", scenario);
}
