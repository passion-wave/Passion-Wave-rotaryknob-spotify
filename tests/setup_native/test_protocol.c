// SPDX-License-Identifier: MIT
#include "pw_setup_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void expect(const char *s, bool valid, pw_setup_method_t method) {
    pw_setup_request_t out;
    memset(&out, 0xaa, sizeof out);
    assert(pw_setup_decode(s, strlen(s), &out) == valid);
    if (valid) {
        assert(out.id == 17);
        assert(out.method == method);
    } else {
        assert(out.id == 0);
        assert(!out.code[0] && !out.state[0]);
    }
    checks++;
}
int main(void) {
    expect("PWSET1 {\"id\":17,\"method\":\"hello\"}", true, PW_SETUP_HELLO);
    expect("PWSET1 {\"id\":17,\"method\":\"status\"}", true, PW_SETUP_STATUS);
    expect("PWSET1 {\"id\":17,\"method\":\"authorize\"}", true, PW_SETUP_AUTHORIZE);
    expect("PWSET1 {\"id\":17,\"method\":\"cancel\"}", true, PW_SETUP_CANCEL);
    const char *state = "0123456789abcdef0123456789abcdef0123456789abcdef";
    char line[PW_SETUP_LINE_BYTES + 16];
    snprintf(
        line, sizeof line,
        "PWSET1 {\"id\":17,\"method\":\"callback\",\"code\":\"one-time.code_-~\",\"state\":\"%s\"}",
        state);
    expect(line, true, PW_SETUP_CALLBACK);
    const char *bad[] = {
        "PWSET1 {}",
        "PWSET1 []",
        "PWSET1 null",
        "PWSET1 {\"id\":0,\"method\":\"hello\"}",
        "PWSET1 {\"id\":-1,\"method\":\"hello\"}",
        "PWSET1 {\"id\":1.1,\"method\":\"hello\"}",
        "PWSET1 {\"id\":2147483648,\"method\":\"hello\"}",
        "PWSET1 {\"id\":17,\"method\":\"hello\",\"method\":\"authorize\"}",
        "PWSET1 {\"id\":17,\"id\":18,\"method\":\"hello\"}",
        "PWSET1 {\"id\":17,\"method\":\"hello\",\"approved\":true}",
        "PWSET1 {\"id\":17,\"method\":\"hello\",\"code\":\"secret\"}",
        "PWSET1 {\"id\":17,\"method\":\"hEllo\"}",
        "PWSET1 {\"id\":17,\"method\":false}",
        "PWSET1 {\"id\":17,\"method\":\"callback\",\"state\":\"x\"}",
        "PWSET1 {\"id\":17,\"method\":\"hello\\u0000authorize\"}",
        "PWSET1 {\"id\":17,\"method\":\"hello\"}garbage",
        "PWSET1 {\"id\":17,\"method\":\"hello\"}\n",
        "PWSET1 {\"id\":17,\"method\":\"hello\"}\r",
        "PWSET1 {\"id\":\"17\",\"method\":\"hello\"}",
        "I (71) example: PWSET1 {\"id\":17,\"method\":\"hello\"}",
        "PWSET2 {\"id\":17,\"method\":\"hello\"}",
        "PWSET1 {\"id\":17,\"method\":\"hello\",\"nest\":{\"method\":\"authorize\"}}"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++)
        expect(bad[i], false, 0);
    snprintf(line, sizeof line,
             "PWSET1 {\"id\":17,\"method\":\"callback\",\"code\":\"has space\",\"state\":\"%s\"}",
             state);
    expect(line, false, 0);
    snprintf(line, sizeof line,
             "PWSET1 {\"id\":17,\"method\":\"callback\",\"code\":\"code\\nline\",\"state\":\"%s\"}",
             state);
    expect(line, false, 0);
    snprintf(line, sizeof line,
             "PWSET1 {\"id\":17,\"method\":\"callback\",\"code\":\"code\",\"state\":\"%s0\"}",
             state);
    expect(line, false, 0);
    char code[PW_SETUP_CODE_BYTES + 1];
    memset(code, 'c', sizeof code);
    code[PW_SETUP_CODE_BYTES - 1] = 0;
    snprintf(line, sizeof line,
             "PWSET1 {\"id\":17,\"method\":\"callback\",\"code\":\"%s\",\"state\":\"%s\"}", code,
             state);
    expect(line, true, PW_SETUP_CALLBACK);
    code[PW_SETUP_CODE_BYTES - 1] = 'c';
    code[PW_SETUP_CODE_BYTES] = 0;
    snprintf(line, sizeof line,
             "PWSET1 {\"id\":17,\"method\":\"callback\",\"code\":\"%s\",\"state\":\"%s\"}", code,
             state);
    expect(line, false, 0);
    pw_setup_request_t out;
    const char embedded[] = "PWSET1 {\"id\":17,\"method\":\"hello\"}\0junk";
    assert(!pw_setup_decode(embedded, sizeof embedded - 1, &out));
    checks++;
    memset(line, 'x', sizeof line);
    assert(!pw_setup_decode(line, sizeof line, &out));
    checks++;
    assert(!pw_setup_decode(NULL, 0, &out));
    checks++;
    assert(!pw_setup_decode("PWSET1 {}", 9, NULL));
    checks++;
    puts("USB setup framing/strict request validation passed (no hardware or OAuth acceptance).");
    printf("%u checks\n", checks);
    return 0;
}
