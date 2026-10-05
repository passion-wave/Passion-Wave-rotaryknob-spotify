// SPDX-License-Identifier: MIT
#pragma once
#include "cJSON.h"
#include "pw_spotify.h"
#define PW_SPOTIFY_TOKEN_BYTES 2049u
#define PW_SPOTIFY_RECORD_BYTES 2304u
#define PW_SPOTIFY_REAUTH_SECONDS (180LL * 86400)
typedef struct {
    char access[PW_SPOTIFY_TOKEN_BYTES], refresh[PW_SPOTIFY_TOKEN_BYTES];
    int64_t authorized_at, expires_at;
} pw_spotify_tokens_t;
typedef struct {
    char state[PW_SPOTIFY_AUTH_STATE_BYTES], verifier[65];
    int64_t expires_ms;
    uint32_t generation;
    bool pending, exchanging;
} pw_spotify_auth_t;
typedef struct {
    bool pause, resume, next, previous;
} pw_spotify_disallows_t;
/* Fixed, payload-free playback validation reasons for numeric diagnostics.
 * JSON_PARSE is assigned by the caller when pw_spotify_json returns NULL. */
typedef enum {
    PW_SPOTIFY_PLAYBACK_PARSE_OK = 0,
    PW_SPOTIFY_PLAYBACK_PARSE_ROOT = 1,
    PW_SPOTIFY_PLAYBACK_PARSE_IS_PLAYING = 2,
    PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_OBJECT = 3,
    PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_ID = 4,
    PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_NAME = 5,
    PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_TYPE = 6,
    PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_FLAGS = 7,
    PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_VOLUME = 8,
    PW_SPOTIFY_PLAYBACK_PARSE_PROGRESS = 9,
    PW_SPOTIFY_PLAYBACK_PARSE_ITEM_TYPE = 10,
    PW_SPOTIFY_PLAYBACK_PARSE_TITLE = 11,
    PW_SPOTIFY_PLAYBACK_PARSE_ITEM_URI = 12,
    PW_SPOTIFY_PLAYBACK_PARSE_DURATION = 13,
    PW_SPOTIFY_PLAYBACK_PARSE_ARTIST = 14,
    PW_SPOTIFY_PLAYBACK_PARSE_ITEM = 15,
    PW_SPOTIFY_PLAYBACK_PARSE_CONTEXT = 16,
    PW_SPOTIFY_PLAYBACK_PARSE_JSON_PARSE = 31
} pw_spotify_playback_parse_error_t;
void pw_spotify_wipe(void *, size_t);
bool pw_spotify_ascii(const char *, size_t, bool);
bool pw_spotify_uri(const char *);
bool pw_spotify_encode(const char *, char *, size_t);
bool pw_spotify_text(char *, size_t, const char *);
cJSON *pw_spotify_json(const char *, size_t);
void pw_spotify_secret_json_delete(cJSON *);
bool pw_spotify_parse_devices(const cJSON *, pw_spotify_snapshot_t *);
bool pw_spotify_parse_playback(const cJSON *, pw_spotify_snapshot_t *, pw_spotify_disallows_t *);
bool pw_spotify_parse_playback_ex(const cJSON *, pw_spotify_snapshot_t *,
                                pw_spotify_disallows_t *, pw_spotify_playback_parse_error_t *);
void pw_spotify_clear_playback(pw_spotify_snapshot_t *);
void pw_spotify_capabilities(pw_spotify_snapshot_t *, const pw_spotify_disallows_t *, bool);
bool pw_spotify_command_valid(const pw_spotify_command_t *, const pw_spotify_snapshot_t *);
bool pw_spotify_parse_token(const cJSON *, const pw_spotify_tokens_t *, int64_t,
                            pw_spotify_tokens_t *);
size_t pw_spotify_record_encode(const pw_spotify_tokens_t *, uint8_t *, size_t);
bool pw_spotify_record_decode(const uint8_t *, size_t, pw_spotify_tokens_t *);
bool pw_spotify_auth_begin(pw_spotify_auth_t *, const uint8_t[56], int64_t,
                           pw_spotify_auth_start_t *);
bool pw_spotify_auth_accept(pw_spotify_auth_t *, const char *, const char *, int64_t);
void pw_spotify_auth_cancel(pw_spotify_auth_t *);
uint32_t pw_spotify_retry_after(const char *);

bool pw_spotify_parse_library(const cJSON *, bool shows, uint32_t offset,
                              pw_spotify_library_page_t *out);
