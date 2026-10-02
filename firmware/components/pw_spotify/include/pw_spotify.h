// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PW_SPOTIFY_MAX_DEVICES 16u
#define PW_SPOTIFY_DEVICE_ID_BYTES 129u
#define PW_SPOTIFY_CLIENT_ID "b785d8a5f5c840e9bc33e168c53098ca"
#define PW_SPOTIFY_REDIRECT_URI "http://127.0.0.1:8766/callback"
#define PW_SPOTIFY_AUTH_URL_BYTES 1024u
#define PW_SPOTIFY_AUTH_CODE_BYTES 1025u
#define PW_SPOTIFY_AUTH_STATE_BYTES 49u
#define PW_SPOTIFY_AUTH_TTL_SECONDS 600u
typedef enum {
    PW_SPOTIFY_DISABLED,
    PW_SPOTIFY_UNLINKED,
    PW_SPOTIFY_WAITING_NETWORK,
    PW_SPOTIFY_WAITING_CLOCK,
    PW_SPOTIFY_AUTHORIZING,
    PW_SPOTIFY_READY,
    PW_SPOTIFY_REAUTH_REQUIRED,
    PW_SPOTIFY_RATE_LIMITED,
    PW_SPOTIFY_ERROR,
    PW_SPOTIFY_SUSPENDED,
    PW_SPOTIFY_DISCONNECTING
} pw_spotify_state_t;
typedef enum {
    PW_SPOTIFY_ERROR_NONE,
    PW_SPOTIFY_ERROR_STORAGE,
    PW_SPOTIFY_ERROR_NETWORK,
    PW_SPOTIFY_ERROR_AUTH,
    PW_SPOTIFY_ERROR_FORBIDDEN,
    PW_SPOTIFY_ERROR_NO_DEVICE,
    PW_SPOTIFY_ERROR_RATE_LIMIT,
    PW_SPOTIFY_ERROR_RESPONSE,
    PW_SPOTIFY_ERROR_MEMORY,
    PW_SPOTIFY_ERROR_STALE,
    PW_SPOTIFY_ERROR_UNSUPPORTED
} pw_spotify_error_t;
typedef enum {
    PW_SPOTIFY_PLAY,
    PW_SPOTIFY_PAUSE,
    PW_SPOTIFY_NEXT,
    PW_SPOTIFY_PREVIOUS,
    PW_SPOTIFY_VOLUME
} pw_spotify_command_kind_t;
typedef enum {
    PW_SPOTIFY_COMMAND_NONE,
    PW_SPOTIFY_COMMAND_QUEUED,
    PW_SPOTIFY_COMMAND_ACCEPTED,
    PW_SPOTIFY_COMMAND_REJECTED,
    PW_SPOTIFY_COMMAND_UNCERTAIN,
    PW_SPOTIFY_COMMAND_STALE
} pw_spotify_command_state_t;
typedef struct {
    char id[PW_SPOTIFY_DEVICE_ID_BYTES], name[129], type[33];
    bool active, restricted, supports_volume, volume_known;
    uint8_t volume;
} pw_spotify_device_t;
typedef struct {
    bool enabled, linked, product_approved, connected, playback_known, playing;
    bool volume_known, supports_volume, position_known, devices_truncated;
    bool can_play_pause, can_play, can_pause, can_next, can_previous, can_volume;
    bool selected_present, selected_restricted, selected_supports_volume, selected_volume_known;
    uint8_t selected_volume;
    pw_spotify_state_t state;
    pw_spotify_error_t error;
    uint16_t http_status;
    uint32_t session, selection_generation, revision, retry_after_seconds;
    uint32_t position_ms, duration_ms;
    int64_t observed_at_ms;
    uint8_t volume, device_count;
    char title[193], artist[161], item_type[17], item_uri[97], context_uri[97];
    char selected_device_id[PW_SPOTIFY_DEVICE_ID_BYTES], selected_device_name[129];
    char active_device_id[PW_SPOTIFY_DEVICE_ID_BYTES], active_device_name[129];
    pw_spotify_device_t devices[PW_SPOTIFY_MAX_DEVICES];
    uint32_t last_request_id;
    pw_spotify_command_state_t last_command_state;
    /* Consumed OAuth state, published to the protected USB owner only after
     * successful token persistence. RAM-only; a reboot cannot confirm an old attempt. */
    char authorization_id[PW_SPOTIFY_AUTH_STATE_BYTES];
} pw_spotify_snapshot_t;
typedef struct {
    pw_spotify_command_kind_t kind;
    uint32_t session, selection_generation;
    char device_id[PW_SPOTIFY_DEVICE_ID_BYTES];
    /* PLAY: empty means resume; otherwise validated playlist/track URI only.
     * Caller resolves content against its saved allowlist before submission. */
    char uri[97];
    uint8_t volume;
} pw_spotify_command_t;
typedef struct {
    char url[PW_SPOTIFY_AUTH_URL_BYTES];
    uint32_t expires_in_seconds;
} pw_spotify_auth_start_t;

esp_err_t pw_spotify_init(void); /* after encrypted settings NVS initialization */
void pw_spotify_set_network(bool connected);
void pw_spotify_set_suspended(bool suspended);
/* Snapshot is several KiB: use caller-owned static/heap storage, not a small stack. */
void pw_spotify_get_snapshot(pw_spotify_snapshot_t *);
esp_err_t pw_spotify_refresh(void); /* enqueue discovery + playback refresh */
esp_err_t pw_spotify_select_device(const char *device_id, uint32_t session);
esp_err_t pw_spotify_submit(const pw_spotify_command_t *, uint32_t *request_id);
/* USB-only owner calls. URL/state are public; verifier/tokens never leave S3.
 * begin_auth performs bounded local PKCE work, no network operation.
 * complete_auth/disconnect enqueue work; ESP_OK means accepted, not linked. */
esp_err_t pw_spotify_begin_auth(pw_spotify_auth_start_t *);
esp_err_t pw_spotify_complete_auth(const char *code, const char *state);
void pw_spotify_cancel_auth(void);
esp_err_t pw_spotify_disconnect(void);
const char *pw_spotify_state_name(pw_spotify_state_t);
const char *pw_spotify_error_name(pw_spotify_error_t);
#ifdef __cplusplus
}
#endif
