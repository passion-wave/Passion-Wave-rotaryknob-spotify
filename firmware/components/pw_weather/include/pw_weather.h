// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "pw_weather_model.h"
#ifdef __cplusplus
extern "C" {
#endif

// Call once after netif/event-loop initialization. Starts one low-priority worker.
esp_err_t pw_weather_init(void);
// Non-blocking. Coordinates truncated to four decimals. Caller owns persistence/TZ.
// Any changed location/settings generation invalidates cached data and in-flight jobs.
esp_err_t pw_weather_configure(bool enabled, double latitude, double longitude,
                               uint32_t generation);
// Respects provider Expires and retry backoff even for a user refresh request.
void pw_weather_request_refresh(void);
// Suspend before OTA or memory-intensive foreground work. In-flight body reads cancel.
void pw_weather_set_suspended(bool suspended);
// Atomic copy, no network. Snapshot is ~7 KiB: caller must avoid a small task stack.
void pw_weather_get_snapshot(pw_weather_snapshot_t *output);
#ifdef __cplusplus
}
#endif
