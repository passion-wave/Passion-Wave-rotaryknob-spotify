#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
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
    uint32_t revision, setup_seconds_left;
    char network_error[64];
} pw_app_view_t;
esp_err_t pw_app_init(void);
void pw_app_get_view(pw_app_view_t *view);
/* Called only by the physical UI gesture. Opens a protected ten-minute window. */
esp_err_t pw_app_open_setup(void);
void pw_app_close_setup(void);
/* Coalesced physical UI settings; no flash write per encoder step. */
void pw_app_adjust_brightness(int delta);
#ifdef __cplusplus
}
#endif
