// SPDX-License-Identifier: MIT
#include "pw_spotify.h"
#include "sdkconfig.h"
#include <string.h>
const char *pw_spotify_state_name(pw_spotify_state_t s) {
    static const char *const names[] = {"disabled",        "unlinked",     "waiting_network",
                                        "waiting_clock",   "authorizing",  "ready",
                                        "reauth_required", "rate_limited", "error",
                                        "suspended",       "disconnecting"};
    return (unsigned)s < sizeof(names) / sizeof(*names) ? names[s] : "error";
}
const char *pw_spotify_error_name(pw_spotify_error_t e) {
    static const char *const names[] = {"none",      "storage",   "network",    "auth",
                                        "forbidden", "no_device", "rate_limit", "response",
                                        "memory",    "stale",     "unsupported"};
    return (unsigned)e < sizeof(names) / sizeof(*names) ? names[e] : "response";
}
#ifndef CONFIG_PW_SPOTIFY_LAB
esp_err_t pw_spotify_init(void) {
    return ESP_OK;
}
void pw_spotify_set_network(bool value) {
    (void)value;
}
void pw_spotify_set_suspended(bool value) {
    (void)value;
}
void pw_spotify_get_snapshot(pw_spotify_snapshot_t *out) {
    if (out) {
        memset(out, 0, sizeof(*out));
        out->state = PW_SPOTIFY_DISABLED;
    }
}
esp_err_t pw_spotify_refresh(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t pw_spotify_select_device(const char *id, uint32_t session) {
    (void)id;
    (void)session;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t pw_spotify_submit(const pw_spotify_command_t *cmd, uint32_t *id) {
    (void)cmd;
    if (id)
        *id = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t pw_spotify_begin_auth(pw_spotify_auth_start_t *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t pw_spotify_complete_auth(const char *code, const char *state) {
    (void)code;
    (void)state;
    return ESP_ERR_NOT_SUPPORTED;
}
void pw_spotify_cancel_auth(void) {}
esp_err_t pw_spotify_disconnect(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
#else
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pw_spotify_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

#define RESPONSE_BYTES 65536u
#define QUEUE_COUNT 8u
#define CLOCK_MIN 1704067200LL
#define DEVICE_FRESH_US 90000000LL
#define PLAYBACK_FRESH_US 30000000LL
#define HTTP_DEADLINE_US 18000000LL

typedef struct {
    pw_spotify_command_t command;
    uint32_t id;
} pending_t;
typedef struct {
    SemaphoreHandle_t mutex;
    TaskHandle_t worker;
    nvs_handle_t store;
    bool initialized, network, suspended, storage_bad, disconnect_pending, refresh_requested;
    bool auth_queued, auth_busy, reauth;
    pw_spotify_auth_t auth;
    char auth_code[PW_SPOTIFY_AUTH_CODE_BYTES];
    char auth_confirmation[PW_SPOTIFY_AUTH_STATE_BYTES];
    pw_spotify_snapshot_t snapshot;
    pw_spotify_disallows_t disallows;
    pending_t commands[QUEUE_COUNT], volume;
    unsigned head, count;
    bool volume_pending;
    uint32_t next_request, epoch;
    int64_t cooldown_us, devices_at_us, playback_at_us;
} service_t;
static service_t service;
/* Only worker (or init before worker starts) accesses credential memory. */
static pw_spotify_tokens_t tokens;
static int64_t access_deadline_us;
static void lock(void) {
    xSemaphoreTake(service.mutex, portMAX_DELAY);
}
static void unlock(void) {
    xSemaphoreGive(service.mutex);
}
static int64_t now_us(void) {
    return esp_timer_get_time();
}
static void wake(void) {
    if (service.worker)
        xTaskNotifyGive(service.worker);
}
static void revision_locked(void) {
    if (++service.snapshot.revision == 0)
        service.snapshot.revision = 1;
}
static uint32_t new_session(void) {
    uint32_t n;
    do {
        n = esp_random();
    } while (!n);
    return n;
}
static void clear_commands_locked(void) {
    pw_spotify_wipe(service.commands, sizeof(service.commands));
    pw_spotify_wipe(&service.volume, sizeof(service.volume));
    service.count = service.head = 0;
    service.volume_pending = false;
}
static void state_locked(void) {
    pw_spotify_snapshot_t *s = &service.snapshot;
    int64_t now = now_us();
    s->connected = service.network;
    s->retry_after_seconds = 0;
    if (service.disconnect_pending)
        s->state = PW_SPOTIFY_DISCONNECTING;
    else if (service.storage_bad)
        s->state = PW_SPOTIFY_ERROR;
    else if (service.suspended)
        s->state = PW_SPOTIFY_SUSPENDED;
    else if (service.auth.pending || service.auth.exchanging || service.auth_busy)
        s->state = PW_SPOTIFY_AUTHORIZING;
    else if (service.reauth)
        s->state = PW_SPOTIFY_REAUTH_REQUIRED;
    else if (!s->linked)
        s->state = PW_SPOTIFY_UNLINKED;
    else if (!service.network)
        s->state = PW_SPOTIFY_WAITING_NETWORK;
    else if (time(NULL) < CLOCK_MIN)
        s->state = PW_SPOTIFY_WAITING_CLOCK;
    else if (service.cooldown_us > now) {
        s->state = PW_SPOTIFY_RATE_LIMITED;
        s->retry_after_seconds = (uint32_t)((service.cooldown_us - now + 999999) / 1000000);
    } else
        s->state = PW_SPOTIFY_READY;
    bool fresh = service.devices_at_us && now - service.devices_at_us < DEVICE_FRESH_US;
    pw_spotify_capabilities(s, &service.disallows, fresh);
    if (!service.playback_at_us || now - service.playback_at_us >= PLAYBACK_FRESH_US) {
        s->can_pause = s->can_next = s->can_previous = false;
        s->can_play_pause = s->can_play;
    }
}
void pw_spotify_get_snapshot(pw_spotify_snapshot_t *out) {
    if (!out)
        return;
    if (!service.mutex) {
        memset(out, 0, sizeof(*out));
        out->enabled = true;
        out->state = PW_SPOTIFY_ERROR;
        out->error = PW_SPOTIFY_ERROR_MEMORY;
        return;
    }
    lock();
    state_locked();
    *out = service.snapshot;
    unlock();
}
void pw_spotify_set_network(bool connected) {
    if (!service.mutex)
        return;
    lock();
    bool changed = service.network != connected;
    service.network = connected;
    if (changed) {
        service.refresh_requested = true;
        state_locked();
        revision_locked();
    }
    unlock();
    wake();
}
void pw_spotify_set_suspended(bool suspended) {
    if (!service.mutex)
        return;
    lock();
    if (service.suspended != suspended) {
        service.suspended = suspended;
        clear_commands_locked();
        state_locked();
        revision_locked();
    }
    unlock();
    wake();
}
esp_err_t pw_spotify_refresh(void) {
    if (!service.initialized)
        return ESP_ERR_INVALID_STATE;
    lock();
    service.refresh_requested = true;
    unlock();
    wake();
    return ESP_OK;
}
esp_err_t pw_spotify_select_device(const char *id, uint32_t session) {
    if (!service.initialized || !pw_spotify_ascii(id, PW_SPOTIFY_DEVICE_ID_BYTES, false))
        return ESP_ERR_INVALID_ARG;
    lock();
    state_locked();
    pw_spotify_snapshot_t *s = &service.snapshot;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s->linked && session == s->session && service.devices_at_us &&
        now_us() - service.devices_at_us < DEVICE_FRESH_US) {
        for (unsigned i = 0; i < s->device_count; i++)
            if (!strcmp(id, s->devices[i].id) && !s->devices[i].restricted) {
                if (strcmp(id, s->selected_device_id)) {
                    strcpy(s->selected_device_id, id);
                    strcpy(s->selected_device_name, s->devices[i].name);
                    if (++s->selection_generation == 0)
                        s->selection_generation = 1;
                    clear_commands_locked();
                    s->last_request_id = 0;
                    s->last_command_state = PW_SPOTIFY_COMMAND_NONE;
                }
                state_locked();
                revision_locked();
                result = ESP_OK;
                break;
            }
    }
    unlock();
    return result; /* Deliberately no network/transfer request. */
}
esp_err_t pw_spotify_submit(const pw_spotify_command_t *cmd, uint32_t *request_id) {
    if (request_id)
        *request_id = 0;
    if (!service.initialized || !cmd)
        return ESP_ERR_INVALID_ARG;
    lock();
    state_locked();
    if (!pw_spotify_command_valid(cmd, &service.snapshot)) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (cmd->kind != PW_SPOTIFY_VOLUME && service.count == QUEUE_COUNT) {
        unlock();
        return ESP_ERR_NO_MEM;
    }
    if (++service.next_request == 0)
        service.next_request = 1;
    pending_t item = {.command = *cmd, .id = service.next_request};
    if (cmd->kind == PW_SPOTIFY_VOLUME) {
        service.volume = item;
        service.volume_pending = true;
    } else {
        service.commands[(service.head + service.count) % QUEUE_COUNT] = item;
        service.count++;
    }
    service.snapshot.last_request_id = item.id;
    service.snapshot.last_command_state = PW_SPOTIFY_COMMAND_QUEUED;
    revision_locked();
    if (request_id)
        *request_id = item.id;
    unlock();
    wake();
    return ESP_OK;
}
esp_err_t pw_spotify_begin_auth(pw_spotify_auth_start_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!service.initialized)
        return ESP_ERR_INVALID_STATE;
    uint8_t random[56];
    esp_fill_random(random, sizeof(random));
    lock();
    if (service.storage_bad || service.disconnect_pending || service.auth_busy ||
        service.auth.exchanging || service.suspended) {
        unlock();
        pw_spotify_wipe(random, sizeof(random));
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = pw_spotify_auth_begin(&service.auth, random, now_us() / 1000, out);
    pw_spotify_wipe(service.auth_confirmation, sizeof(service.auth_confirmation));
    clear_commands_locked();
    state_locked();
    revision_locked();
    unlock();
    pw_spotify_wipe(random, sizeof(random));
    wake();
    return ok ? ESP_OK : ESP_FAIL;
}
esp_err_t pw_spotify_complete_auth(const char *code, const char *state) {
    if (!service.initialized)
        return ESP_ERR_INVALID_STATE;
    lock();
    if (service.storage_bad || service.disconnect_pending || service.auth_busy ||
        service.suspended || !pw_spotify_auth_accept(&service.auth, code, state, now_us() / 1000)) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    strcpy(service.auth_code, code);
    strcpy(service.auth_confirmation, state);
    service.auth_queued = true;
    state_locked();
    revision_locked();
    unlock();
    wake();
    return ESP_OK;
}
void pw_spotify_cancel_auth(void) {
    if (!service.initialized)
        return;
    lock();
    pw_spotify_auth_cancel(&service.auth);
    pw_spotify_wipe(service.auth_code, sizeof(service.auth_code));
    pw_spotify_wipe(service.auth_confirmation, sizeof(service.auth_confirmation));
    service.auth_queued = false;
    state_locked();
    revision_locked();
    unlock();
    wake();
}
esp_err_t pw_spotify_disconnect(void) {
    if (!service.initialized)
        return ESP_ERR_INVALID_STATE;
    lock();
    pw_spotify_auth_cancel(&service.auth);
    pw_spotify_wipe(service.auth_code, sizeof(service.auth_code));
    service.auth_queued = false;
    service.disconnect_pending = true;
    pw_spotify_wipe(service.auth_confirmation, sizeof(service.auth_confirmation));
    service.epoch++;
    service.snapshot.session = new_session();
    clear_commands_locked();
    state_locked();
    revision_locked();
    unlock();
    wake();
    return ESP_OK;
}
static esp_err_t load_record(void) {
    esp_err_t err = nvs_open_from_partition("settings", "spotify", NVS_READWRITE, &service.store);
    if (err != ESP_OK)
        return err;
    nvs_type_t type;
    err = nvs_find_key(service.store, "auth", &type);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK)
        return err;
    if (type != NVS_TYPE_BLOB)
        return ESP_ERR_INVALID_STATE;
    size_t n = 0;
    err = nvs_get_blob(service.store, "auth", NULL, &n);
    if (err != ESP_OK)
        return err;
    if (!n || n > PW_SPOTIFY_RECORD_BYTES)
        return ESP_ERR_INVALID_SIZE;
    uint8_t *bytes = malloc(n);
    if (!bytes)
        return ESP_ERR_NO_MEM;
    size_t actual = n;
    err = nvs_get_blob(service.store, "auth", bytes, &actual);
    if (err == ESP_OK && (actual != n || !pw_spotify_record_decode(bytes, n, &tokens)))
        err = ESP_ERR_INVALID_RESPONSE;
    pw_spotify_wipe(bytes, n);
    free(bytes);
    return err;
}
static esp_err_t save_record(const pw_spotify_tokens_t *value) {
    uint8_t *bytes = malloc(PW_SPOTIFY_RECORD_BYTES);
    if (!bytes)
        return ESP_ERR_NO_MEM;
    size_t n = pw_spotify_record_encode(value, bytes, PW_SPOTIFY_RECORD_BYTES);
    esp_err_t err = n ? nvs_set_blob(service.store, "auth", bytes, n) : ESP_ERR_INVALID_ARG;
    if (err == ESP_OK)
        err = nvs_commit(service.store);
    pw_spotify_wipe(bytes, PW_SPOTIFY_RECORD_BYTES);
    free(bytes);
    return err;
}
typedef struct {
    char *body;
    size_t size;
    int status;
    uint32_t retry_after;
    esp_err_t transport;
    bool overflow;
    uint32_t epoch;
} response_t;
static bool allowed(uint32_t epoch) {
    lock();
    bool yes = service.epoch == epoch && service.network && !service.suspended &&
               !service.disconnect_pending;
    unlock();
    return yes;
}
static esp_err_t http_event(esp_http_client_event_t *event) {
    response_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Retry-After"))
        r->retry_after = pw_spotify_retry_after(event->header_value);
    return ESP_OK;
}
static response_t request(bool token, esp_http_client_method_t method, const char *path,
                          const char *body, uint32_t epoch) {
    response_t r = {.retry_after = 30, .transport = ESP_FAIL, .epoch = epoch};
    if (!allowed(epoch)) {
        r.transport = ESP_ERR_INVALID_STATE;
        return r;
    }
    char url[768];
    int len = snprintf(url, sizeof(url), "%s%s",
                       token ? "https://accounts.spotify.com" : "https://api.spotify.com", path);
    if (len < 0 || (size_t)len >= sizeof(url)) {
        r.transport = ESP_ERR_INVALID_SIZE;
        return r;
    }
    r.body = calloc(1, RESPONSE_BYTES + 1);
    if (!r.body) {
        r.transport = ESP_ERR_NO_MEM;
        return r;
    }
    esp_http_client_config_t config = {.url = url,
                                       .method = method,
                                       .timeout_ms = 6000,
                                       .crt_bundle_attach = esp_crt_bundle_attach,
                                       .disable_auto_redirect = true,
                                       .event_handler = http_event,
                                       .user_data = &r,
                                       .buffer_size = 2048,
                                       .buffer_size_tx = 4096,
                                       .keep_alive_enable = false};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        r.transport = ESP_ERR_NO_MEM;
        return r;
    }
    char *bearer = NULL;
    int64_t started = now_us();
    if (!token) {
        bearer = malloc(PW_SPOTIFY_TOKEN_BYTES + 8);
        if (!bearer) {
            r.transport = ESP_ERR_NO_MEM;
            goto done;
        }
        snprintf(bearer, PW_SPOTIFY_TOKEN_BYTES + 8, "Bearer %s", tokens.access);
        r.transport = esp_http_client_set_header(client, "Authorization", bearer);
        if (r.transport != ESP_OK)
            goto done;
    }
    r.transport = esp_http_client_set_header(client, "Accept", "application/json");
    if (r.transport != ESP_OK)
        goto done;
    if (body) {
        r.transport = esp_http_client_set_header(client, "Content-Type",
                                                 token ? "application/x-www-form-urlencoded"
                                                       : "application/json");
        if (r.transport != ESP_OK)
            goto done;
    }
    r.transport = esp_http_client_open(client, body ? (int)strlen(body) : 0);
    if (r.transport != ESP_OK)
        goto done;
    if (body) {
        size_t offset = 0, n = strlen(body);
        while (offset < n) {
            if (!allowed(epoch) || now_us() - started > HTTP_DEADLINE_US) {
                r.transport = ESP_ERR_TIMEOUT;
                goto done;
            }
            int wrote = esp_http_client_write(client, body + offset, n - offset);
            if (wrote <= 0) {
                r.transport = ESP_FAIL;
                goto done;
            }
            offset += wrote;
        }
    }
    int64_t content = esp_http_client_fetch_headers(client);
    r.status = esp_http_client_get_status_code(client);
    if (content < 0) {
        r.transport = ESP_FAIL;
        goto done;
    }
    if (content > RESPONSE_BYTES) {
        r.overflow = true;
        r.transport = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    while (true) {
        if (!allowed(epoch) || now_us() - started > HTTP_DEADLINE_US) {
            r.transport = ESP_ERR_TIMEOUT;
            goto done;
        }
        if (r.size == RESPONSE_BYTES) {
            if (esp_http_client_is_complete_data_received(client))
                break;
            r.overflow = true;
            r.transport = ESP_ERR_INVALID_SIZE;
            goto done;
        }
        int n = esp_http_client_read(client, r.body + r.size, RESPONSE_BYTES - r.size);
        if (n < 0) {
            r.transport = ESP_FAIL;
            goto done;
        }
        if (!n) {
            if (!esp_http_client_is_complete_data_received(client)) {
                r.transport = ESP_FAIL;
                goto done;
            }
            break;
        }
        r.size += n;
    }
    r.body[r.size] = 0;
    r.transport = ESP_OK;
done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (bearer) {
        pw_spotify_wipe(bearer, PW_SPOTIFY_TOKEN_BYTES + 8);
        free(bearer);
    }
    return r;
}
static void response_free(response_t *r) {
    if (r->body) {
        pw_spotify_wipe(r->body, RESPONSE_BYTES + 1);
        free(r->body);
        r->body = NULL;
    }
}
static void report_response(const response_t *r) {
    lock();
    if (r->epoch == service.epoch) {
        /* IDF reports -1 when header reception fails before a status line.
         * Keep raw transport diagnostics internal; the USB/API contract uses
         * zero for unknown and real HTTP codes only, never a wrapped uint16_t. */
        service.snapshot.http_status =
            r->status >= 100 && r->status <= 599 ? (uint16_t)r->status : 0;
        if (service.storage_bad) {
            service.snapshot.error = PW_SPOTIFY_ERROR_STORAGE;
        } else if (r->status == 429) {
            service.cooldown_us = now_us() + (int64_t)r->retry_after * 1000000;
            service.snapshot.error = PW_SPOTIFY_ERROR_RATE_LIMIT;
        } else if (r->transport != ESP_OK)
            service.snapshot.error =
                r->transport == ESP_ERR_NO_MEM ? PW_SPOTIFY_ERROR_MEMORY : PW_SPOTIFY_ERROR_NETWORK;
        else if (r->status == 401)
            service.snapshot.error = PW_SPOTIFY_ERROR_AUTH;
        else if (r->status == 403)
            service.snapshot.error = PW_SPOTIFY_ERROR_FORBIDDEN;
        else if (r->status == 404)
            service.snapshot.error = PW_SPOTIFY_ERROR_NO_DEVICE;
        else if (r->status < 200 || r->status >= 300)
            service.snapshot.error = PW_SPOTIFY_ERROR_RESPONSE;
        else
            service.snapshot.error = PW_SPOTIFY_ERROR_NONE;
        state_locked();
        revision_locked();
    }
    unlock();
}
static void token_failure(bool fatal, pw_spotify_error_t error, uint32_t epoch) {
    lock();
    if (service.epoch == epoch) {
        if (fatal) {
            service.reauth = true;
            esp_err_t err = nvs_erase_key(service.store, "auth");
            if (err == ESP_ERR_NVS_NOT_FOUND)
                err = ESP_OK;
            if (err == ESP_OK)
                err = nvs_commit(service.store);
            pw_spotify_wipe(&tokens, sizeof(tokens));
            access_deadline_us = 0;
            service.snapshot.linked = false;
            service.snapshot.authorization_id[0] = 0;
            clear_commands_locked();
            if (err != ESP_OK) {
                service.storage_bad = true;
                error = PW_SPOTIFY_ERROR_STORAGE;
            }
        }
        service.snapshot.error = error;
        state_locked();
        revision_locked();
    }
    unlock();
}
/* Uses heap buffers so the worker stack never contains a full response/token pair. */
static bool exchange(const char *code, const char *verifier, uint32_t auth_generation,
                     uint32_t epoch) {
    bool initial = code != NULL;
    char *encoded = malloc(3 * PW_SPOTIFY_TOKEN_BYTES),
         *form = malloc(3 * PW_SPOTIFY_TOKEN_BYTES + 512);
    pw_spotify_tokens_t *fresh = calloc(1, sizeof(*fresh));
    if (!encoded || !form || !fresh) {
        free(encoded);
        free(form);
        free(fresh);
        token_failure(false, PW_SPOTIFY_ERROR_MEMORY, epoch);
        return false;
    }
    const char *credential = initial ? code : tokens.refresh;
    bool success = false;
    if (!pw_spotify_encode(credential, encoded, 3 * PW_SPOTIFY_TOKEN_BYTES))
        goto cleanup;
    if (initial) {
        char redirect[192];
        pw_spotify_encode(PW_SPOTIFY_REDIRECT_URI, redirect, sizeof(redirect));
        snprintf(
            form, 3 * PW_SPOTIFY_TOKEN_BYTES + 512,
            "grant_type=authorization_code&client_id=%s&code=%s&redirect_uri=%s&code_verifier=%s",
            PW_SPOTIFY_CLIENT_ID, encoded, redirect, verifier);
    } else
        snprintf(form, 3 * PW_SPOTIFY_TOKEN_BYTES + 512,
                 "grant_type=refresh_token&client_id=%s&refresh_token=%s", PW_SPOTIFY_CLIENT_ID,
                 encoded);
    response_t r = request(true, HTTP_METHOD_POST, "/api/token", form, epoch);
    report_response(&r);
    if (r.transport == ESP_OK && r.status == 200) {
        cJSON *j = pw_spotify_json(r.body, r.size);
        bool valid = j && pw_spotify_parse_token(j, initial ? NULL : &tokens, time(NULL), fresh);
        pw_spotify_secret_json_delete(j);
        if (valid) {
            /* Linearize cancellation against persistence; a bounded NVS commit
             * holds this lock, never a network operation. After cancellation
             * returns, an old token exchange cannot link a different account. */
            lock();
            bool current = service.epoch == epoch && !service.disconnect_pending &&
                           (!initial || service.auth.generation == auth_generation);
            if (current) {
                bool changed = initial || strcmp(fresh->refresh, tokens.refresh);
                esp_err_t err = changed ? save_record(fresh) : ESP_OK;
                if (err == ESP_OK) {
                    pw_spotify_wipe(&tokens, sizeof(tokens));
                    tokens = *fresh;
                    access_deadline_us = now_us() + (fresh->expires_at - time(NULL)) * 1000000;
                    service.snapshot.linked = true;
                    service.reauth = false;
                    service.snapshot.error = PW_SPOTIFY_ERROR_NONE;
                    if (initial) {
                        strcpy(service.snapshot.authorization_id, service.auth_confirmation);
                        service.snapshot.session = new_session();
                        service.snapshot.selection_generation = 1;
                        service.snapshot.selected_device_id[0] =
                            service.snapshot.selected_device_name[0] = 0;
                        service.snapshot.device_count = 0;
                        pw_spotify_clear_playback(&service.snapshot);
                        service.devices_at_us = service.playback_at_us = 0;
                        clear_commands_locked();
                        service.epoch++;
                    }
                    service.refresh_requested = true;
                    success = true;
                } else {
                    service.storage_bad = true;
                    service.snapshot.error = PW_SPOTIFY_ERROR_STORAGE;
                }
                state_locked();
                revision_locked();
            }
            unlock();
        } else
            token_failure(false, PW_SPOTIFY_ERROR_RESPONSE, epoch);
    } else if (!initial && r.transport == ESP_OK && (r.status == 400 || r.status == 401)) {
        cJSON *j = pw_spotify_json(r.body, r.size);
        const cJSON *error = j ? cJSON_GetObjectItemCaseSensitive(j, "error") : NULL;
        bool invalid = cJSON_IsString(error) && (!strcmp(error->valuestring, "invalid_grant") ||
                                                 !strcmp(error->valuestring, "invalid_client"));
        pw_spotify_secret_json_delete(j);
        token_failure(invalid, PW_SPOTIFY_ERROR_AUTH, epoch);
    }
    response_free(&r);
cleanup:
    pw_spotify_wipe(encoded, 3 * PW_SPOTIFY_TOKEN_BYTES);
    pw_spotify_wipe(form, 3 * PW_SPOTIFY_TOKEN_BYTES + 512);
    pw_spotify_wipe(fresh, sizeof(*fresh));
    free(encoded);
    free(form);
    free(fresh);
    return success;
}
static response_t api(esp_http_client_method_t method, const char *path, const char *body,
                      uint32_t epoch) {
    response_t r = request(false, method, path, body, epoch);
    if (r.transport == ESP_OK && r.status == 401) {
        response_free(&r);
        if (exchange(NULL, NULL, 0, epoch)) {
            r = request(false, method, path, body, epoch);
            if (r.status == 401 && r.transport == ESP_OK)
                token_failure(true, PW_SPOTIFY_ERROR_AUTH, epoch);
        } else {
            /* The original operation was explicitly rejected, never timed out.
             * Preserve the refresh's error/cooldown instead of overwriting it
             * with a synthetic network failure. The freed 401 has no body. */
            return r;
        }
    }
    report_response(&r);
    return r; /* Only an explicit 401 can trigger one retry. */
}
static void publish_devices(pw_spotify_snapshot_t *value, uint32_t epoch) {
    lock();
    if (service.epoch == epoch) {
        memcpy(service.snapshot.devices, value->devices, sizeof(value->devices));
        service.snapshot.device_count = value->device_count;
        service.snapshot.devices_truncated = value->devices_truncated;
        service.devices_at_us = now_us();
        state_locked();
        revision_locked();
    }
    unlock();
}
static void publish_playback(pw_spotify_snapshot_t *value, const pw_spotify_disallows_t *dis,
                             uint32_t epoch) {
    lock();
    if (service.epoch == epoch) {
        pw_spotify_snapshot_t *s = &service.snapshot;
        s->playback_known = value->playback_known;
        s->playing = value->playing;
        s->volume_known = value->volume_known;
        s->supports_volume = value->supports_volume;
        s->position_known = value->position_known;
        s->position_ms = value->position_ms;
        s->duration_ms = value->duration_ms;
        s->volume = value->volume;
        s->observed_at_ms = value->observed_at_ms;
        strcpy(s->title, value->title);
        strcpy(s->artist, value->artist);
        strcpy(s->item_type, value->item_type);
        strcpy(s->item_uri, value->item_uri);
        strcpy(s->context_uri, value->context_uri);
        strcpy(s->active_device_id, value->active_device_id);
        strcpy(s->active_device_name, value->active_device_name);
        service.disallows = *dis;
        service.playback_at_us = now_us();
        state_locked();
        revision_locked();
    }
    unlock();
}
static void poll_devices(uint32_t epoch, pw_spotify_snapshot_t *scratch) {
    response_t r = api(HTTP_METHOD_GET, "/v1/me/player/devices", NULL, epoch);
    if (r.transport == ESP_OK && r.status == 200) {
        cJSON *j = pw_spotify_json(r.body, r.size);
        bool valid = j && pw_spotify_parse_devices(j, scratch);
        cJSON_Delete(j);
        if (valid)
            publish_devices(scratch, epoch);
        else
            token_failure(false, PW_SPOTIFY_ERROR_RESPONSE, epoch);
    }
    response_free(&r);
}
static void poll_playback(uint32_t epoch, pw_spotify_snapshot_t *scratch) {
    response_t r =
        api(HTTP_METHOD_GET, "/v1/me/player?additional_types=track%2Cepisode", NULL, epoch);
    pw_spotify_disallows_t dis = {0};
    bool valid = false;
    if (r.transport == ESP_OK && r.status == 200) {
        cJSON *j = pw_spotify_json(r.body, r.size);
        valid = j && pw_spotify_parse_playback(j, scratch, &dis);
        cJSON_Delete(j);
        if (!valid)
            token_failure(false, PW_SPOTIFY_ERROR_RESPONSE, epoch);
    } else if (r.transport == ESP_OK && r.status == 204) {
        pw_spotify_clear_playback(scratch);
        valid = true;
    }
    if (valid) {
        scratch->observed_at_ms = now_us() / 1000;
        publish_playback(scratch, &dis, epoch);
    }
    response_free(&r);
}
static void finish_command(uint32_t id, pw_spotify_command_state_t state, uint32_t epoch) {
    lock();
    if (service.epoch == epoch && service.snapshot.last_request_id == id) {
        service.snapshot.last_command_state = state;
        revision_locked();
    }
    unlock();
}
static void execute(const pending_t *item, uint32_t epoch) {
    lock();
    state_locked();
    bool valid = pw_spotify_command_valid(&item->command, &service.snapshot);
    unlock();
    if (!valid) {
        finish_command(item->id, PW_SPOTIFY_COMMAND_STALE, epoch);
        return;
    }
    const pw_spotify_command_t *c = &item->command;
    char id[3 * PW_SPOTIFY_DEVICE_ID_BYTES], path[512], body[160];
    if (!pw_spotify_encode(c->device_id, id, sizeof(id))) {
        finish_command(item->id, PW_SPOTIFY_COMMAND_REJECTED, epoch);
        return;
    }
    const char *action = c->kind == PW_SPOTIFY_PLAY       ? "play"
                         : c->kind == PW_SPOTIFY_PAUSE    ? "pause"
                         : c->kind == PW_SPOTIFY_NEXT     ? "next"
                         : c->kind == PW_SPOTIFY_PREVIOUS ? "previous"
                                                          : "volume";
    snprintf(path, sizeof(path), "/v1/me/player/%s?device_id=%s", action, id);
    if (c->kind == PW_SPOTIFY_VOLUME) {
        size_t n = strlen(path);
        snprintf(path + n, sizeof(path) - n, "&volume_percent=%u", c->volume);
    }
    const char *payload = NULL;
    if (c->kind == PW_SPOTIFY_PLAY) {
        if (!c->uri[0])
            strcpy(body, "{}");
        else if (!strncmp(c->uri, "spotify:playlist:", 17))
            snprintf(body, sizeof(body), "{\"context_uri\":\"%s\"}", c->uri);
        else
            snprintf(body, sizeof(body), "{\"uris\":[\"%s\"]}", c->uri);
        payload = body;
    }
    response_t r =
        api(c->kind == PW_SPOTIFY_NEXT || c->kind == PW_SPOTIFY_PREVIOUS ? HTTP_METHOD_POST
                                                                         : HTTP_METHOD_PUT,
            path, payload, epoch);
    pw_spotify_command_state_t result = r.transport != ESP_OK ? PW_SPOTIFY_COMMAND_UNCERTAIN
                                        : r.status >= 200 && r.status < 300
                                            ? PW_SPOTIFY_COMMAND_ACCEPTED
                                            : PW_SPOTIFY_COMMAND_REJECTED;
    finish_command(item->id, result, epoch);
    response_free(&r);
    lock();
    service.refresh_requested = true;
    unlock();
}
static void disconnect_worker(void) {
    esp_err_t err = service.store ? nvs_erase_key(service.store, "auth") : ESP_ERR_INVALID_STATE;
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;
    if (err == ESP_OK)
        err = nvs_commit(service.store);
    lock();
    service.disconnect_pending = false;
    if (err == ESP_OK) {
        pw_spotify_wipe(&tokens, sizeof(tokens));
        access_deadline_us = 0;
        bool network = service.network;
        memset(&service.snapshot, 0, sizeof(service.snapshot));
        service.snapshot.enabled = true;
        service.snapshot.connected = network;
        service.snapshot.session = new_session();
        service.snapshot.selection_generation = 1;
        service.storage_bad = service.reauth = false;
        service.devices_at_us = service.playback_at_us = 0;
        service.cooldown_us = 0;
    } else {
        service.storage_bad = true;
        service.snapshot.error = PW_SPOTIFY_ERROR_STORAGE;
    }
    state_locked();
    revision_locked();
    unlock();
}
static void worker(void *unused) {
    (void)unused;
    pw_spotify_snapshot_t *scratch = calloc(1, sizeof(*scratch));
    if (!scratch) {
        lock();
        service.storage_bad = true;
        service.snapshot.error = PW_SPOTIFY_ERROR_MEMORY;
        state_locked();
        unlock();
        vTaskDelete(NULL);
        return;
    }
    int64_t poll_at = 0, devices_at = 0, refresh_retry = 0;
    uint32_t seen_epoch = 0;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        lock();
        int64_t now = now_us();
        if (service.auth.pending && now / 1000 >= service.auth.expires_ms) {
            pw_spotify_auth_cancel(&service.auth);
            service.snapshot.error = PW_SPOTIFY_ERROR_AUTH;
            revision_locked();
        }
        if (service.disconnect_pending) {
            unlock();
            disconnect_worker();
            poll_at = devices_at = refresh_retry = 0;
            continue;
        }
        if (service.epoch != seen_epoch) {
            seen_epoch = service.epoch;
            poll_at = devices_at = refresh_retry = 0;
        }
        if (service.snapshot.linked && time(NULL) >= CLOCK_MIN &&
            time(NULL) - tokens.authorized_at >= PW_SPOTIFY_REAUTH_SECONDS) {
            uint32_t expired_epoch = service.epoch;
            unlock();
            token_failure(true, PW_SPOTIFY_ERROR_AUTH, expired_epoch);
            continue;
        }
        state_locked();
        uint32_t epoch = service.epoch;
        bool usable = service.network && !service.suspended && !service.storage_bad &&
                      time(NULL) >= CLOCK_MIN && now >= service.cooldown_us;
        if (usable && service.auth_queued) {
            char code[PW_SPOTIFY_AUTH_CODE_BYTES], verifier[65];
            strcpy(code, service.auth_code);
            strcpy(verifier, service.auth.verifier);
            uint32_t generation = service.auth.generation;
            pw_spotify_wipe(service.auth_code, sizeof(service.auth_code));
            pw_spotify_wipe(service.auth.verifier, sizeof(service.auth.verifier));
            service.auth_queued = false;
            service.auth_busy = true;
            unlock();
            bool ok = exchange(code, verifier, generation, epoch);
            pw_spotify_wipe(code, sizeof(code));
            pw_spotify_wipe(verifier, sizeof(verifier));
            lock();
            service.auth_busy = false;
            if (service.auth.generation == generation) {
                pw_spotify_auth_cancel(&service.auth);
                pw_spotify_wipe(service.auth_confirmation, sizeof(service.auth_confirmation));
                if (!ok && service.snapshot.error == PW_SPOTIFY_ERROR_NONE)
                    service.snapshot.error = PW_SPOTIFY_ERROR_AUTH;
            }
            state_locked();
            revision_locked();
            unlock();
            continue;
        }
        if (!usable || !service.snapshot.linked || service.reauth || service.auth.pending ||
            service.auth.exchanging || service.auth_busy) {
            unlock();
            continue;
        }
        bool force = service.refresh_requested;
        service.refresh_requested = false;
        bool refresh = !tokens.access[0] || now + 60000000 >= access_deadline_us;
        if (refresh) {
            unlock();
            if (now >= refresh_retry) {
                bool ok = exchange(NULL, NULL, 0, epoch);
                refresh_retry = ok ? 0 : now_us() + 30000000;
            }
            continue;
        }
        pending_t item;
        bool command = false;
        if (service.count) {
            item = service.commands[service.head];
            pw_spotify_wipe(&service.commands[service.head], sizeof(item));
            service.head = (service.head + 1) % QUEUE_COUNT;
            service.count--;
            command = true;
        } else if (service.volume_pending) {
            item = service.volume;
            pw_spotify_wipe(&service.volume, sizeof(service.volume));
            service.volume_pending = false;
            command = true;
        }
        bool playing = service.snapshot.playing;
        unlock();
        if (command) {
            execute(&item, epoch);
            pw_spotify_wipe(&item, sizeof(item));
            poll_at = 0;
            continue;
        }
        if (force || now >= devices_at) {
            poll_devices(epoch, scratch);
            devices_at = now_us() + 60000000;
        }
        lock();
        bool can_poll = service.epoch == epoch && now_us() >= service.cooldown_us &&
                        !service.reauth && !service.auth.pending && !service.auth.exchanging;
        unlock();
        if (can_poll && (force || now >= poll_at)) {
            poll_playback(epoch, scratch);
            poll_at = now_us() + (playing ? 5000000 : 15000000);
        }
    }
}
esp_err_t pw_spotify_init(void) {
    if (service.initialized)
        return ESP_OK;
    service.mutex = xSemaphoreCreateMutex();
    if (!service.mutex)
        return ESP_ERR_NO_MEM;
    service.snapshot.enabled = true;
    service.snapshot.product_approved = false;
    service.snapshot.session = new_session();
    service.snapshot.selection_generation = 1;
    service.epoch = 1;
    esp_err_t err = load_record();
    if (err != ESP_OK) {
        service.storage_bad = true;
        service.snapshot.error = PW_SPOTIFY_ERROR_STORAGE;
        pw_spotify_wipe(&tokens, sizeof(tokens));
    }
    service.snapshot.linked = tokens.refresh[0] != 0;
    service.initialized = true;
    state_locked();
    revision_locked();
    if (xTaskCreate(worker, "pw_spotify", 12288, NULL, 3, &service.worker) != pdPASS) {
        service.initialized = false;
        service.snapshot.error = PW_SPOTIFY_ERROR_MEMORY;
        service.snapshot.state = PW_SPOTIFY_ERROR;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif
