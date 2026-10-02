// SPDX-License-Identifier: MIT
#include "pw_setup_protocol.h"
#include "pw_validation.h"
#include <math.h>
#include <string.h>

static bool bounded_ascii(const cJSON *item, size_t capacity) {
    if (!cJSON_IsString(item) || !item->valuestring)
        return false;
    size_t n = strlen(item->valuestring);
    if (!n || n >= capacity)
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)item->valuestring[i];
        if (c < 0x21 || c > 0x7e)
            return false;
    }
    return true;
}

bool pw_setup_decode(const char *line, size_t size, pw_setup_request_t *out) {
    if (!out)
        return false;
    memset(out, 0, sizeof *out);
    const size_t prefix = sizeof(PW_SETUP_PREFIX) - 1;
    if (!line || size <= prefix || size >= PW_SETUP_LINE_BYTES ||
        memcmp(line, PW_SETUP_PREFIX, prefix) || memchr(line, '\0', size) ||
        memchr(line, '\r', size) || memchr(line, '\n', size))
        return false;
    /* The framing owner reserves and supplies a terminating NUL. Do not read it:
     * make a length-bounded copy for the existing strict JSON parser. */
    char *json = cJSON_malloc(size - prefix + 1);
    if (!json)
        return false;
    memcpy(json, line + prefix, size - prefix);
    json[size - prefix] = 0;
    cJSON *root = pw_parse_json(json, size - prefix + 1);
    /* Clear the callback authorization code before freeing temporary JSON. */
    volatile char *wipe = json;
    for (size_t i = 0; i < size - prefix + 1; i++)
        wipe[i] = 0;
    cJSON_free(json);
    if (!root)
        return false;
    const char *const basic[] = {"id", "method"};
    const char *const callback[] = {"id", "method", "code", "state"};
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    bool ok = cJSON_IsNumber(id) && isfinite(id->valuedouble) && id->valuedouble >= 1 &&
              id->valuedouble <= 2147483647 && floor(id->valuedouble) == id->valuedouble &&
              bounded_ascii(method, 16);
    pw_setup_method_t decoded = PW_SETUP_HELLO;
    if (ok) {
        if (!strcmp(method->valuestring, "hello"))
            decoded = PW_SETUP_HELLO;
        else if (!strcmp(method->valuestring, "status"))
            decoded = PW_SETUP_STATUS;
        else if (!strcmp(method->valuestring, "authorize"))
            decoded = PW_SETUP_AUTHORIZE;
        else if (!strcmp(method->valuestring, "callback"))
            decoded = PW_SETUP_CALLBACK;
        else if (!strcmp(method->valuestring, "cancel"))
            decoded = PW_SETUP_CANCEL;
        else
            ok = false;
    }
    if (ok && decoded == PW_SETUP_CALLBACK) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
        ok = pw_keys_only(root, callback, 4) && bounded_ascii(code, PW_SETUP_CODE_BYTES) &&
             bounded_ascii(state, PW_SETUP_STATE_BYTES) &&
             strlen(state->valuestring) == PW_SETUP_STATE_BYTES - 1;
        if (ok) {
            for (size_t i = 0; i < PW_SETUP_STATE_BYTES - 1; i++) {
                unsigned char c = (unsigned char)state->valuestring[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    ok = false;
            }
        }
        if (ok) {
            memcpy(out->code, code->valuestring, strlen(code->valuestring) + 1);
            memcpy(out->state, state->valuestring, PW_SETUP_STATE_BYTES);
        }
    } else if (ok)
        ok = pw_keys_only(root, basic, 2);
    if (ok) {
        out->id = (uint32_t)id->valuedouble;
        out->method = decoded;
    }
    for (cJSON *item = root->child; item; item = item->next) {
        if (cJSON_IsString(item) && item->valuestring) {
            volatile char *v = item->valuestring;
            for (size_t i = 0, n = strlen(item->valuestring); i < n; i++)
                v[i] = 0;
        }
    }
    cJSON_Delete(root);
    return ok;
}
