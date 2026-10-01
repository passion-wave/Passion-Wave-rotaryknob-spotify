// SPDX-License-Identifier: MIT
/* Actual provider C with deterministic IDF boundary fakes. No network or USB. */
#include <assert.h>
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
static uint8_t stored[PW_SPOTIFY_RECORD_BYTES];
static size_t stored_size;
static const char *token_ok =
    "{\"access_token\":\"new-access\",\"refresh_token\":\"new-refresh\",\"expires_in\":3600,"
    "\"token_type\":\"Bearer\",\"scope\":\"user-read-playback-state user-modify-playback-state\"}";
typedef struct {
    int status;
    const char *body, *retry;
    bool timeout, header_timeout;
    void (*hook)(void);
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
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
    fixture_t *r = &responses[h->index];
    /* IDF sets response status to -1 before reading the first header. A
     * timeout here differs from failure to establish the connection. */
    if (r->header_timeout)
        return -1;
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
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) {
    const char *b = responses[h->index].body;
    return h->offset == (b ? strlen(b) : 0);
}
int esp_http_client_read(esp_http_client_handle_t h, char *p, int n) {
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
int main(void) {
    test_targets_and_uncertain_commands();
    test_refresh_rate_limit_and_invalidation();
    test_header_timeout_and_public_http_status();
    test_auth_persistence_cancellation_and_failed_relink();
    printf("Spotify worker: %u assertions passed; actual provider, simulated IDF HTTP/NVS/tasks\n",
           checks);
    return 0;
}
