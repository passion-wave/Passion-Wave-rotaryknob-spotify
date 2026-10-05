// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
#include "pw_asset_decode.h"
#include "pw_assets.h"
#include <string.h>
#include "src/libs/tjpgd/tjpgd.h"
#if !LV_USE_TJPGD || JD_FORMAT != 0
#error "pw_assets requires pinned LVGL 9.2.2 TJpgDec with 24-bit MCU output"
#endif
typedef struct {
    const uint8_t *jpeg;
    size_t bytes, offset;
    uint16_t *output;
    bool (*cancelled)(void *);
    void *context;
} decoder_context_t;
static size_t read_jpeg(JDEC *decoder, uint8_t *buffer, size_t wanted) {
    decoder_context_t *c = decoder->device;
    const size_t remaining = c->bytes - c->offset;
    if (wanted > remaining) wanted = remaining;
    if (buffer && wanted) memcpy(buffer, c->jpeg + c->offset, wanted);
    c->offset += wanted;
    return wanted;
}
static int write_pixels(JDEC *decoder, void *bitmap, JRECT *rectangle) {
    decoder_context_t *c = decoder->device;
    if (c->cancelled && c->cancelled(c->context)) return 0;
    if (rectangle->right >= PW_ASSET_WIDTH || rectangle->bottom >= PW_ASSET_HEIGHT ||
        rectangle->left > rectangle->right || rectangle->top > rectangle->bottom) return 0;
    const uint8_t *source = bitmap;
    for (unsigned y = rectangle->top; y <= rectangle->bottom; ++y) {
        uint16_t *dest = c->output + y * PW_ASSET_WIDTH + rectangle->left;
        for (unsigned x = rectangle->left; x <= rectangle->right; ++x) {
            // LVGL 9.2.2's TJpgDec fork emits B,G,R bytes for JD_FORMAT=0,
            // unlike the RGB wording in its upstream configuration comment.
            *dest++ = ((uint16_t)(source[2] & 0xF8) << 8) |
                      ((uint16_t)(source[1] & 0xFC) << 3) | (source[0] >> 3);
            source += 3;
        }
    }
    return 1;
}
bool pw_asset_decode(const uint8_t *jpeg, size_t bytes, uint16_t *output,
                     size_t output_pixels, void *workspace, size_t workspace_bytes,
                     bool (*cancelled)(void *), void *context) {
    if (!jpeg || bytes < 4 || bytes > 128u * 1024u || !output || !workspace ||
        output_pixels < PW_ASSET_WIDTH * PW_ASSET_HEIGHT || workspace_bytes < PW_ASSET_DECODE_WORKSPACE ||
        jpeg[0] != 0xFF || jpeg[1] != 0xD8 || jpeg[bytes - 2] != 0xFF || jpeg[bytes - 1] != 0xD9) return false;
    if (cancelled && cancelled(context)) return false;
    decoder_context_t c = {jpeg, bytes, 0, output, cancelled, context};
    JDEC decoder = {0};
    if (jd_prepare(&decoder, read_jpeg, workspace, workspace_bytes, &c) != JDR_OK) return false;
    if (decoder.width != PW_ASSET_WIDTH || decoder.height != PW_ASSET_HEIGHT) return false;
    return jd_decomp(&decoder, write_pixels, 0) == JDR_OK;
}
