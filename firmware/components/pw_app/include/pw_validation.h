// SPDX-License-Identifier: MIT
#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure bounded validation. No mutation, allocation, network or ESP-IDF calls. */
bool pw_keys_only(const cJSON *object, const char *const *names, size_t count);
/* Patch fields may be partial. Validate the complete merged config before commit. */
bool pw_validate_settings_patch(const cJSON *settings);
bool pw_validate_catalog(const cJSON *catalog);
/* Complete schema-1 config; revision is an integer in [1, UINT32_MAX].
 * The caller must reject revision exhaustion before incrementing it. */
bool pw_validate_config(const cJSON *config);
bool pw_validate_next_config(const cJSON *config, uint32_t previous_revision);
/* Parsing boundary (allocates via cJSON). Length INCLUDES the final NUL.
 * Requires exactly one object or array, no embedded NUL/escaped U+0000,
 * at most 16 levels of nesting. Caller owns the returned tree. */
cJSON *pw_parse_json(const char *json, size_t length);
/* Syntax/address screening only. DNS answers and redirects MUST be checked again
 * at a future playback/fetch boundary. IPv6 URLs are not qualified in this build. */
bool pw_public_radio_url(const char *url);
