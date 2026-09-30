// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// Start the single LVGL owner task after pw_board_init and pw_app_init.
// Startup waits up to 5 seconds for LVGL/widget initialization; no runtime wait.
// The UI consumes real snapshots; it does not manufacture playback/weather data.
esp_err_t pw_ui_init(void);
#ifdef __cplusplus
}
#endif
