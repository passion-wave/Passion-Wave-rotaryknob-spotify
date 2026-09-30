// SPDX-License-Identifier: MIT
// Laboratory implementation: protected setup AP writes; LAN write trust remains G2.
#include "pw_app.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pw_board.h"
#include "pw_protocol.h"
#include "pw_storage.h"
#include "pw_update_service.h"
#include "pw_validation.h"
#include "pw_weather.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CONFIG_MAX 49152
#define CONFIG_BLOB_KEY "config_blob"
#define SETUP_SECONDS 600
static const char *TAG = "pw_app";
static SemaphoreHandle_t lock;
static nvs_handle_t store;
static cJSON *config;
static pw_app_view_t view;
static wifi_config_t pending_wifi, candidate_wifi;
static bool wifi_pending, brightness_dirty, candidate_active, persist_wifi;
static int64_t setup_until, brightness_changed, retry_at, peer_seen;
static uint32_t weather_generation = 1;
static unsigned retry_count;
static esp_netif_t *station_if, *ap_if;
static httpd_handle_t server;
static char session[65], csrf[65];
static int64_t session_until;
static pw_weather_snapshot_t weather_copy;
extern const uint8_t page_start[] asm("_binary_index_html_start");
extern const uint8_t page_end[] asm("_binary_index_html_end");
extern const uint8_t js_start[] asm("_binary_app_js_start");
extern const uint8_t js_end[] asm("_binary_app_js_end");
extern const uint8_t css_start[] asm("_binary_style_css_start");
extern const uint8_t css_end[] asm("_binary_style_css_end");
extern const uint8_t places_start[] asm("_binary_places_de_json_gz_start");
extern const uint8_t places_end[] asm("_binary_places_de_json_gz_end");
static void take(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
}
static void give(void) {
    xSemaphoreGive(lock);
}
static cJSON *item(const cJSON *o, const char *k) {
    return cJSON_GetObjectItemCaseSensitive(o, k);
}
static const char *text(const cJSON *o, const char *k) {
    const cJSON *v = item(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static double number(const cJSON *o, const char *k, double d) {
    const cJSON *v = item(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : d;
}
static bool flag(const cJSON *o, const char *k) {
    return cJSON_IsTrue(item(o, k));
}
static void random_hex(char *out, size_t bytes) {
    static const char h[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; i++) {
        uint8_t b = (uint8_t)esp_random();
        out[i * 2] = h[b >> 4];
        out[i * 2 + 1] = h[b & 15];
    }
    out[bytes * 2] = 0;
}
static bool same_secret(const char *a, const char *b) {
    size_t n = strlen(a);
    if (n != strlen(b))
        return false;
    unsigned d = 0;
    for (size_t i = 0; i < n; i++)
        d |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return d == 0;
}
static bool clean_text(const char *s, size_t min, size_t max) {
    size_t n = strlen(s);
    if (n < min || n > max)
        return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32)
            return false;
    return true;
}
static void delete_sensitive_json(cJSON *object) {
    if (!object)
        return;
    for (cJSON *value = object->child; value; value = value->next)
        if (cJSON_IsString(value) && value->valuestring)
            mbedtls_platform_zeroize(value->valuestring, strlen(value->valuestring));
    cJSON_Delete(object);
}
static bool wifi_credentials_valid(const cJSON *object) {
    const char *const keys[] = {"ssid", "password"};
    if (!pw_keys_only(object, keys, 2) || !cJSON_IsString(item(object, "ssid")) ||
        !cJSON_IsString(item(object, "password")) || !clean_text(text(object, "ssid"), 1, 32))
        return false;
    size_t length = strlen(text(object, "password"));
    return length == 0 || (length >= 8 && length <= 63);
}
/* Caller holds lock. The already allocated revision/brightness nodes are updated
 * without allocating, so a failed replacement cannot masquerade as a saved edit. */
static bool prepare_config_copy(cJSON *next) {
    if (!pw_validate_config(next) || view.revision == UINT32_MAX ||
        number(next, "revision", 0) != view.revision)
        return false;
    if (brightness_dirty)
        cJSON_SetNumberValue(item(item(next, "settings"), "brightness"), view.brightness);
    cJSON_SetNumberValue(item(next, "revision"), (double)view.revision + 1);
    return true;
}
static cJSON *default_config(void) {
    return cJSON_Parse(
        "{\"schema\":1,\"revision\":1,\"settings\":{\"name\":\"PassionWave\",\"brightness\":65,"
        "\"avatar_enabled\":true,\"avatar_blond\":false,\"screensaver_mode\":\"weather_photo\","
        "\"haptic\":true,\"weather_enabled\":false,\"latitude\":null,\"longitude\":null,"
        "\"timezone\":\"Europe/Berlin\"},\"catalog\":{\"favorites\":[],\"stations\":[]}}");
}
/* caller holds lock. Single NVS blob commit binds revision, settings and catalog. */
static esp_err_t save_config(cJSON *next) {
    if (!pw_validate_next_config(next, view.revision))
        return ESP_ERR_INVALID_ARG;
    char *json = cJSON_PrintUnformatted(next);
    if (!json)
        return ESP_ERR_NO_MEM;
    size_t n = strlen(json) + 1;
    esp_err_t e =
        n > CONFIG_MAX ? ESP_ERR_INVALID_SIZE : nvs_set_blob(store, CONFIG_BLOB_KEY, json, n);
    if (e == ESP_OK)
        e = nvs_commit(store);
    free(json);
    if (e == ESP_OK) {
        cJSON_Delete(config);
        config = next;
        view.revision = (uint32_t)number(config, "revision", 1);
        cJSON *s = item(config, "settings");
        strlcpy(view.name, text(s, "name"), sizeof view.name);
        view.brightness = (uint8_t)number(s, "brightness", 65);
        view.haptic = flag(s, "haptic");
        view.weather_enabled = flag(s, "weather_enabled");
        view.avatar_enabled = flag(s, "avatar_enabled");
        view.avatar_blond = flag(s, "avatar_blond");
        strlcpy(view.screensaver_mode, text(s, "screensaver_mode"), sizeof view.screensaver_mode);
        brightness_dirty = false;
    }
    return e;
}
static void apply_settings(bool changed_location) {
    take();
    cJSON *s = item(config, "settings");
    bool enabled = flag(s, "weather_enabled");
    double lat = number(s, "latitude", NAN), lon = number(s, "longitude", NAN);
    uint8_t b = view.brightness;
    if (changed_location)
        weather_generation++;
    uint32_t g = weather_generation;
    give();
    pw_board_set_brightness(b);
    pw_weather_configure(enabled, lat, lon, g);
}
void pw_app_get_view(pw_app_view_t *out) {
    if (!out)
        return;
    take();
    *out = view;
    int64_t left = setup_until - esp_timer_get_time();
    out->setup_seconds_left = left > 0 ? (uint32_t)(left / 1000000) : 0;
    out->peer_connected = peer_seen && esp_timer_get_time() - peer_seen < 6000000;
    give();
}
void pw_app_adjust_brightness(int delta) {
    take();
    int b = view.brightness + delta;
    if (b < 10)
        b = 10;
    if (b > 100)
        b = 100;
    view.brightness = (uint8_t)b;
    brightness_dirty = true;
    brightness_changed = esp_timer_get_time();
    give();
    pw_board_set_brightness((uint8_t)b);
}
esp_err_t pw_app_open_setup(void) {
    take();
    if (!view.setup_password[0]) {
        char tmp[17];
        random_hex(tmp, 8);
        strlcpy(view.setup_password, tmp, sizeof view.setup_password);
        mbedtls_platform_zeroize(tmp, sizeof tmp);
    }
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, view.setup_ssid, sizeof ap.ap.ssid);
    strlcpy((char *)ap.ap.password, view.setup_password, sizeof ap.ap.password);
    ap.ap.ssid_len = strlen(view.setup_ssid);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 1;
    ap.ap.channel = 1;
    ap.ap.pmf_cfg.capable = true;
    give();
    esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (e == ESP_OK)
        e = esp_wifi_set_config(WIFI_IF_AP, &ap);
    mbedtls_platform_zeroize(&ap, sizeof ap);
    take();
    if (e == ESP_OK) {
        view.setup_open = true;
        setup_until = esp_timer_get_time() + SETUP_SECONDS * 1000000LL;
    }
    give();
    return e;
}
void pw_app_close_setup(void) {
    take();
    view.setup_open = false;
    setup_until = 0;
    session_until = 0;
    mbedtls_platform_zeroize(session, sizeof session);
    mbedtls_platform_zeroize(csrf, sizeof csrf);
    mbedtls_platform_zeroize(view.setup_password, sizeof view.setup_password);
    give();
    esp_wifi_set_mode(WIFI_MODE_STA);
}
static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        take();
        view.connected = true;
        view.connecting = false;
        retry_count = 0;
        retry_at = 0;
        view.network_error[0] = 0;
        if (candidate_active)
            persist_wifi = true;
        snprintf(view.ip, sizeof view.ip, IPSTR, IP2STR(&ev->ip_info.ip));
        give();
        pw_weather_request_refresh();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        take();
        view.connected = false;
        view.ip[0] = 0;
        view.connecting = false;
        if (ev->reason == WIFI_REASON_AUTH_FAIL || ev->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)
            strlcpy(view.network_error, "WLAN-Passwort oder Zugang prüfen",
                    sizeof view.network_error);
        else
            strlcpy(view.network_error, "WLAN nicht erreichbar", sizeof view.network_error);
        unsigned shift = retry_count < 5 ? retry_count++ : 5;
        retry_at = esp_timer_get_time() + ((int64_t)1 << shift) * 1000000;
        give();
    }
}
/* The AP endpoint is reachable via its own local socket address, not a forged Host/IP header. */
static bool is_ap_request(httpd_req_t *r) {
    struct sockaddr_in local = {0}, remote = {0};
    socklen_t n = sizeof local;
    int fd = httpd_req_to_sockfd(r);
    if (getsockname(fd, (struct sockaddr *)&local, &n))
        return false;
    n = sizeof remote;
    if (getpeername(fd, (struct sockaddr *)&remote, &n))
        return false;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(ap_if, &ip) != ESP_OK)
        return false;
    take();
    bool opened = view.setup_open && setup_until > esp_timer_get_time();
    give();
    if (!opened || local.sin_family != AF_INET || remote.sin_family != AF_INET ||
        local.sin_addr.s_addr != ip.ip.addr ||
        (remote.sin_addr.s_addr & ip.netmask.addr) != (ip.ip.addr & ip.netmask.addr))
        return false;
    wifi_sta_list_t associated = {0};
    wifi_sta_mac_ip_list_t mapped = {0};
    if (esp_wifi_ap_get_sta_list(&associated) != ESP_OK ||
        esp_wifi_ap_get_sta_list_with_ip(&associated, &mapped) != ESP_OK)
        return false;
    for (int i = 0; i < mapped.num; i++)
        if (mapped.sta[i].ip.addr == remote.sin_addr.s_addr)
            return true;
    return false;
}
static bool host_valid(httpd_req_t *r) {
    char host[80] = {0};
    if (httpd_req_get_hdr_value_str(r, "Host", host, sizeof host) != ESP_OK)
        return false;
    struct sockaddr_in local = {0};
    socklen_t n = sizeof local;
    if (getsockname(httpd_req_to_sockfd(r), (struct sockaddr *)&local, &n))
        return false;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local.sin_addr, ip, sizeof ip);
    char withport[64];
    snprintf(withport, sizeof withport, "%s:80", ip);
    return !strcmp(host, ip) || !strcmp(host, withport);
}
static void headers(httpd_req_t *r) {
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(
        r, "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
        "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}
static esp_err_t memory_error(httpd_req_t *r) {
    headers(r);
    httpd_resp_set_status(r, "503 Service Unavailable");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r,
                           "{\"error\":{\"code\":\"memory\",\"message\":\"Speicher belegt. Bitte "
                           "erneut versuchen.\"}}",
                           HTTPD_RESP_USE_STRLEN);
}
static bool add_owned(cJSON *parent, const char *key, cJSON *child) {
    if (child && cJSON_AddItemToObject(parent, key, child))
        return true;
    cJSON_Delete(child);
    return false;
}
static esp_err_t json_response(httpd_req_t *r, cJSON *j) {
    if (!j)
        return memory_error(r);
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!s)
        return memory_error(r);
    headers(r);
    httpd_resp_set_type(r, "application/json");
    esp_err_t e = httpd_resp_send(r, s, HTTPD_RESP_USE_STRLEN);
    free(s);
    return e;
}
static esp_err_t revision_response(httpd_req_t *r, uint32_t revision) {
    char body[64];
    snprintf(body, sizeof body, "{\"config_revision\":%lu}", (unsigned long)revision);
    headers(r);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, body, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t error(httpd_req_t *r, const char *status, const char *code, const char *message) {
    httpd_resp_set_status(r, status);
    cJSON *j = cJSON_CreateObject(), *e = cJSON_AddObjectToObject(j, "error");
    if (!e || !cJSON_AddStringToObject(e, "code", code) ||
        !cJSON_AddStringToObject(e, "message", message)) {
        cJSON_Delete(j);
        return memory_error(r);
    }
    return json_response(r, j);
}
static bool authorized(httpd_req_t *r, bool mutation) {
    if (!host_valid(r) || !is_ap_request(r))
        return false;
    char cookie[160] = {0}, expected[80];
    if (httpd_req_get_hdr_value_str(r, "Cookie", cookie, sizeof cookie) != ESP_OK)
        return false;
    take();
    snprintf(expected, sizeof expected, "pw_session=%s", session);
    bool valid =
        session[0] && session_until > esp_timer_get_time() && same_secret(cookie, expected);
    give();
    if (!valid)
        return false;
    if (!mutation)
        return true;
    char origin[80] = {0}, token[80] = {0}, host[80] = {0}, want[100];
    httpd_req_get_hdr_value_str(r, "Origin", origin, sizeof origin);
    httpd_req_get_hdr_value_str(r, "Host", host, sizeof host);
    snprintf(want, sizeof want, "http://%s", host);
    if (strcmp(origin, want) ||
        httpd_req_get_hdr_value_str(r, "X-CSRF-Token", token, sizeof token) != ESP_OK)
        return false;
    take();
    valid = same_secret(token, csrf);
    give();
    return valid;
}
static cJSON *read_body(httpd_req_t *r, size_t max) {
    if (!r->content_len || r->content_len > max)
        return NULL;
    char ct[80] = {0};
    if (httpd_req_get_hdr_value_str(r, "Content-Type", ct, sizeof ct) != ESP_OK ||
        strncmp(ct, "application/json", 16) || (ct[16] && ct[16] != ';'))
        return NULL;
    char *body = malloc(r->content_len + 1);
    if (!body)
        return NULL;
    size_t used = 0;
    while (used < r->content_len) {
        int got = httpd_req_recv(r, body + used, r->content_len - used);
        if (got <= 0) {
            mbedtls_platform_zeroize(body, r->content_len + 1);
            free(body);
            return NULL;
        }
        used += got;
    }
    body[used] = 0;
    cJSON *j = pw_parse_json(body, used + 1);
    mbedtls_platform_zeroize(body, used + 1);
    free(body);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return NULL;
    }
    /* Endpoint validators reject duplicate and unknown fields. */
    return j;
}
static bool revision_matches(httpd_req_t *r) {
    char match[40] = {0}, a[32], b[34];
    if (httpd_req_get_hdr_value_str(r, "If-Match", match, sizeof match) != ESP_OK)
        return false;
    snprintf(a, sizeof a, "%lu", (unsigned long)view.revision);
    snprintf(b, sizeof b, "\"%s\"", a);
    return !strcmp(a, match) || !strcmp(b, match);
}
static esp_err_t session_handler(httpd_req_t *r) {
    char cookie[160];
    bool secure = host_valid(r) && is_ap_request(r);
    cJSON *j = cJSON_CreateObject();
    if (!j || !cJSON_AddBoolToObject(j, "secure_write", secure)) {
        cJSON_Delete(j);
        return memory_error(r);
    }
    if (secure) {
        take();
        if (session_until <= esp_timer_get_time()) {
            random_hex(session, 32);
            random_hex(csrf, 32);
        }
        session_until = esp_timer_get_time() + 300000000LL;
        snprintf(cookie, sizeof cookie,
                 "pw_session=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=300", session);
        httpd_resp_set_hdr(r, "Set-Cookie", cookie);
        bool added = cJSON_AddStringToObject(j, "csrf", csrf) != NULL;
        give();
        if (!added) {
            cJSON_Delete(j);
            return memory_error(r);
        }
    }
    return json_response(r, j);
}
static cJSON *weather_json(void) {
    pw_weather_get_snapshot(&weather_copy);
    cJSON *w = cJSON_CreateObject();
    bool ok =
        w && cJSON_AddStringToObject(w, "state", pw_weather_status_name(weather_copy.status)) &&
        cJSON_AddStringToObject(w, "source", weather_copy.source) &&
        cJSON_AddStringToObject(w, "attribution", PW_WEATHER_ATTRIBUTION) &&
        cJSON_AddNumberToObject(w, "checked_utc", (double)weather_copy.checked_utc) &&
        cJSON_AddNumberToObject(w, "source_updated_utc", (double)weather_copy.source_updated_utc) &&
        cJSON_AddBoolToObject(w, "model_forecast", true);
    cJSON *cur = cJSON_AddObjectToObject(w, "current");
    ok = ok && cur;
    if (weather_copy.current_valid) {
        if (weather_copy.current.valid & PW_WEATHER_TEMPERATURE)
            ok = ok &&
                 cJSON_AddNumberToObject(cur, "temperature", weather_copy.current.temperature_c);
        if (weather_copy.current.valid & PW_WEATHER_WIND)
            ok =
                ok && cJSON_AddNumberToObject(cur, "wind_kmh", weather_copy.current.wind_mps * 3.6);
        if (weather_copy.current.valid & PW_WEATHER_SYMBOL)
            ok = ok && cJSON_AddStringToObject(cur, "condition", weather_copy.current.symbol);
    }
    cJSON *days = cJSON_AddArrayToObject(w, "days");
    ok = ok && days;
    for (unsigned i = 0; ok && i < weather_copy.day_count; i++) {
        const pw_weather_day_t *d = &weather_copy.days[i];
        cJSON *x = cJSON_CreateObject();
        if (!x || !cJSON_AddItemToArray(days, x)) {
            cJSON_Delete(x);
            ok = false;
            break;
        }
        char date[16];
        snprintf(date, sizeof date, "%04d-%02d-%02d", d->year, d->month, d->day);
        ok = cJSON_AddStringToObject(x, "date", date);
        if (d->temperature_valid)
            ok = ok && cJSON_AddNumberToObject(x, "min", d->temperature_min_c) &&
                 cJSON_AddNumberToObject(x, "max", d->temperature_max_c);
        ok = ok && cJSON_AddStringToObject(x, "condition", d->symbol);
    }
    if (!ok) {
        cJSON_Delete(w);
        return NULL;
    }
    return w;
}
static esp_err_t status_handler(httpd_req_t *r) {
    if (!host_valid(r))
        return error(r, "400 Bad Request", "host", "Ungültige Geräteadresse");
    bool admin = authorized(r, false);
    pw_app_view_t v;
    pw_app_get_view(&v);
    cJSON *j = cJSON_CreateObject(), *d = cJSON_AddObjectToObject(j, "device");
    bool ok = d && cJSON_AddStringToObject(d, "name", v.name) &&
              cJSON_AddStringToObject(d, "version", esp_app_get_description()->version) &&
              cJSON_AddStringToObject(d, "hardware", PW_APP_HARDWARE) &&
              cJSON_AddBoolToObject(d, "peer_connected", v.peer_connected);
    cJSON *n = cJSON_AddObjectToObject(j, "network");
    ok = ok && n && cJSON_AddBoolToObject(n, "connected", v.connected) &&
         cJSON_AddBoolToObject(n, "connecting", v.connecting) &&
         cJSON_AddBoolToObject(n, "setup_open", v.setup_open) &&
         cJSON_AddStringToObject(n, "ip", v.ip);
    if (admin)
        ok = ok && cJSON_AddStringToObject(n, "ssid", v.ssid) &&
             cJSON_AddStringToObject(n, "error", v.network_error);
    cJSON *c = cJSON_AddObjectToObject(j, "capabilities");
    pw_update_service_view_t update;
    pw_update_service_get_view(&update);
    ok = ok && c && cJSON_AddBoolToObject(c, "signed_bundle_staging", update.upload_enabled) &&
         cJSON_AddBoolToObject(c, "spotify", false) &&
         cJSON_AddBoolToObject(c, "radio_playback", false) &&
         cJSON_AddBoolToObject(c, "pair_ota", false) &&
         cJSON_AddBoolToObject(c, "secure_lan_write", false) &&
         cJSON_AddBoolToObject(j, "secure_write", admin);
    if (admin && ok) {
        take();
        ok = add_owned(j, "settings", cJSON_Duplicate(item(config, "settings"), true)) &&
             add_owned(j, "catalog", cJSON_Duplicate(item(config, "catalog"), true)) &&
             cJSON_AddNumberToObject(j, "config_revision", view.revision);
        give();
        ok = ok && add_owned(j, "weather", weather_json());
    }
    if (!ok) {
        cJSON_Delete(j);
        return memory_error(r);
    }
    return json_response(r, j);
}
static esp_err_t scan_handler(httpd_req_t *r) {
    if (!authorized(r, false))
        return error(r, "403 Forbidden", "pairing",
                     "Einrichtung am Knob öffnen und mit seinem WLAN verbinden");
    wifi_scan_config_t sc = {.show_hidden = false};
    esp_err_t e = esp_wifi_scan_start(&sc, true);
    if (e != ESP_OK)
        return error(r, "503 Service Unavailable", "wifi_busy",
                     "WLAN verbindet gerade. Gleich erneut versuchen.");
    uint16_t count = 20;
    wifi_ap_record_t *records = calloc(count, sizeof *records);
    if (!records)
        return error(r, "503 Service Unavailable", "memory", "Bitte erneut versuchen");
    e = esp_wifi_scan_get_ap_records(&count, records);
    cJSON *j = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(j, "networks");
    bool complete = a != NULL;
    if (e == ESP_OK && complete)
        for (unsigned i = 0; i < count; i++) {
            if (!records[i].ssid[0])
                continue;
            bool duplicate = false;
            for (unsigned k = 0; k < i; k++)
                if (!strcmp((char *)records[k].ssid, (char *)records[i].ssid))
                    duplicate = true;
            if (duplicate)
                continue;
            cJSON *x = cJSON_CreateObject();
            if (!x || !cJSON_AddItemToArray(a, x)) {
                cJSON_Delete(x);
                complete = false;
                break;
            }
            complete = cJSON_AddStringToObject(x, "ssid", (char *)records[i].ssid) &&
                       cJSON_AddNumberToObject(x, "rssi", records[i].rssi) &&
                       cJSON_AddBoolToObject(x, "secure", records[i].authmode != WIFI_AUTH_OPEN);
            if (!complete)
                break;
        }
    free(records);
    if (!complete) {
        cJSON_Delete(j);
        return memory_error(r);
    }
    if (e != ESP_OK) {
        cJSON_Delete(j);
        return error(r, "503 Service Unavailable", "scan",
                     "WLAN-Suche nicht verfügbar. Bitte erneut versuchen.");
    }
    return json_response(r, j);
}
static esp_err_t wifi_handler(httpd_req_t *r) {
    if (!authorized(r, true))
        return error(r, "403 Forbidden", "pairing", "Geschützte Einrichtung erneut öffnen");
    cJSON *j = read_body(r, 512);
    if (!wifi_credentials_valid(j)) {
        delete_sensitive_json(j);
        return error(r, "400 Bad Request", "wifi", "WLAN-Name oder Passwort prüfen");
    }
    take();
    if (!revision_matches(r)) {
        give();
        delete_sensitive_json(j);
        return error(r, "409 Conflict", "revision",
                     "Einstellungen wurden geändert. Neu laden und erneut versuchen.");
    }
    memset(&pending_wifi, 0, sizeof pending_wifi);
    memcpy(pending_wifi.sta.ssid, text(j, "ssid"), strlen(text(j, "ssid")));
    strlcpy((char *)pending_wifi.sta.password, text(j, "password"),
            sizeof pending_wifi.sta.password);
    strlcpy(view.ssid, text(j, "ssid"), sizeof view.ssid);
    wifi_pending = true;
    view.connected = false;
    view.connecting = true;
    view.network_error[0] = 0;
    give();
    delete_sensitive_json(j);
    httpd_resp_set_status(r, "202 Accepted");
    headers(r);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, "{\"state\":\"connecting\"}", HTTPD_RESP_USE_STRLEN);
}
static esp_err_t settings_handler(httpd_req_t *r) {
    if (!authorized(r, true))
        return error(r, "403 Forbidden", "pairing", "Geschützte Einrichtung erneut öffnen");
    cJSON *patch = read_body(r, 2048);
    if (!patch || !pw_validate_settings_patch(patch)) {
        cJSON_Delete(patch);
        return error(r, "400 Bad Request", "settings", "Einstellungen prüfen");
    }
    take();
    if (!revision_matches(r)) {
        give();
        cJSON_Delete(patch);
        return error(r, "409 Conflict", "revision",
                     "Einstellungen wurden geändert. Neu laden und erneut speichern.");
    }
    if (view.revision == UINT32_MAX) {
        give();
        cJSON_Delete(patch);
        return error(r, "507 Insufficient Storage", "revision_limit",
                     "Die Konfiguration kann nicht weiter gespeichert werden. Bitte kontaktiere "
                     "den Support.");
    }
    cJSON *next = cJSON_Duplicate(config, true);
    cJSON *s = item(next, "settings");
    if (!next || !s || !prepare_config_copy(next)) {
        cJSON_Delete(next);
        give();
        cJSON_Delete(patch);
        return error(r, "503 Service Unavailable", "memory", "Bitte erneut versuchen");
    }
    bool location =
        item(patch, "latitude") || item(patch, "longitude") || item(patch, "weather_enabled");
    bool patch_ok = true;
    for (cJSON *v = patch->child; v; v = v->next) {
        cJSON *copy = cJSON_Duplicate(v, true);
        if (!copy || !cJSON_ReplaceItemInObjectCaseSensitive(s, v->string, copy)) {
            cJSON_Delete(copy);
            patch_ok = false;
            break;
        }
    }
    if (!patch_ok) {
        cJSON_Delete(next);
        give();
        cJSON_Delete(patch);
        return error(r, "503 Service Unavailable", "memory",
                     "Einstellungen nicht gespeichert. Erneut versuchen.");
    }
    if (!pw_validate_config(next)) {
        cJSON_Delete(next);
        give();
        cJSON_Delete(patch);
        return error(
            r, "400 Bad Request", "settings",
            "Einstellungen prüfen. Für Wetter sind beide Standortkoordinaten erforderlich.");
    }
    esp_err_t e = save_config(next);
    uint32_t rev = view.revision;
    if (e != ESP_OK)
        cJSON_Delete(next);
    give();
    cJSON_Delete(patch);
    if (e != ESP_OK)
        return error(r, "507 Insufficient Storage", "storage",
                     "Speichern konnte nicht bestätigt werden. Bitte lade den Gerätestand erneut.");
    apply_settings(location);
    return revision_response(r, rev);
}
static esp_err_t catalog_handler(httpd_req_t *r) {
    if (!authorized(r, true))
        return error(r, "403 Forbidden", "pairing", "Geschützte Einrichtung erneut öffnen");
    cJSON *j = read_body(r, CONFIG_MAX);
    if (!j || !pw_validate_catalog(j)) {
        cJSON_Delete(j);
        return error(
            r, "400 Bad Request", "catalog",
            "Liste prüfen: maximal 64 Einträge, kurze Namen, gültige Links und eindeutige IDs");
    }
    take();
    if (!revision_matches(r)) {
        give();
        cJSON_Delete(j);
        return error(r, "409 Conflict", "revision",
                     "Liste wurde geändert. Neu laden und erneut speichern.");
    }
    if (view.revision == UINT32_MAX) {
        give();
        cJSON_Delete(j);
        return error(r, "507 Insufficient Storage", "revision_limit",
                     "Die Konfiguration kann nicht weiter gespeichert werden. Bitte kontaktiere "
                     "den Support.");
    }
    cJSON *next = cJSON_Duplicate(config, true);
    if (!next || !prepare_config_copy(next)) {
        cJSON_Delete(next);
        give();
        cJSON_Delete(j);
        return error(r, "503 Service Unavailable", "memory", "Bitte erneut versuchen");
    }
    if (!cJSON_ReplaceItemInObjectCaseSensitive(next, "catalog", j)) {
        cJSON_Delete(next);
        cJSON_Delete(j);
        give();
        return error(r, "503 Service Unavailable", "memory",
                     "Liste nicht gespeichert. Bitte erneut versuchen.");
    }
    esp_err_t e = save_config(next);
    uint32_t rev = view.revision;
    if (e != ESP_OK)
        cJSON_Delete(next);
    give();
    if (e != ESP_OK)
        return error(r, "507 Insufficient Storage", "storage", "Speichern fehlgeschlagen");
    return revision_response(r, rev);
}
static int unhex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}
static bool decode_query(const char *encoded, char *out, size_t cap) {
    size_t at = 0;
    for (size_t i = 0; encoded[i]; i++) {
        unsigned char c = (unsigned char)encoded[i];
        if (c == '%') {
            if (!encoded[i + 1] || !encoded[i + 2])
                return false;
            int a = unhex(encoded[++i]), b = unhex(encoded[++i]);
            if (a < 0 || b < 0)
                return false;
            c = (unsigned char)((a << 4) | b);
        } else if (c == '+')
            c = ' ';
        if (c < 32 || at + 1 >= cap)
            return false;
        out[at++] = (char)c;
    }
    out[at] = 0;
    return at > 0;
}
static void encode_query(const char *value, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    while (*value) {
        unsigned char c = (unsigned char)*value++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            *out++ = (char)c;
        else {
            *out++ = '%';
            *out++ = hex[c >> 4];
            *out++ = hex[c & 15];
        }
    }
    *out = 0;
}
static cJSON *radio_fetch(const char *host, const char *encoded) {
    char url[512];
    snprintf(url, sizeof url,
             "https://%s/json/stations/"
             "search?name=%s&countrycode=DE&hidebroken=true&is_https=true&limit=20",
             host, encoded);
    esp_http_client_config_t options = {
        .url = url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .buffer_size = 2048,
        .user_agent = "PassionWaveRotarySpotify/0.1.0 "
                      "(https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify)"};
    esp_http_client_handle_t client = esp_http_client_init(&options);
    if (!client)
        return NULL;
    cJSON *result = NULL;
    char *body = NULL;
    if (esp_http_client_open(client, 0) != ESP_OK)
        goto done;
    int64_t size = esp_http_client_fetch_headers(client);
    if (size > 65536 || esp_http_client_get_status_code(client) != 200)
        goto done;
    body = malloc(65537);
    if (!body)
        goto done;
    size_t used = 0;
    int64_t started = esp_timer_get_time();
    while (used < 65536 && esp_timer_get_time() - started < 10000000) {
        int n = esp_http_client_read(client, body + used, 65536 - used);
        if (n < 0)
            goto done;
        if (!n) {
            if (!esp_http_client_is_complete_data_received(client))
                goto done;
            break;
        }
        used += (size_t)n;
    }
    if (used >= 65536 || !esp_http_client_is_complete_data_received(client))
        goto done;
    body[used] = 0;
    result = pw_parse_json(body, used + 1);
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(result);
        result = NULL;
    }
done:
    free(body);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}
static esp_err_t radio_handler(httpd_req_t *r) {
    if (!authorized(r, false))
        return error(r, "403 Forbidden", "pairing", "Geschützte Einrichtung erneut öffnen");
    pw_app_view_t v;
    pw_app_get_view(&v);
    if (!v.connected)
        return error(r, "503 Service Unavailable", "offline",
                     "Für die Sendersuche zuerst mit dem Heim-WLAN verbinden");
    char query[240], encoded[200], value[65], escaped[200];
    if (httpd_req_get_url_query_str(r, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, "q", encoded, sizeof encoded) != ESP_OK ||
        !decode_query(encoded, value, sizeof value))
        return error(r, "400 Bad Request", "query", "Bitte einen Sendernamen eingeben");
    encode_query(value, escaped);
    cJSON *remote = radio_fetch("de1.api.radio-browser.info", escaped);
    if (!remote)
        remote = radio_fetch("nl1.api.radio-browser.info", escaped);
    if (!remote)
        return error(
            r, "503 Service Unavailable", "directory",
            "Senderverzeichnis gerade nicht erreichbar. Eigene URL kann gespeichert werden.");
    cJSON *j = cJSON_CreateObject(), *stations = cJSON_AddArrayToObject(j, "stations");
    bool complete = stations != NULL;
    unsigned count = 0;
    for (cJSON *x = remote->child; complete && x && count < 20; x = x->next) {
        const char *url = text(x, "url_resolved");
        if (!*url)
            url = text(x, "url");
        const char *name = text(x, "name"), *id = text(x, "stationuuid");
        if (!clean_text(name, 1, 80) || !clean_text(id, 1, 64) || !pw_public_radio_url(url))
            continue;
        cJSON *station = cJSON_CreateObject();
        if (!station || !cJSON_AddItemToArray(stations, station)) {
            cJSON_Delete(station);
            complete = false;
            break;
        }
        complete = cJSON_AddStringToObject(station, "id", id) &&
                   cJSON_AddStringToObject(station, "name", name) &&
                   cJSON_AddStringToObject(station, "url", url) &&
                   cJSON_AddStringToObject(station, "codec", text(x, "codec")) &&
                   cJSON_AddBoolToObject(station, "enabled", true);
        count++;
    }
    cJSON_Delete(remote);
    if (!complete) {
        cJSON_Delete(j);
        return memory_error(r);
    }
    return json_response(r, j);
}
static cJSON *update_json(void) {
    pw_update_service_view_t update;
    pw_update_service_get_view(&update);
    char id[33];
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; i++) {
        id[i * 2] = hex[update.job.transaction[i] >> 4];
        id[i * 2 + 1] = hex[update.job.transaction[i] & 15];
    }
    id[32] = 0;
    cJSON *j = cJSON_CreateObject();
    bool ok = j &&
              cJSON_AddStringToObject(j, "state", pw_update_stage_phase_name(update.job.phase)) &&
              cJSON_AddStringToObject(j, "reason", update.reason) &&
              cJSON_AddStringToObject(j, "error", pw_update_result_name(update.job.error)) &&
              cJSON_AddStringToObject(j, "job_id", id) &&
              cJSON_AddStringToObject(j, "version", update.job.version) &&
              cJSON_AddBoolToObject(j, "upload_enabled", update.upload_enabled) &&
              cJSON_AddBoolToObject(j, "activation_enabled", update.activation_enabled) &&
              cJSON_AddBoolToObject(j, "busy", update.busy) &&
              cJSON_AddNumberToObject(j, "received", update.job.received) &&
              cJSON_AddNumberToObject(j, "total", update.job.total) &&
              cJSON_AddNumberToObject(j, "peer_received", update.job.peer_received) &&
              cJSON_AddNumberToObject(j, "peer_total", update.job.image_bytes[0]);
    if (!ok) {
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}
static esp_err_t updates_handler(httpd_req_t *r) {
    if (!authorized(r, false))
        return error(r, "403 Forbidden", "pairing", "Geschützte Einrichtung erneut öffnen");
    return json_response(r, update_json());
}
static int update_read(void *context, uint8_t *buffer, size_t count) {
    return httpd_req_recv((httpd_req_t *)context, (char *)buffer, count);
}
static void update_reply(void *context, bool accepted, const char *reason) {
    httpd_req_t *request = context;
    if (accepted) {
        httpd_resp_set_status(request, "202 Accepted");
        headers(request);
        httpd_resp_set_type(request, "application/json");
        httpd_resp_send(request, "{\"state\":\"local_ready\",\"status_url\":\"/api/v1/updates\"}",
                        HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_hdr(request, "Connection", "close");
        error(request, "400 Bad Request", reason ? reason : "update",
              "Updatepaket konnte nicht vorbereitet werden. Den Gerätestatus erneut laden.");
    }
    httpd_req_async_handler_complete(request);
}
static esp_err_t update_upload_handler(httpd_req_t *r) {
    if (!authorized(r, true))
        return error(r, "403 Forbidden", "pairing", "Geschützte Einrichtung erneut öffnen");
    take();
    bool current = revision_matches(r);
    give();
    if (!current)
        return error(r, "409 Conflict", "revision",
                     "Einstellungen wurden geändert. Neu laden und erneut versuchen.");
    char type[80] = {0}, power[24] = {0};
    if (!r->content_len || r->content_len > 16u * 1024u * 1024u ||
        httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof type) != ESP_OK ||
        strcmp(type, "application/octet-stream") ||
        httpd_req_get_hdr_value_str(r, "X-PW-USB-Power", power, sizeof power) != ESP_OK ||
        strcmp(power, "confirmed"))
        return error(r, "400 Bad Request", "update_upload",
                     "Signiertes .pwota-Paket wählen und USB-Stromversorgung bestätigen.");
    httpd_req_t *async = NULL;
    esp_err_t result = httpd_req_async_handler_begin(r, &async);
    if (result != ESP_OK)
        return memory_error(r);
    result =
        pw_update_service_upload((uint32_t)r->content_len, true, update_read, update_reply, async);
    if (result != ESP_OK) {
        httpd_resp_set_hdr(async, "Connection", "close");
        error(async, "409 Conflict", "update_unavailable",
              "Update nicht freigegeben, Gerät beschäftigt oder Begleitprozessor nicht bereit.");
        httpd_req_async_handler_complete(async);
    }
    return ESP_OK;
}
static esp_err_t asset_handler(httpd_req_t *r) {
    if (!host_valid(r))
        return error(r, "400 Bad Request", "host", "Geräteadresse direkt öffnen");
    headers(r);
    const uint8_t *start = page_start, *end = page_end;
    const char *type = "text/html; charset=utf-8";
    if (!strcmp(r->uri, "/app.js")) {
        start = js_start;
        end = js_end;
        type = "text/javascript; charset=utf-8";
    } else if (!strcmp(r->uri, "/style.css")) {
        start = css_start;
        end = css_end;
        type = "text/css; charset=utf-8";
    } else if (!strcmp(r->uri, "/places-de.json")) {
        httpd_resp_set_type(r, "application/json; charset=utf-8");
        httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
        return httpd_resp_send(r, (const char *)places_start, places_end - places_start);
    }
    httpd_resp_set_type(r, type);
    return httpd_resp_send(r, (const char *)start, end - start - 1);
}
static void register_uri(const char *uri, httpd_method_t method,
                         esp_err_t (*handler)(httpd_req_t *)) {
    httpd_uri_t route = {.uri = uri, .method = method, .handler = handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
}
static void service_task(void *arg) {
    (void)arg;
    pw_parser_t parser = {0};
    pw_frame_t in, out = {.role = PW_ROLE_S3, .kind = PW_MSG_HELLO};
    do {
        out.session = esp_random();
    } while (!out.session);
    pw_update_service_transport_session(out.session);
    uint8_t wire[PW_FRAME_MAX], rx[256];
    int64_t heartbeat = 0;
    while (1) {
        int64_t now = esp_timer_get_time();
        wifi_config_t wifi = {0};
        take();
        bool change = wifi_pending;
        if (change) {
            wifi = pending_wifi;
            mbedtls_platform_zeroize(&pending_wifi, sizeof pending_wifi);
            wifi_pending = false;
        }
        bool expire = view.setup_open && setup_until < now;
        bool retry = retry_at && now >= retry_at;
        bool dirty = brightness_dirty && now - brightness_changed > 1500000;
        if (retry) {
            retry_at = 0;
            view.connecting = true;
        }
        if (dirty) {
            cJSON *next = cJSON_Duplicate(config, true);
            if (!next || !prepare_config_copy(next) || save_config(next) != ESP_OK) {
                cJSON_Delete(next);
                /* Rate-limit persistence retries; the physical setting remains usable. */
                brightness_changed = now + 8500000;
            }
        }
        give();
        if (expire)
            pw_app_close_setup();
        if (change) {
            take();
            candidate_active = false;
            persist_wifi = false;
            give();
            esp_wifi_disconnect();
            esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wifi);
            if (e == ESP_OK) {
                take();
                candidate_wifi = wifi;
                candidate_active = true;
                give();
                e = esp_wifi_connect();
            }
            take();
            view.connecting = e == ESP_OK;
            retry_at = 0;
            if (e != ESP_OK) {
                candidate_active = false;
                mbedtls_platform_zeroize(&candidate_wifi, sizeof candidate_wifi);
                strlcpy(view.network_error, "WLAN-Verbindung erneut versuchen",
                        sizeof view.network_error);
            }
            give();
            mbedtls_platform_zeroize(&wifi, sizeof wifi);
        } else if (retry)
            esp_wifi_connect();
        take();
        bool persist = persist_wifi && candidate_active;
        give();
        if (persist) {
            wifi_ap_record_t associated = {0};
            esp_err_t association = esp_wifi_sta_get_ap_info(&associated);
            take();
            if (association == ESP_OK && candidate_active &&
                !memcmp(associated.ssid, candidate_wifi.sta.ssid, 32)) {
                char ssid[33] = {0};
                memcpy(ssid, candidate_wifi.sta.ssid, 32);
                cJSON *credential = cJSON_CreateObject();
                bool complete = credential && cJSON_AddStringToObject(credential, "ssid", ssid) &&
                                cJSON_AddStringToObject(credential, "password",
                                                        (char *)candidate_wifi.sta.password);
                char *blob = complete && wifi_credentials_valid(credential)
                                 ? cJSON_PrintUnformatted(credential)
                                 : NULL;
                esp_err_t e = blob ? nvs_set_str(store, "wifi", blob) : ESP_ERR_NO_MEM;
                if (e == ESP_OK)
                    e = nvs_commit(store);
                if (blob) {
                    mbedtls_platform_zeroize(blob, strlen(blob));
                    free(blob);
                }
                delete_sensitive_json(credential);
                if (e != ESP_OK)
                    strlcpy(view.network_error, "WLAN konnte nicht gespeichert werden",
                            sizeof view.network_error);
                else {
                    candidate_active = false;
                    mbedtls_platform_zeroize(&candidate_wifi, sizeof candidate_wifi);
                }
                persist_wifi = false;
            }
            give();
        }
        int got = uart_read_bytes(UART_NUM_1, rx, sizeof rx, 0);
        for (int i = 0; i < got; i++) {
            if (!pw_parser_feed(&parser, rx[i], &in) || in.role != PW_ROLE_COMPANION)
                continue;
            pw_update_service_receive(&in);
            if (in.kind == PW_MSG_HEARTBEAT || in.kind == PW_MSG_HEALTH) {
                take();
                peer_seen = now;
                give();
            }
        }
        pw_frame_t update_frame;
        if (pw_update_service_take_frame(&update_frame)) {
            size_t n = pw_frame_encode(&update_frame, wire, sizeof wire);
            if (n)
                uart_write_bytes(UART_NUM_1, wire, n);
        }
        if (now - heartbeat > 2000000) {
            out.kind = PW_MSG_HELLO;
            out.length = 4;
            memcpy(out.payload, &out.session, 4);
            out.sequence++;
            size_t n = pw_frame_encode(&out, wire, sizeof wire);
            uart_write_bytes(UART_NUM_1, wire, n);
            heartbeat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
esp_err_t pw_app_init(void) {
    lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;
    esp_err_t e = pw_storage_init();
    if (e != ESP_OK)
        return e;
    ESP_ERROR_CHECK(nvs_open_from_partition("settings", "pw", NVS_READWRITE, &store));
    /* Separate blob key preserves the old string until an explicit successful save.
     * nvs_find_key distinguishes a missing key from the wrong stored type. */
    nvs_type_t stored_type;
    bool legacy_config = false;
    const char *config_key = CONFIG_BLOB_KEY;
    e = nvs_find_key(store, CONFIG_BLOB_KEY, &stored_type);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        config_key = "config";
        legacy_config = true;
        e = nvs_find_key(store, config_key, &stored_type);
    }
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        config = default_config();
        if (!config)
            return ESP_ERR_NO_MEM;
    } else {
        if (e != ESP_OK)
            return e;
        if (stored_type != (legacy_config ? NVS_TYPE_STR : NVS_TYPE_BLOB))
            return ESP_ERR_NVS_TYPE_MISMATCH;
        size_t len = 0;
        e = legacy_config ? nvs_get_str(store, config_key, NULL, &len)
                          : nvs_get_blob(store, config_key, NULL, &len);
        if (e != ESP_OK)
            return e;
        if (len < 3 || len > CONFIG_MAX)
            return ESP_ERR_INVALID_SIZE;
        char *raw = malloc(len);
        if (!raw)
            return ESP_ERR_NO_MEM;
        size_t actual = len;
        e = legacy_config ? nvs_get_str(store, config_key, raw, &actual)
                          : nvs_get_blob(store, config_key, raw, &actual);
        if (e == ESP_OK && actual == len)
            config = pw_parse_json(raw, actual);
        free(raw);
        if (e != ESP_OK)
            return e;
        if (actual != len)
            return ESP_ERR_INVALID_SIZE;
        /* A parse OOM is indistinguishable here from invalid JSON. Neither may
         * replace durable data with defaults or open a writable recovery AP. */
        if (!config || !pw_validate_config(config)) {
            cJSON_Delete(config);
            config = NULL;
            return ESP_ERR_INVALID_STATE;
        }
        if (legacy_config)
            ESP_LOGI(TAG, "Legacy configuration loaded; next explicit save uses blob storage");
    }
    cJSON *s = item(config, "settings");
    strlcpy(view.name, text(s, "name"), sizeof view.name);
    view.brightness = (uint8_t)number(s, "brightness", 65);
    view.haptic = flag(s, "haptic");
    view.weather_enabled = flag(s, "weather_enabled");
    view.avatar_enabled = flag(s, "avatar_enabled");
    view.avatar_blond = flag(s, "avatar_blond");
    strlcpy(view.screensaver_mode, text(s, "screensaver_mode"), sizeof view.screensaver_mode);
    view.revision = (uint32_t)number(config, "revision", 1);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    station_if = esp_netif_create_default_wifi_sta();
    ap_if = esp_netif_create_default_wifi_ap();
    if (!station_if || !ap_if)
        return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    snprintf(view.setup_ssid, sizeof view.setup_ssid, "PassionWave-%02X%02X%02X", mac[3], mac[4],
             mac[5]);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    char credentials[512] = {0};
    size_t credential_size = sizeof credentials;
    bool saved = false;
    cJSON *wifi_saved = NULL;
    e = nvs_find_key(store, "wifi", &stored_type);
    if (e != ESP_ERR_NVS_NOT_FOUND) {
        if (e != ESP_OK)
            return e;
        if (stored_type != NVS_TYPE_STR)
            return ESP_ERR_NVS_TYPE_MISMATCH;
        e = nvs_get_str(store, "wifi", credentials, &credential_size);
        if (e == ESP_OK && credential_size >= 3 && credential_size <= sizeof credentials)
            wifi_saved = pw_parse_json(credentials, credential_size);
        mbedtls_platform_zeroize(credentials, sizeof credentials);
        if (e != ESP_OK)
            return e;
        if (!wifi_credentials_valid(wifi_saved)) {
            delete_sensitive_json(wifi_saved);
            return ESP_ERR_INVALID_STATE;
        }
        take();
        memcpy(pending_wifi.sta.ssid, text(wifi_saved, "ssid"), strlen(text(wifi_saved, "ssid")));
        strlcpy((char *)pending_wifi.sta.password, text(wifi_saved, "password"),
                sizeof pending_wifi.sta.password);
        strlcpy(view.ssid, text(wifi_saved, "ssid"), sizeof view.ssid);
        wifi_pending = true;
        view.connecting = true;
        give();
        saved = true;
    }
    delete_sensitive_json(wifi_saved);
    mbedtls_platform_zeroize(credentials, sizeof credentials);
    if (!saved)
        ESP_ERROR_CHECK(pw_app_open_setup());
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_ERROR_CHECK(pw_weather_init());
    ESP_ERROR_CHECK(pw_update_service_init());
    apply_settings(true);
    const uart_config_t uart = {.baud_rate = 2000000,
                                .data_bits = UART_DATA_8_BITS,
                                .parity = UART_PARITY_DISABLE,
                                .stop_bits = UART_STOP_BITS_1,
                                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                                .source_clk = UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 38, 48, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 2048, 0, 0, NULL, 0));
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 18;
    hc.stack_size = 12288;
    hc.max_open_sockets = 4;
    hc.lru_purge_enable = true;
    hc.recv_wait_timeout = 5;
    hc.send_wait_timeout = 5;
    ESP_ERROR_CHECK(httpd_start(&server, &hc));
    register_uri("/", HTTP_GET, asset_handler);
    register_uri("/app.js", HTTP_GET, asset_handler);
    register_uri("/style.css", HTTP_GET, asset_handler);
    register_uri("/places-de.json", HTTP_GET, asset_handler);
    register_uri("/api/v1/session", HTTP_GET, session_handler);
    register_uri("/api/v1/status", HTTP_GET, status_handler);
    register_uri("/api/v1/wifi/scan", HTTP_GET, scan_handler);
    register_uri("/api/v1/wifi", HTTP_POST, wifi_handler);
    register_uri("/api/v1/settings", HTTP_PATCH, settings_handler);
    register_uri("/api/v1/catalog", HTTP_PUT, catalog_handler);
    register_uri("/api/v1/radio/search", HTTP_GET, radio_handler);
    register_uri("/api/v1/updates", HTTP_GET, updates_handler);
    register_uri("/api/v1/updates/upload", HTTP_POST, update_upload_handler);
    if (xTaskCreate(service_task, "pw_service", 6144, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Local services ready; protected AP writes only, LAN trust not qualified");
    return ESP_OK;
}
