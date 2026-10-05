/* Lab-only, read-only parser error evidence. Never retain response data. */
#include "http_parser.h"
#include "esp_log.h"

size_t __real_http_parser_execute(http_parser *, const http_parser_settings *,
                                  const char *, size_t);
size_t __wrap_http_parser_execute(http_parser *parser, const http_parser_settings *settings,
                                  const char *data, size_t length) {
    const enum http_errno before = HTTP_PARSER_ERRNO(parser);
    const size_t consumed = __real_http_parser_execute(parser, settings, data, length);
    const enum http_errno after = HTTP_PARSER_ERRNO(parser);
    if (parser->type == HTTP_RESPONSE && before == HPE_OK && after != HPE_OK && after != HPE_PAUSED) {
        const unsigned code = (unsigned)after;
        const unsigned status = parser->status_code;
        ESP_LOGI("pw_http_diag", "parser_error code=%u status=%u",
                 code <= 127 ? code : 0, status >= 100 && status <= 599 ? status : 0);
    }
    return consumed;
}
