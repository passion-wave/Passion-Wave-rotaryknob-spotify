// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PW_ASSET_WIDTH 368
#define PW_ASSET_HEIGHT 368
#define PW_ASSET_COUNT 67
#define PW_ASSET_AVATAR_COUNT 52
#define PW_ASSET_NONE (-1)
typedef struct {
    int asset_id;
    uint32_t generation;
    uint8_t slot;
    const uint16_t *rgb565;
} pw_asset_frame_t;
typedef struct {
    bool ready, decoding;
    uint32_t completed, cancelled, failed;
    uint32_t maximum_decode_ms;
    int last_failed_id;
} pw_assets_stats_t;
// Allocate exactly two 368x368 RGB565 PSRAM buffers and one bounded decoder.
esp_err_t pw_assets_init(void);
// Nonblocking latest-wins request. IDs 0..51 match the original avatar resolver.
esp_err_t pw_assets_request(int asset_id, uint32_t generation);
void pw_assets_cancel(void);
// UI task only: acquired buffer remains immutable until release. Hide/change
// the LVGL source and drop its cache before releasing its old slot.
bool pw_assets_acquire(pw_asset_frame_t *frame);
void pw_assets_release(uint8_t slot);
void pw_assets_get_stats(pw_assets_stats_t *stats);
// Returns no image for unknown/empty MET symbols. Never a partly-cloudy fallback.
int pw_assets_weather_id(const char *symbol);
const char *pw_assets_name(int asset_id);
#ifdef __cplusplus
}
#endif
