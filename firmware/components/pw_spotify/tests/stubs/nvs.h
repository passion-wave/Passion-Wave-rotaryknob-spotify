#pragma once
#include "esp_err.h"
#include <stddef.h>
typedef unsigned nvs_handle_t;
typedef int nvs_type_t;
#define NVS_READWRITE 1
#define NVS_TYPE_BLOB 2
esp_err_t nvs_open_from_partition(const char *, const char *, int, nvs_handle_t *);
esp_err_t nvs_find_key(nvs_handle_t, const char *, nvs_type_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_commit(nvs_handle_t);
