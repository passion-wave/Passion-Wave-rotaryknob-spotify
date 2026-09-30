#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_NVS_ENCRYPTION 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NVS_KEYS_NOT_INITIALIZED -4
#define ESP_ERR_NVS_CORRUPT_KEY_PART -5
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS 4
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
#define NVS_KEY_SIZE 32
typedef int esp_err_t;
typedef struct { size_t size; unsigned id; } esp_partition_t;
typedef struct { uint8_t eky[32], tky[32]; } nvs_sec_cfg_t;
typedef struct {
    int scheme_id;
    void *scheme_data;
    esp_err_t (*nvs_flash_key_gen)(const void *, nvs_sec_cfg_t *);
    esp_err_t (*nvs_flash_read_cfg)(const void *, nvs_sec_cfg_t *);
} nvs_sec_scheme_t;
const esp_partition_t *esp_partition_find_first(int, int, const char *);
esp_err_t esp_partition_write(const esp_partition_t *, size_t, const void *, size_t);
esp_err_t esp_partition_read_raw(const esp_partition_t *, size_t, void *, size_t);
esp_err_t nvs_flash_read_security_cfg(const esp_partition_t *, nvs_sec_cfg_t *);
esp_err_t nvs_flash_register_security_scheme(nvs_sec_scheme_t *);
esp_err_t nvs_flash_secure_init_partition(const char *, nvs_sec_cfg_t *);
void bootloader_random_enable(void);
void bootloader_random_disable(void);
void esp_fill_random(void *, size_t);
uint32_t esp_rom_crc32_le(uint32_t, const uint8_t *, uint32_t);
void mbedtls_platform_zeroize(void *, size_t);
