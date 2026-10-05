// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// No output allocation; handles gzip, zlib-wrapped HTTP deflate, identity.
bool pw_weather_decode_body(const uint8_t *input, size_t length, const char *encoding,
                            uint8_t *output, size_t capacity, size_t *written);
