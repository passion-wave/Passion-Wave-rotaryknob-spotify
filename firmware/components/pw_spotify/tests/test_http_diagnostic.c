#include "http_parser.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
size_t __real_http_parser_execute(http_parser *, const http_parser_settings *, const char *, size_t);
size_t __wrap_http_parser_execute(http_parser *, const http_parser_settings *, const char *, size_t);
static unsigned logs;
static char message[128];
void pw_test_logi(const char *tag, const char *format, ...) {
    assert(strcmp(tag, "pw_http_diag") == 0);
    va_list args; va_start(args, format); vsnprintf(message, sizeof(message), format, args); va_end(args);
    logs++;
}
static void check(const char *data, enum http_parser_type type, int error) {
    http_parser baseline = {0}, observed = {0};
    http_parser_settings settings = {0};
    http_parser_init(&baseline, type); http_parser_init(&observed, type);
    logs = 0;
    /* Fragmentation reproduces transport reads and confirms exact parser behaviour. */
    for (size_t i = 0; i < strlen(data); i++) {
        size_t expected = __real_http_parser_execute(&baseline, &settings, data + i, 1);
        size_t actual = __wrap_http_parser_execute(&observed, &settings, data + i, 1);
        assert(expected == actual);
        assert(memcmp(&baseline, &observed, sizeof(baseline)) == 0);
    }
    assert((HTTP_PARSER_ERRNO(&observed) != HPE_OK) == error);
    assert(logs == (unsigned)(error && type == HTTP_RESPONSE));
    if (logs) assert(strstr(message, "parser_error code=") == message);
}
int main(void) {
    check("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", HTTP_RESPONSE, 0);
    check("HTTP/1.1 204 No Content\r\n\r\n", HTTP_RESPONSE, 0);
    check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n", HTTP_RESPONSE, 0);
    check("HTTP/1.1 200 OK\r\nBad Header: secret\r\n\r\n", HTTP_RESPONSE, 1);
    check("HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n", HTTP_RESPONSE, 1);
    check("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}", HTTP_RESPONSE, 1);
    check("GET / HTTP/1.1\r\nBad Header: secret\r\n\r\n", HTTP_REQUEST, 1);
    puts("HTTP diagnostic: 7 real-parser cases, exact fragmented-state/return parity");
}
