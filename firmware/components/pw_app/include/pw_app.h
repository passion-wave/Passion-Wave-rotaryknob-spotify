#pragma once
#include "esp_err.h"
#include "pw_spotify.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PW_APP_HARDWARE "JC3636K518C_I_YR1"
typedef struct {
    char name[64], ip[16], ssid[33], setup_ssid[33], setup_password[24];
    bool connected, setup_open, connecting, peer_connected, weather_enabled, haptic;
    bool avatar_enabled, avatar_blond;
    char screensaver_mode[16];
    uint8_t brightness;
    uint32_t revision, setup_seconds_left, lan_seconds_left;
    bool lan_open;
    char lan_code[7];
    char network_error[64];
} pw_app_view_t;
esp_err_t pw_app_init(void);
void pw_app_get_view(pw_app_view_t *view);
/* Called only by the physical UI gesture. Opens a protected ten-minute window. */
esp_err_t pw_app_open_setup(void);
void pw_app_close_setup(void);
/* Physical UI only; explicitly opted-in pilot, never enabled by network request. */
esp_err_t pw_app_open_lan_lab(void);
/* Coalesced physical UI settings; no flash write per encoder step. */
void pw_app_adjust_brightness(int delta);
typedef struct {
    char id[65], name[81];
    bool playable; /* Qualified lab playlist start only; no implicit podcast start. */
} pw_app_favorite_t;
/* Bounded physical-UI view of the saved, enabled catalog; returns total count. */
size_t pw_app_get_favorites(size_t offset, pw_app_favorite_t *items, size_t capacity,
                            uint32_t *revision);
/* Resolves a saved ID again under the config lock; never accepts arbitrary URLs. */
esp_err_t pw_app_play_favorite(const char *id, uint32_t expected_revision,
                              const pw_spotify_command_t *target, uint32_t *request_id);
#ifdef __cplusplus
}
#endif
