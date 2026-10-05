// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_WEATHER_HOURS_MAX 48
#define PW_WEATHER_DAYS_MAX 5
#define PW_WEATHER_BODY_MAX (128u * 1024u)
#define PW_WEATHER_ATTRIBUTION "Wetterdaten: MET Norway (CC BY 4.0); lokal zusammengefasst"
#define PW_WEATHER_SOURCE_URL "https://api.met.no/"

typedef enum {
    PW_WEATHER_UNCONFIGURED, PW_WEATHER_WAITING_NETWORK,
    PW_WEATHER_WAITING_CLOCK, PW_WEATHER_FETCHING, PW_WEATHER_READY,
    PW_WEATHER_STALE, PW_WEATHER_ERROR, PW_WEATHER_SUSPENDED
} pw_weather_status_t;

enum {
    PW_WEATHER_TEMPERATURE = 1u << 0, PW_WEATHER_HUMIDITY = 1u << 1,
    PW_WEATHER_WIND = 1u << 2, PW_WEATHER_WIND_DIRECTION = 1u << 3,
    PW_WEATHER_GUST = 1u << 4, PW_WEATHER_CLOUD = 1u << 5,
    PW_WEATHER_PRECIPITATION = 1u << 6, PW_WEATHER_RAIN_PROBABILITY = 1u << 7,
    PW_WEATHER_SYMBOL = 1u << 8, PW_WEATHER_UV = 1u << 9,
    PW_WEATHER_APPARENT_TEMPERATURE = 1u << 10
};

typedef struct {
    int64_t time_utc;                 // Instant forecast, not a sensor observation.
    int64_t interval_end_utc;         // next_1_hours only in hours[]; never a fake 1h bin.
    uint32_t valid;
    float temperature_c, humidity_percent, wind_mps, wind_from_degrees;
    float gust_mps, cloud_percent, precipitation_mm, rain_probability_percent, uv_index;
    float apparent_temperature_c;
    char symbol[48];                  // MET symbol_code, including _day/_night suffix.
} pw_weather_point_t;

typedef struct {
    int year, month, day;             // Device's configured local timezone.
    int64_t start_utc, end_utc;        // Actual 23/24/25h local day boundaries.
    bool temperature_valid;
    float temperature_min_c, temperature_max_c; // Extrema of available forecast samples.
    bool precipitation_valid, precipitation_complete;
    float precipitation_mm;           // Sum of non-overlapping intervals wholly in day.
    uint32_t precipitation_covered_seconds;
    uint16_t temperature_sample_count;
    char symbol[48];                  // Nearest supplied forecast to local noon.
} pw_weather_day_t;

typedef struct {
    pw_weather_status_t status;
    uint32_t location_generation;
    int64_t source_updated_utc;        // Provider's updated_at, NOT a local retrieval time.
    int64_t checked_utc;               // Last successful 200/203 or matching 304 validation.
    int64_t next_request_utc;          // Includes Expires, backoff and request jitter.
    bool model_forecast, deprecated_source;
    bool current_valid;
    pw_weather_point_t current;
    uint8_t hour_count, day_count;
    pw_weather_point_t hours[PW_WEATHER_HOURS_MAX];
    pw_weather_day_t days[PW_WEATHER_DAYS_MAX];
    char source[24];
    char error[96];                    // Stable technical code, no coordinates or secrets.
} pw_weather_snapshot_t;

typedef struct {
    bool valid, partial, windy, wet, snow, sun_hat, hail, lightning;
    uint8_t asset_index, thermal_band, protection, scene;
    int64_t start_utc, end_utc;
    char scene_label[48], outfit_label[64];
} pw_weather_avatar_t;

void pw_weather_avatar_resolve(const pw_weather_snapshot_t *snapshot, int64_t now_utc,
                               bool blond, pw_weather_avatar_t *output);

// Pure bounded parser; uses current process timezone only for local day aggregation.
// Rejects invalid timestamps, out-of-order/duplicate entries, future/old source versions.
// output is cleared on failure. Each valid bit is independent; missing != zero.
bool pw_weather_parse_met(const char *json, size_t length, int64_t now_utc,
                          uint32_t generation, pw_weather_snapshot_t *output,
                          char *error, size_t error_size);
bool pw_weather_parse_utc(const char *iso, int64_t *out);
int64_t pw_weather_parse_http_date(const char *imf_fixdate);
void pw_weather_recompute_freshness(pw_weather_snapshot_t *snapshot, int64_t now_utc);
bool pw_weather_avatar_window(const pw_weather_snapshot_t *snapshot, int64_t now_utc,
                              pw_weather_point_t output[4]);
const char *pw_weather_status_name(pw_weather_status_t status);
#ifdef __cplusplus
}
#endif
