// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PW_SETUP_PREFIX "PWSET1 "
#define PW_SETUP_LINE_BYTES 8192u
#define PW_SETUP_CODE_BYTES 1025u
#define PW_SETUP_STATE_BYTES 49u

typedef enum {
    PW_SETUP_HELLO,
    PW_SETUP_STATUS,
    PW_SETUP_AUTHORIZE,
    PW_SETUP_CALLBACK,
    PW_SETUP_CANCEL
} pw_setup_method_t;
typedef struct {
    uint32_t id;
    pw_setup_method_t method;
    char code[PW_SETUP_CODE_BYTES], state[PW_SETUP_STATE_BYTES];
} pw_setup_request_t;
/* Includes prefix, excludes line terminator/NUL. Rejects duplicate/unknown keys,
 * non-integral IDs, nested/oversize input and embedded control/NUL characters.
 * On failure id remains zero; never echo the input or its secret fields. */
bool pw_setup_decode(const char *line, size_t size, pw_setup_request_t *out);
