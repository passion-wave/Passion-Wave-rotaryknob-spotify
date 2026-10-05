// SPDX-License-Identifier: MIT
/* Actual provider C with deterministic IDF boundary fakes. No network or USB. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static time_t fixture_time(time_t *out) {
    time_t t = 1790850000;
    if (out)
        *out = t;
    return t;
}
#define time fixture_time
#include "../pw_spotify.c"
#undef time
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "worker check failed line %d: %s\n", __LINE__, #x);                    \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static int mutex_depth;
static int64_t clock_us = 10000000;
static uint32_t rng = 23;
static unsigned http_count, response_count, commits, erased;
static bool fail_commit;
static char last_device_log[256], last_playback_log[256], last_validation_log[256];
static unsigned validation_logs;
static char last_transport_log[256], last_playback_device_log[256], last_token_log[256];
void pw_test_logi(const char *tag, const char *format, ...) {
    CHECK(!strcmp(tag, "pw_spotify"));
    char *output;
    if (!strncmp(format, "token_exchange ", 15)) output = last_token_log;
    else if (!strncmp(format, "devices ", 8)) output = last_device_log;
    else if (!strncmp(format, "playback_validation ", 20)) {
        output = last_validation_log;
        validation_logs++;
    }
    else if (!strncmp(format, "playback_transport ", sizeof("playback_transport ") - 1))
        output = last_transport_log;
    else if (!strncmp(format, "playback_device ", sizeof("playback_device ") - 1))
        output = last_playback_device_log;
    else {
        CHECK(!strncmp(format, "playback ", 9));
        output = last_playback_log;
    }
    va_list args;
    va_start(args, format);
    int n = vsnprintf(output, sizeof last_device_log, format, args);
    va_end(args);
    CHECK(n > 0 && n < (int)sizeof last_device_log);
}
static uint8_t stored[PW_SPOTIFY_RECORD_BYTES];
static size_t stored_size;
static const char *token_ok =
    "{\"access_token\":\"new-access\",\"refresh_token\":\"new-refresh\",\"expires_in\":3600,"
    "\"token_type\":\"Bearer\",\"scope\":\"user-read-playback-state user-modify-playback-state\"}";
typedef struct {
    int status;
    const char *body, *retry;
    bool timeout, header_timeout;
    unsigned transient_headers, header_calls;
    int socket_error, header_error, read_error;
    int64_t header_elapsed_us;
    void (*hook)(void), (*cleanup_hook)(void);
} fixture_t;
static fixture_t responses[8];
static struct {
    char url[768], body[8192];
    esp_http_client_method_t method;
} calls[8];
struct fake_http {
    esp_http_client_config_t config;
    unsigned index;
    size_t offset;
};
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (void *)1;
}
int xSemaphoreTake(SemaphoreHandle_t m, uint32_t n) {
    (void)m;
    (void)n;
    CHECK(!mutex_depth);
    mutex_depth++;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t m) {
    (void)m;
    CHECK(mutex_depth == 1);
    mutex_depth--;
    return 1;
}
int xTaskCreate(void (*f)(void *), const char *n, uint32_t z, void *a, unsigned p,
                TaskHandle_t *out) {
    (void)f;
    (void)n;
    (void)z;
    (void)a;
    (void)p;
    *out = (void *)1;
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t t) {
    (void)t;
}
uint32_t ulTaskNotifyTake(int c, uint32_t t) {
    (void)c;
    (void)t;
    return 0;
}
void vTaskDelete(TaskHandle_t t) {
    (void)t;
}
uint32_t esp_random(void) {
    return ++rng;
}
void esp_fill_random(void *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        ((uint8_t *)p)[i] = (uint8_t)++rng;
}
int64_t esp_timer_get_time(void) {
    return clock_us;
}
esp_err_t esp_crt_bundle_attach(void *p) {
    (void)p;
    return ESP_OK;
}
esp_err_t nvs_open_from_partition(const char *p, const char *n, int mode, nvs_handle_t *h) {
    CHECK(!strcmp(p, "settings") && !strcmp(n, "spotify") && mode == NVS_READWRITE);
    *h = 1;
    return ESP_OK;
}
esp_err_t nvs_find_key(nvs_handle_t h, const char *k, nvs_type_t *t) {
    (void)h;
    CHECK(!strcmp(k, "auth"));
    *t = NVS_TYPE_BLOB;
    return stored_size ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *p, size_t *n) {
    (void)h;
    (void)k;
    if (p) {
        CHECK(*n >= stored_size);
        memcpy(p, stored, stored_size);
    }
    *n = stored_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *p, size_t n) {
    (void)h;
    (void)k;
    CHECK(n <= sizeof stored);
    memcpy(stored, p, n);
    stored_size = n;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *k) {
    (void)h;
    (void)k;
    erased++;
    stored_size = 0;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    commits++;
    return fail_commit ? ESP_FAIL : ESP_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    CHECK(http_count < response_count);
    CHECK(c->crt_bundle_attach == esp_crt_bundle_attach);
    CHECK(c->disable_auto_redirect);
    struct fake_http *h = calloc(1, sizeof(*h));
    CHECK(h);
    h->config = *c;
    h->index = http_count++;
    snprintf(calls[h->index].url, sizeof(calls[0].url), "%s", c->url);
    calls[h->index].method = c->method;
    return h;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *k, const char *v) {
    (void)h;
    CHECK(k && v);
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int size) {
    (void)size;
    return responses[h->index].timeout ? ESP_ERR_TIMEOUT : ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t h, const char *p, int n) {
    CHECK(strlen(calls[h->index].body) + (size_t)n < sizeof calls[0].body);
    strncat(calls[h->index].body, p, (size_t)n);
    return n;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h, int ms) {
    CHECK(ms > 0 && ms <= 6000); h->config.timeout_ms = ms; return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
    fixture_t *r = &responses[h->index];
    r->header_calls++;
    clock_us += r->header_elapsed_us;
    if (r->transient_headers) {
        r->transient_headers--; clock_us += h->config.timeout_ms * 1000LL;
        if (r->hook) r->hook();
        return -ESP_ERR_HTTP_EAGAIN;
    }
    /* IDF sets response status to -1 before reading the first header. A
     * timeout here differs from failure to establish the connection. */
    if (r->header_timeout)
        return r->header_error ? r->header_error : -1;
    if (r->hook)
        r->hook();
    if (r->retry) {
        esp_http_client_event_t e = {.event_id = HTTP_EVENT_ON_HEADER,
                                     .header_key = "Retry-After",
                                     .header_value = (char *)r->retry,
                                     .user_data = h->config.user_data};
        h->config.event_handler(&e);
    }
    return r->body ? (int64_t)strlen(r->body) : 0;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h) {
    return responses[h->index].status;
}
int esp_http_client_get_errno(esp_http_client_handle_t h) {
    return responses[h->index].socket_error;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) {
    const char *b = responses[h->index].body;
    return h->offset == (b ? strlen(b) : 0);
}
int esp_http_client_read(esp_http_client_handle_t h, char *p, int n) {
    if (responses[h->index].read_error) return responses[h->index].read_error;
    const char *b = responses[h->index].body;
    if (!b)
        return 0;
    size_t left = strlen(b) - h->offset;
    if (left > (size_t)n)
        left = (size_t)n;
    memcpy(p, b + h->offset, left);
    h->offset += left;
    return (int)left;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t h) {
    (void)h;
    return ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) {
    if (responses[h->index].cleanup_hook)
        responses[h->index].cleanup_hook();
    free(h);
    return ESP_OK;
}
static void reset(void) {
    CHECK(!mutex_depth);
    memset(&service, 0, sizeof service);
    memset(&tokens, 0, sizeof tokens);
    memset(responses, 0, sizeof responses);
    memset(calls, 0, sizeof calls);
    http_count = response_count = commits = erased = 0;
    stored_size = 0;
    fail_commit = false;
    last_device_log[0] = last_playback_log[0] = last_validation_log[0] = 0;
    validation_logs = 0;
    last_transport_log[0] = last_playback_device_log[0] = last_token_log[0] = 0;
    clock_us = 10000000;
    CHECK(pw_spotify_init() == ESP_OK);
    pw_spotify_set_network(true);
}
static void account(void) {
    strcpy(tokens.access, "old-access");
    strcpy(tokens.refresh, "old-refresh");
    tokens.authorized_at = fixture_time(NULL);
    tokens.expires_at = tokens.authorized_at + 3600;
    access_deadline_us = clock_us + 3600000000LL;
    CHECK(save_record(&tokens) == ESP_OK);
    service.snapshot.linked = true;
    service.devices_at_us = service.playback_at_us = clock_us;
    service.snapshot.device_count = 2;
    for (unsigned i = 0; i < 2; i++) {
        pw_spotify_device_t *d = &service.snapshot.devices[i];
        snprintf(d->id, sizeof d->id, "speaker-%u", i + 1);
        snprintf(d->name, sizeof d->name, "Speaker %u", i + 1);
        d->supports_volume = d->volume_known = true;
        d->volume = 30;
    }
    CHECK(pw_spotify_select_device("speaker-1", service.snapshot.session) == ESP_OK);
    state_locked();
}
static pending_t play(pw_spotify_command_kind_t kind) {
    pending_t p = {.command = {.kind = kind,
                               .session = service.snapshot.session,
                               .selection_generation = service.snapshot.selection_generation}};
    strcpy(p.command.device_id, service.snapshot.selected_device_id);
    p.command.volume = 42;
    CHECK(pw_spotify_submit(&p.command, &p.id) == ESP_OK);
    return p;
}
static void fixture(int status, const char *body) {
    CHECK(response_count < 8);
    responses[response_count++] = (fixture_t){.status = status, .body = body};
}
static void switch_target(void) {
    CHECK(pw_spotify_select_device("speaker-2", service.snapshot.session) == ESP_OK);
}
static void cancel_exchange(void) {
    pw_spotify_cancel_auth();
}
static void test_targets_and_uncertain_commands(void) {
    reset();
    account();
    CHECK(http_count == 0);
    pending_t p = play(PW_SPOTIFY_PLAY);
    fixture(204, NULL);
    execute(&p, service.epoch);
    CHECK(strstr(calls[0].url, "/v1/me/player/play?device_id=speaker-1"));
    CHECK(!strcmp(calls[0].body, "{}"));
    CHECK(service.snapshot.last_command_state == PW_SPOTIFY_COMMAND_ACCEPTED);
    CHECK(!service.snapshot.playback_known);
    p = play(PW_SPOTIFY_PLAY);
    switch_target();
    execute(&p, service.epoch);
    CHECK(http_count == 1);
    CHECK(service.snapshot.last_request_id == 0);
    p = play(PW_SPOTIFY_PLAY);
    fixture(204, NULL);
    responses[1].hook = switch_target;
    /* Switch to a different selected target while response is in flight. */
    CHECK(pw_spotify_select_device("speaker-1", service.snapshot.session) == ESP_OK);
    p = play(PW_SPOTIFY_PLAY);
    execute(&p, service.epoch);
    CHECK(service.snapshot.last_request_id == 0);
    CHECK(service.snapshot.last_command_state == PW_SPOTIFY_COMMAND_NONE);
    service.snapshot.playback_known = true;
    service.snapshot.playing = true;
    strcpy(service.snapshot.active_device_id, "speaker-2");
    state_locked();
    p = play(PW_SPOTIFY_NEXT);
    fixture(0, NULL);
    responses[2].timeout = true;
    execute(&p, service.epoch);
    CHECK(http_count == 3);
    CHECK(service.snapshot.last_command_state == PW_SPOTIFY_COMMAND_UNCERTAIN);
}
static void test_refresh_rate_limit_and_invalidation(void) {
    reset();
    account();
    fixture(401, "{}");
    fixture(200, token_ok);
    fixture(204, NULL);
    response_t r = api(HTTP_METHOD_GET, "/v1/me/player", NULL, service.epoch);
    CHECK(r.status == 204 && r.transport == ESP_OK);
    response_free(&r);
    CHECK(http_count == 3);
    CHECK(!strcmp(tokens.refresh, "new-refresh"));
    CHECK(strstr(calls[1].body, "grant_type=refresh_token"));
    reset();
    account();
    fixture(429, "{}");
    responses[0].retry = "127";
    r = api(HTTP_METHOD_GET, "/v1/me/player", NULL, service.epoch);
    response_free(&r);
    CHECK(service.snapshot.state == PW_SPOTIFY_RATE_LIMITED);
    CHECK(service.snapshot.retry_after_seconds == 127);
    CHECK(http_count == 1);
    reset();
    account();
    fixture(401, "{}");
    fixture(429, "{}");
    responses[1].retry = "64";
    r = api(HTTP_METHOD_GET, "/v1/me/player", NULL, service.epoch);
    CHECK(r.status == 401 && r.transport == ESP_OK);
    response_free(&r);
    CHECK(http_count == 2);
    CHECK(service.snapshot.state == PW_SPOTIFY_RATE_LIMITED &&
          service.snapshot.retry_after_seconds == 64);
    reset();
    account();
    fixture(400, "{\"error\":\"invalid_grant\"}");
    CHECK(!exchange(NULL, NULL, 0, service.epoch));
    CHECK(service.reauth && !service.snapshot.linked);
    CHECK(!tokens.refresh[0] && !stored_size);
    CHECK(erased == 1);
    CHECK(service.snapshot.state == PW_SPOTIFY_REAUTH_REQUIRED);
    reset();
    account();
    fixture(401, "{}");
    fixture(200, token_ok);
    fixture(401, "{}");
    r = api(HTTP_METHOD_GET, "/v1/me/player", NULL, service.epoch);
    response_free(&r);
    CHECK(http_count == 3);
    CHECK(service.reauth && !tokens.refresh[0]);
    reset();
    account();
    fail_commit = true;
    token_failure(true, PW_SPOTIFY_ERROR_AUTH, service.epoch);
    CHECK(service.storage_bad);
    CHECK(service.snapshot.error == PW_SPOTIFY_ERROR_STORAGE);
    CHECK(!tokens.refresh[0]);
}
static void test_header_timeout_and_public_http_status(void) {
    reset();
    account();
    service.snapshot.playback_known = true;
    service.snapshot.playing = true;
    strcpy(service.snapshot.active_device_id, "speaker-1");
    state_locked();
    pending_t p = play(PW_SPOTIFY_NEXT);
    fixture(-1, NULL);
    responses[0].header_timeout = true;
    execute(&p, service.epoch);
    static pw_spotify_snapshot_t exposed;
    pw_spotify_get_snapshot(&exposed);
    CHECK(http_count == 1); /* No replay after an uncertain Next request. */
    CHECK(strstr(calls[0].url, "/v1/me/player/next?device_id=speaker-1"));
    CHECK(exposed.http_status == 0); /* Unknown status, never uint16_t(-1). */
    CHECK(exposed.error == PW_SPOTIFY_ERROR_NETWORK);
    CHECK(exposed.last_command_state == PW_SPOTIFY_COMMAND_UNCERTAIN);
    CHECK(exposed.linked && !strcmp(tokens.refresh, "old-refresh"));

    const int invalid[] = {-1, 0, 99, 600, 65535};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        response_t r = {.status = invalid[i], .transport = ESP_FAIL, .epoch = service.epoch};
        report_response(&r);
        pw_spotify_get_snapshot(&exposed);
        CHECK(exposed.http_status == 0);
    }
    const int valid[] = {100, 200, 204, 401, 429, 599};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        response_t r = {.status = valid[i], .transport = ESP_OK, .retry_after = 30,
                        .epoch = service.epoch};
        report_response(&r);
        pw_spotify_get_snapshot(&exposed);
        CHECK(exposed.http_status == valid[i]);
    }
}
static void test_auth_persistence_cancellation_and_failed_relink(void) {
    char state[PW_SPOTIFY_AUTH_STATE_BYTES], verifier[65];
    pw_spotify_auth_start_t start;
    reset();
    CHECK(pw_spotify_begin_auth(&start) == ESP_OK);
    strcpy(state, service.auth.state);
    strcpy(verifier, service.auth.verifier);
    CHECK(pw_spotify_complete_auth("code", state) == ESP_OK);
    CHECK(!service.snapshot.linked && !service.snapshot.authorization_id[0]);
    fixture(200, token_ok);
    CHECK(exchange("code", verifier, service.auth.generation, service.epoch));
    CHECK(service.snapshot.linked && stored_size);
    CHECK(!strcmp(service.snapshot.authorization_id, state));
    reset();
    account();
    CHECK(pw_spotify_begin_auth(&start) == ESP_OK);
    strcpy(state, service.auth.state);
    strcpy(verifier, service.auth.verifier);
    CHECK(pw_spotify_complete_auth("code", state) == ESP_OK);
    fixture(400, "{\"error\":\"invalid_grant\"}");
    CHECK(!exchange("code", verifier, service.auth.generation, service.epoch));
    CHECK(!strcmp(tokens.refresh, "old-refresh"));
    CHECK(!service.snapshot.authorization_id[0]);
    reset();
    CHECK(pw_spotify_begin_auth(&start) == ESP_OK);
    strcpy(state, service.auth.state);
    strcpy(verifier, service.auth.verifier);
    CHECK(pw_spotify_complete_auth("code", state) == ESP_OK);
    fixture(200, token_ok);
    responses[0].hook = cancel_exchange;
    CHECK(!exchange("code", verifier, service.auth.generation, service.epoch));
    CHECK(!stored_size && !service.snapshot.linked && !service.snapshot.authorization_id[0]);
    reset();
    CHECK(pw_spotify_begin_auth(&start) == ESP_OK);
    strcpy(state, service.auth.state);
    strcpy(verifier, service.auth.verifier);
    CHECK(pw_spotify_complete_auth("code", state) == ESP_OK);
    fixture(200, token_ok);
    fail_commit = true;
    CHECK(!exchange("code", verifier, service.auth.generation, service.epoch));
    CHECK(!service.snapshot.linked && !service.snapshot.authorization_id[0]);
    CHECK(service.storage_bad);
    reset();
    account();
    CHECK(pw_spotify_disconnect() == ESP_OK);
    disconnect_worker();
    CHECK(!stored_size && !tokens.refresh[0] && !service.snapshot.linked);
}
static void test_device_fetch_diagnostics(void) {
    pw_spotify_snapshot_t scratch = {0};
    reset();
    account();
    fixture(200, "{\"devices\":[{\"id\":\"SECRET-ID\",\"name\":\"SECRET-NAME\","
                 "\"type\":\"speaker\",\"is_active\":true,\"is_restricted\":false},"
                 "{\"id\":null}]}");
    poll_devices(service.epoch, &scratch);
    CHECK(service.snapshot.device_count == 1);
    CHECK(!strcmp(last_device_log, "devices http=200 transport=0 valid=1 listed=2 count=1"));
    CHECK(!strstr(last_device_log, "SECRET"));
    fixture(200, "{\"devices\":[]}");
    poll_devices(service.epoch, &scratch);
    CHECK(!strcmp(last_device_log, "devices http=200 transport=0 valid=1 listed=0 count=0"));
    CHECK(service.snapshot.device_count == 0);

    reset();
    account();
    fixture(403, "{\"error\":\"SECRET-REASON\"}");
    poll_devices(service.epoch, &scratch);
    CHECK(!strcmp(last_device_log, "devices http=403 transport=0 valid=0 listed=0 count=0"));
    fixture(204, "");
    poll_playback(service.epoch, &scratch);
    CHECK(service.snapshot.http_status == 204);
    CHECK(!strcmp(last_device_log, "devices http=403 transport=0 valid=0 listed=0 count=0"));

    reset();
    account();
    fixture(200, "{\"devices\":[{\"id\":\"SECRET-ID\"}]}");
    poll_devices(service.epoch, &scratch);
    CHECK(!strcmp(last_device_log, "devices http=200 transport=0 valid=0 listed=1 count=0"));
    CHECK(service.snapshot.error == PW_SPOTIFY_ERROR_RESPONSE);
    fixture(200, "SECRET-MALFORMED-RESPONSE");
    poll_devices(service.epoch, &scratch);
    CHECK(!strcmp(last_device_log, "devices http=200 transport=0 valid=0 listed=0 count=0"));
    fixture(200, "");
    responses[2].timeout = true;
    poll_devices(service.epoch, &scratch);
    CHECK(strstr(last_device_log, "devices http=0 transport="));
    CHECK(strstr(last_device_log, " valid=0 listed=0 count=0"));
    CHECK(!strstr(last_device_log, "SECRET"));
}
static void change_epoch_after_response(void) {
    ++service.epoch;
}
static void test_playback_fetch_diagnostics(void) {
    pw_spotify_snapshot_t scratch = {0};
    const char *playing = "{\"device\":{\"id\":\"speaker-1\",\"name\":\"SECRET-NAME\","
                          "\"type\":\"speaker\",\"is_active\":true,\"is_restricted\":false},"
                          "\"is_playing\":true,\"item\":{\"name\":\"SECRET-TITLE\"}}";
    reset();
    account();
    fixture(200, playing);
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_log,
        "playback http=200 transport=0 valid=1 known=1 playing=1 listed=1 selected=1"));
    CHECK(service.snapshot.playback_known && service.snapshot.playing);
    CHECK(validation_logs == 0);
    CHECK(!strcmp(service.snapshot.title, "SECRET-TITLE"));
    CHECK(!strstr(last_playback_log, "SECRET") && !strstr(last_playback_log, "speaker-1"));
    switch_target();
    fixture(200, playing);
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_log,
        "playback http=200 transport=0 valid=1 known=1 playing=1 listed=1 selected=0"));
    CHECK(!strcmp(service.snapshot.selected_device_id, "speaker-2"));
    fixture(204, "");
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_log,
        "playback http=204 transport=0 valid=1 known=0 playing=0 listed=0 selected=0"));
    CHECK(!service.snapshot.playback_known && !service.snapshot.playing);
    CHECK(!service.snapshot.title[0] && !service.snapshot.active_device_id[0]);
    CHECK(validation_logs == 0);

    reset();
    account();
    fixture(200, playing);
    poll_playback(service.epoch, &scratch);
    fixture(200, "SECRET-MALFORMED-RESPONSE");
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_log,
        "playback http=200 transport=0 valid=0 known=0 playing=0 listed=0 selected=0"));
    CHECK(scratch.playback_known && scratch.playing); /* Stale scratch is never reported. */
    CHECK(service.snapshot.playback_known && service.snapshot.error == PW_SPOTIFY_ERROR_RESPONSE);
    CHECK(!strcmp(last_validation_log, "playback_validation reason=31"));
    CHECK(validation_logs == 1);
    fixture(200, "{\"device\":{\"id\":\"speaker-1\",\"name\":\"SECRET\",\"type\":\"speaker\","
                 "\"is_active\":true,\"is_restricted\":false},\"is_playing\":true,\"progress_ms\":-1}");
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_log,
        "playback http=200 transport=0 valid=0 known=0 playing=0 listed=0 selected=0"));
    CHECK(scratch.playback_known && scratch.playing); /* Partially parsed scratch is masked too. */
    CHECK(!strcmp(last_validation_log, "playback_validation reason=9"));
    CHECK(validation_logs == 2);
    fixture(403, "{\"error\":\"SECRET-REASON\",\"access_token\":\"SECRET-TOKEN\"}");
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_log,
        "playback http=403 transport=0 valid=0 known=0 playing=0 listed=0 selected=0"));
    CHECK(service.snapshot.error == PW_SPOTIFY_ERROR_FORBIDDEN);
    CHECK(validation_logs == 2);
    fixture(200, "SECRET-UNREAD-BODY");
    responses[4].timeout = true;
    poll_playback(service.epoch, &scratch);
    char expected[256];
    snprintf(expected, sizeof expected,
        "playback http=0 transport=%d valid=0 known=0 playing=0 listed=0 selected=0", ESP_ERR_TIMEOUT);
    CHECK(!strcmp(last_playback_log, expected));
    CHECK(service.snapshot.error == PW_SPOTIFY_ERROR_NETWORK);
    fixture(-1, NULL);
    responses[5].header_timeout = true;
    poll_playback(service.epoch, &scratch);
    snprintf(expected, sizeof expected,
        "playback http=0 transport=%d valid=0 known=0 playing=0 listed=0 selected=0", ESP_FAIL);
    CHECK(!strcmp(last_playback_log, expected));
    CHECK(!strstr(last_playback_log, "SECRET"));

    reset();
    account();
    fixture(200, "{\"devices\":[{\"id\":\"SECRET-UNLISTED-ID\",\"name\":\"SECRET-NAME\","
                 "\"type\":\"speaker\",\"is_active\":true,\"is_restricted\":false},{}]}");
    poll_devices(service.epoch, &scratch);
    CHECK(scratch.device_count == 1 && service.snapshot.device_count == 2);
    CHECK(!strcmp(scratch.devices[0].id, "SECRET-UNLISTED-ID"));
    fixture(200, "{\"device\":{\"id\":\"SECRET-UNLISTED-ID\",\"name\":\"SECRET-NAME\","
                 "\"type\":\"speaker\",\"is_active\":true,\"is_restricted\":false},\"is_playing\":false}");
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_log,
        "playback http=200 transport=0 valid=1 known=1 playing=0 listed=0 selected=0"));
    CHECK(service.snapshot.playback_known && !service.snapshot.playing);
    CHECK(service.snapshot.device_count == 2 && !strcmp(service.snapshot.selected_device_id, "speaker-1"));
    CHECK(!strstr(last_playback_log, "SECRET"));

    reset();
    account();
    uint32_t epoch = service.epoch, revision = service.snapshot.revision;
    fixture(200, playing);
    responses[0].cleanup_hook = change_epoch_after_response;
    poll_playback(epoch, &scratch);
    CHECK(service.epoch != epoch && service.snapshot.revision == revision);
    CHECK(!service.snapshot.playback_known && !service.snapshot.active_device_id[0]);
    CHECK(!strcmp(last_playback_log,
        "playback http=200 transport=0 valid=1 known=1 playing=1 listed=0 selected=0"));
    CHECK(!strstr(last_playback_log, "SECRET") && !strstr(last_playback_log, "speaker-1"));
}
static void test_playback_device_and_transport_evidence(void) {
    pw_spotify_snapshot_t scratch = {0};
    reset(); account();
    fixture(200, "{\"device\":{\"id\":null,\"name\":\"SECRET-NAME\","
                 "\"type\":\"speaker\",\"is_active\":true,\"is_restricted\":true},"
                 "\"is_playing\":true,\"item\":{\"name\":\"SECRET-TITLE\"}}");
    poll_playback(service.epoch, &scratch);
    CHECK(service.snapshot.playback_known && service.snapshot.playing);
    CHECK(!service.snapshot.active_device_id[0]);
    CHECK(!strcmp(last_playback_device_log, "playback_device addressable=0 restricted=1"));
    CHECK(!service.snapshot.can_pause && !service.snapshot.can_next);
    CHECK(!strstr(last_playback_device_log, "SECRET") && !last_transport_log[0]);

    reset(); account();
    fixture(200, "{\"device\":{\"id\":\"speaker-1\",\"name\":\"SECRET\","
                 "\"type\":\"speaker\",\"is_active\":true,\"is_restricted\":false},\"is_playing\":true}");
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_playback_device_log, "playback_device addressable=1 restricted=0"));
    CHECK(!last_transport_log[0]);

    reset(); account();
    fixture(0, NULL); responses[0].timeout = true; responses[0].socket_error = 113;
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_transport_log, "playback_transport phase=1 detail=0 errno=113 elapsed=0"));
    CHECK(!last_playback_device_log[0]);

    reset(); account();
    fixture(-1, NULL); responses[0].header_timeout = true;
    responses[0].header_error = -0x7007; responses[0].header_elapsed_us = 6000000;
    responses[0].socket_error = 11;
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_transport_log, "playback_transport phase=3 detail=-28679 errno=11 elapsed=6000"));
    CHECK(service.snapshot.http_status == 0 && service.snapshot.error == PW_SPOTIFY_ERROR_NETWORK);
    CHECK(!last_playback_device_log[0] && validation_logs == 0);

    reset(); account();
    fixture(200, "SECRET-BODY"); responses[0].read_error = -1; responses[0].socket_error = 104;
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_transport_log, "playback_transport phase=4 detail=-1 errno=104 elapsed=0"));
    CHECK(!strstr(last_transport_log, "SECRET") && validation_logs == 0);

    reset(); account();
    fixture(-1, NULL); responses[0].header_timeout = true;
    responses[0].header_error = -70000; responses[0].header_elapsed_us = 700000000;
    responses[0].socket_error = -1;
    poll_playback(service.epoch, &scratch);
    CHECK(!strcmp(last_transport_log, "playback_transport phase=3 detail=-65535 errno=0 elapsed=600000"));
}
static void test_token_response_wait(void) {
    reset(); account();
    fixture(200, token_ok); responses[0].transient_headers = 2;
    CHECK(exchange("SECRET-CODE", "SECRET-VERIFIER", service.auth.generation, service.epoch));
    CHECK(http_count == 1 && responses[0].header_calls == 3);
    CHECK(strstr(calls[0].body, "grant_type=authorization_code"));
    CHECK(strstr(last_token_log, "initial=1 http=200 transport=0 phase=5"));
    CHECK(strstr(last_token_log, "elapsed=12000 confirmed=1"));
    CHECK(!strstr(last_token_log, "SECRET") && !strstr(last_token_log, "new-access"));
    reset(); account();
    fixture(0, NULL); responses[0].transient_headers = 99;
    CHECK(!exchange("SECRET-CODE", "SECRET-VERIFIER", service.auth.generation, service.epoch));
    CHECK(http_count == 1 && responses[0].header_calls == 5);
    CHECK(!strcmp(tokens.refresh, "old-refresh") && !service.snapshot.authorization_id[0]);
    CHECK(strstr(last_token_log, "initial=1 http=0") && strstr(last_token_log, "confirmed=0"));
    reset(); account();
    fixture(400, "{\"error\":\"invalid_grant\"}");
    CHECK(!exchange("SECRET-CODE", "SECRET-VERIFIER", service.auth.generation, service.epoch));
    CHECK(http_count == 1 && responses[0].header_calls == 1);
    reset(); account();
    fixture(0, NULL); responses[0].transient_headers = 99; responses[0].hook = cancel_exchange;
    CHECK(!exchange("SECRET-CODE", "SECRET-VERIFIER", service.auth.generation, service.epoch));
    CHECK(http_count == 1); /* A cancelled attempt never repeats the POST or replaces credentials. */
}
static void test_library_pages(void) {
    reset(); account();
    pw_spotify_library_page_t page;
    uint32_t session = service.snapshot.session;
    CHECK(pw_spotify_library_request(false, 100001, session) == ESP_ERR_INVALID_ARG);
    CHECK(pw_spotify_library_request(false, 0, session + 1) == ESP_ERR_INVALID_STATE);
    CHECK(pw_spotify_library_request(false, 0, session) == ESP_OK);
    CHECK(http_count == 0); /* HTTP handler enqueue is nonblocking. */
    CHECK(pw_spotify_library_request(false, 0, session) == ESP_OK);
    CHECK(pw_spotify_library_request(false, 10, session) == ESP_ERR_INVALID_STATE);
    fixture(200, "{\"items\":[{\"id\":\"0123456789012345678901\",\"name\":\"My playlist\"}],\"offset\":0,\"total\":2,\"next\":\"https://untrusted.invalid/do-not-follow\"}");
    poll_library(false, 0, service.epoch);
    pw_spotify_library_get(false, &page);
    CHECK(page.state == PW_LIBRARY_READY && page.count == 1 && page.has_more && page.next_offset == 1);
    CHECK(!strcmp(page.items[0].uri, "spotify:playlist:0123456789012345678901"));
    CHECK(strstr(calls[0].url, "/v1/me/playlists?limit=10&offset=0"));
    CHECK(pw_spotify_library_request(true, 0, session) == ESP_OK);
    fixture(403, "{}");
    pw_spotify_error_t previous = service.snapshot.error;
    poll_library(true, 0, service.epoch); pw_spotify_library_get(true, &page);
    CHECK(page.state == PW_LIBRARY_FORBIDDEN);
    CHECK(service.snapshot.linked && service.snapshot.error == previous);
    CHECK(pw_spotify_library_request(true, 0, session) == ESP_OK);
    fixture(200, "{\"items\":[{\"show\":{\"id\":\"abcdefghijklmnopqrstuv\",\"name\":\"My podcast\"}}],\"offset\":0,\"total\":1,\"next\":null}");
    poll_library(true, 0, service.epoch); pw_spotify_library_get(true, &page);
    CHECK(page.state == PW_LIBRARY_READY && page.count == 1 && !page.has_more);
    CHECK(!strcmp(page.items[0].uri, "spotify:show:abcdefghijklmnopqrstuv"));
    CHECK(pw_spotify_library_request(false, 1, session) == ESP_OK);
    fixture(200, "{\"items\":[],\"offset\":0,\"total\":0,\"next\":null}");
    poll_library(false, 1, service.epoch); pw_spotify_library_get(false, &page);
    CHECK(page.state == PW_LIBRARY_ERROR); /* Wrong page never accepted. */
    CHECK(pw_spotify_library_request(false, 0, session) == ESP_OK);
    fixture(429, "{}"); responses[response_count - 1].retry = "12";
    poll_library(false, 0, service.epoch); pw_spotify_library_get(false, &page);
    CHECK(page.retry_after_seconds == 12);
    CHECK(pw_spotify_library_request(false, 0, session) == ESP_ERR_INVALID_STATE);
    CHECK(pw_spotify_disconnect() == ESP_OK); pw_spotify_library_get(true, &page);
    CHECK(page.count == 0 && page.state == PW_LIBRARY_IDLE);
    /* Removed entries, invalid identifiers, malformed pagination. */
    const char *bad[] = {
      "{\"items\":[],\"offset\":0,\"total\":1,\"next\":\"url\"}",
      "{\"items\":[{\"id\":\"bad\",\"name\":\"x\"}],\"offset\":0,\"total\":1,\"next\":null}",
      "{\"items\":[],\"offset\":0,\"total\":-1,\"next\":null}"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        cJSON *j = cJSON_Parse(bad[i]); CHECK(!pw_spotify_parse_library(j, false, 0, &page)); cJSON_Delete(j);
    }
    cJSON *j = cJSON_Parse("{\"items\":[null],\"offset\":0,\"total\":1,\"next\":null}");
    CHECK(pw_spotify_parse_library(j, false, 0, &page) && page.count == 0); cJSON_Delete(j);
}
int main(void) {
    test_token_response_wait();
    test_library_pages();
    test_targets_and_uncertain_commands();
    test_refresh_rate_limit_and_invalidation();
    test_header_timeout_and_public_http_status();
    test_auth_persistence_cancellation_and_failed_relink();
    test_device_fetch_diagnostics();
    test_playback_fetch_diagnostics();
    test_playback_device_and_transport_evidence();
    printf("Spotify worker: %u assertions passed; actual provider, simulated IDF HTTP/NVS/tasks\n",
           checks);
    return 0;
}
