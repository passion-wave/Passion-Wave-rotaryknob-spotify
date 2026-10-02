// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"

/* Encrypt system NVS, settings and journal using the laboratory key partition.
 * Never programs eFuses; physical flash extraction protection is a factory gate.
 * No application-level factory erase or replacement of existing keys.
 * IDF's regular NVS recovery may discard damaged pages; keep private backups.
 */
esp_err_t pw_storage_init(void);
