// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PW_ASSET_DECODE_WORKSPACE 16384
// Pure bounded decoder, independently host-testable. All storage belongs to
// caller. Returns false for cancellation, malformed/truncated/wrong-size JPEG.
bool pw_asset_decode(const uint8_t *jpeg, size_t bytes, uint16_t *output,
                     size_t output_pixels, void *workspace, size_t workspace_bytes,
                     bool (*cancelled)(void *), void *context);
