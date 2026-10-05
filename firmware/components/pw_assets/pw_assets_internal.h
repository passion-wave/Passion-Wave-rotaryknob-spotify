// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pw_assets.h"
typedef struct { const char *name; const uint8_t *jpeg; size_t bytes; } pw_asset_source_t;
extern const pw_asset_source_t pw_asset_sources[PW_ASSET_COUNT];
