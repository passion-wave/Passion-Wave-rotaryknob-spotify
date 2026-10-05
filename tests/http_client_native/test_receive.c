// SPDX-License-Identifier: MIT
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "http_parser.h"
#define ESP_FAIL (-1)
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT (-3)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
enum {HTTP_STATE_REQ_COMPLETE_HEADER, HTTP_STATE_REQ_COMPLETE_DATA, HTTP_STATE_RES_COMPLETE_HEADER, HTTP_STATE_RES_ON_DATA_START};
typedef struct { char data[2048]; int len; } esp_http_buffer_t;
typedef struct { int status_code; int64_t content_length; bool is_chunked; esp_http_buffer_t *buffer; } response_t;
typedef struct { int state, buffer_size_rx, timeout_ms; void *transport; response_t *response; http_parser *parser; http_parser_settings *parser_settings; } client_t;
typedef client_t *esp_http_client_handle_t;
static const char *parts[4]; static unsigned cursor;
static int esp_transport_read(void *transport, char *data, int size, int timeout) {
    (void)transport; assert(timeout==6000); assert(cursor<4);
    const char *part=parts[cursor++]; if(!part) return ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT;
    assert(strlen(part)<(size_t)size); memcpy(data,part,strlen(part)); return (int)strlen(part);
}
static int complete(http_parser *p) {
    client_t *c=p->data; c->response->status_code=p->status_code;
    c->response->content_length=(int64_t)p->content_length;
    c->state=HTTP_STATE_RES_COMPLETE_HEADER; return 0;
}
#include "receiver.inc"
static void check(bool fragmented) {
    http_parser parser={0}; http_parser_init(&parser,HTTP_RESPONSE);
    http_parser_settings settings={.on_headers_complete=complete};
    esp_http_buffer_t buffer={0};response_t response={.buffer=&buffer};
    client_t c={.state=HTTP_STATE_REQ_COMPLETE_HEADER,.buffer_size_rx=2048,.timeout_ms=6000,.response=&response,.parser=&parser,.parser_settings=&settings}; parser.data=&c;
    cursor=0;
    if(fragmented) { parts[0]="HTTP/1.1 200 OK\r\nContent-Le";parts[1]=NULL;parts[2]="ngth: 2\r\n\r\n{}"; }
    else { parts[0]=NULL;parts[1]="HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}"; }
    assert(esp_http_client_fetch_headers(&c)==-ESP_ERR_HTTP_EAGAIN);
    assert(esp_http_client_fetch_headers(&c)==2);
    assert(c.state==HTTP_STATE_RES_ON_DATA_START && response.status_code==200);
    assert(HTTP_PARSER_ERRNO(&parser)==HPE_OK);
}
int main(void) { check(false);check(true);puts("Real IDF token header receiver: resume after timeout before/inside headers PASS"); }
