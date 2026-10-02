// SPDX-License-Identifier: MIT
// Include current production functions; simulate only IDF boundaries and send().
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// IDF's response implementation uses 32-bit ssize_t in its %d formatter.
// Keep the target ABI for that type when running on a 64-bit host.
#define ssize_t int32_t

typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = -2,
       ESP_ERR_HTTPD_INVALID_REQ = -3, ESP_ERR_HTTPD_RESP_HDR = -4,
       ESP_ERR_HTTPD_RESP_SEND = -5, HTTP_SERVER_EVENT_HEADERS_SENT,
       HTTP_SERVER_EVENT_SENT_DATA };
#define HTTPD_RESP_USE_STRLEN (-1)
#define LOG_FMT(value) value
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
struct session { void *handle; int fd; int (*send_fn)(void *, int, const char *, size_t, int); };
struct httpd_req_aux {
    struct session *sd;
    char scratch[1024];
    const char *status, *content_type;
    unsigned req_hdrs_count, resp_hdrs_count;
    struct { const char *field, *value; } resp_hdrs[8];
};
typedef struct { struct httpd_req_aux *aux; const char *uri; } httpd_req_t;
typedef struct { int fd; ssize_t data_len; } esp_http_server_event_data;
static const uint8_t *page_start, *page_end, *js_start, *js_end, *css_start, *css_end;
static const uint8_t *places_start, *places_end;
static unsigned checks, scenarios, events, nodelay_calls;
static size_t limit, fail_after, wire_length, wire_capacity, send_calls;
static unsigned char *wire;
static bool valid_host = true;
#define CHECK(expression) do { checks++; if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); exit(1); \
} } while (0)

static int send_sink(void *handle, int fd, const char *data, size_t size, int flags) {
    (void)handle; (void)fd; (void)flags;
    send_calls++;
    if (wire_length >= fail_after)
        return -1;
    if (limit && size > limit)
        size = limit;
    if (size > fail_after - wire_length)
        size = fail_after - wire_length;
    CHECK(wire_length + size < wire_capacity);
    memcpy(wire + wire_length, data, size);
    wire_length += size;
    wire[wire_length] = 0;
    return (int)size;
}
static bool httpd_valid_req(httpd_req_t *r) { return r && r->aux; }
static void esp_http_server_dispatch_event(int event, const void *data, size_t length) {
    (void)data; (void)length;
    if (event == HTTP_SERVER_EVENT_SENT_DATA)
        events++;
}
static bool host_valid(httpd_req_t *r) { (void)r; return valid_host; }
static int httpd_req_to_sockfd(httpd_req_t *r) { return r->aux->sd->fd; }
static bool pw_http_socket_low_latency(int fd) { (void)fd; nodelay_calls++; return true; }
static esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value) {
    CHECK(r->aux->resp_hdrs_count < 8);
    unsigned i = r->aux->resp_hdrs_count++;
    r->aux->resp_hdrs[i].field = field;
    r->aux->resp_hdrs[i].value = value;
    return ESP_OK;
}
static esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
    r->aux->content_type = type;
    return ESP_OK;
}
static esp_err_t error(httpd_req_t *r, const char *status, const char *code, const char *message) {
    (void)r; (void)status; (void)code; (void)message;
    return ESP_FAIL;
}
// Logging arguments are evaluated so the production function remains warning-clean.
static void log_asset(const char *format, const char *name, unsigned size,
                      long long duration, const char *result) {
    (void)format; (void)name; (void)size; (void)duration; (void)result;
}
#undef ESP_LOGI
#define ESP_LOGI(tag, ...) log_asset(__VA_ARGS__)
static int64_t esp_timer_get_time(void) { return 0; }
static const char *esp_err_to_name(esp_err_t error_code) { return error_code ? "FAIL" : "OK"; }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "production.inc"
#pragma GCC diagnostic pop

static unsigned char *read_asset(const char *directory, const char *name, size_t *length) {
    char path[2048];
    CHECK(snprintf(path, sizeof path, "%s/%s.gz", directory, name) < (int)sizeof path);
    FILE *input = fopen(path, "rb");
    CHECK(input != NULL);
    CHECK(fseek(input, 0, SEEK_END) == 0);
    long size = ftell(input);
    CHECK(size > 0);
    rewind(input);
    unsigned char *bytes = malloc((size_t)size);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1, (size_t)size, input) == (size_t)size);
    CHECK(fclose(input) == 0);
    *length = (size_t)size;
    return bytes;
}
static esp_err_t run_response(const char *uri, size_t partial, size_t stop) {
    scenarios++;
    wire_length = send_calls = events = nodelay_calls = 0;
    limit = partial;
    fail_after = stop;
    struct session session = {.fd = 123, .send_fn = send_sink};
    struct httpd_req_aux aux = {.sd = &session, .status = "200 OK", .req_hdrs_count = 4};
    httpd_req_t request = {.aux = &aux, .uri = uri};
    esp_err_t result = asset_handler(&request);
    CHECK(aux.req_hdrs_count == (valid_host ? 0 : 4));
    return result;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    const char *names[] = {"index.html", "style.css", "app.js", "places-de.json"};
    const char *uris[] = {"/", "/style.css", "/app.js", "/places-de.json"};
    const char *types[] = {"text/html", "text/css", "text/javascript", "application/json"};
    unsigned char *data[4];
    size_t sizes[4];
    for (unsigned i = 0; i < 4; i++)
        data[i] = read_asset(argv[1], names[i], &sizes[i]);
    page_start = data[0]; page_end = page_start + sizes[0];
    css_start = data[1]; css_end = css_start + sizes[1];
    js_start = data[2]; js_end = js_start + sizes[2];
    places_start = data[3]; places_end = places_start + sizes[3];
    for (unsigned i = 0; i < 4; i++)
        if (sizes[i] + 4096 > wire_capacity)
            wire_capacity = sizes[i] + 4096;
    wire = malloc(wire_capacity);
    CHECK(wire != NULL);
    const size_t partial[] = {0, 1, 37, 1440, 5760};
    for (unsigned i = 0; i < 4; i++) {
        for (unsigned j = 0; j < sizeof partial / sizeof partial[0]; j++) {
            CHECK(run_response(uris[i], partial[j], SIZE_MAX) == ESP_OK);
            CHECK(events == 1 && nodelay_calls == 1);
            const char *body = strstr((const char *)wire, "\r\n\r\n");
            CHECK(body != NULL);
            size_t offset = (size_t)(body + 4 - (const char *)wire);
            CHECK(wire_length - offset == sizes[i]);
            CHECK(memcmp(wire + offset, data[i], sizes[i]) == 0);
            CHECK(strstr((const char *)wire, types[i]) != NULL);
            if (!partial[j])
                CHECK(send_calls == 23); // IDF splits every extra header into four sends.
            char path[2048];
            CHECK(snprintf(path, sizeof path, "%s/wire-%u--%s--%u.http", argv[1], i,
                           names[i], j) < (int)sizeof path);
            FILE *output = fopen(path, "wb");
            CHECK(output != NULL);
            CHECK(fwrite(wire, 1, wire_length, output) == wire_length);
            CHECK(fclose(output) == 0);
        }
    }
    const size_t stops[] = {0, 50, 5760, 10000};
    for (unsigned i = 0; i < sizeof stops / sizeof stops[0]; i++) {
        CHECK(run_response("/app.js", 37, stops[i]) == ESP_ERR_HTTPD_RESP_SEND);
        CHECK(wire_length == stops[i]);
        CHECK(events == 0); // A partial/error response is never reported complete.
    }
    valid_host = false;
    CHECK(run_response("/app.js", 0, SIZE_MAX) == ESP_FAIL);
    CHECK(send_calls == 0 && wire_length == 0 && events == 0);
    for (unsigned i = 0; i < 4; i++)
        free(data[i]);
    free(wire);
    CHECK(scenarios == 25);
    printf("%u response scenarios passed (real handler + IDF writer; simulated send sink).\n", scenarios);
    return 0;
}
