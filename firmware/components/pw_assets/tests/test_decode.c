// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "pw_asset_decode.h"
#include "pw_assets.h"
#include "color_reference.h"

static int countdown;
static bool cancel_after(void *context) { (void)context; return --countdown <= 0; }

int main(int argc, char **argv) {
    assert(argc > 1);
    assert(argc - 1 == COLOR_IMAGE_COUNT);
    const size_t pixels = PW_ASSET_WIDTH * PW_ASSET_HEIGHT;
    uint16_t *guard = malloc((pixels + 32) * sizeof(uint16_t));
    void *workspace = malloc(PW_ASSET_DECODE_WORKSPACE);
    assert(guard && workspace);
    uint16_t *output = guard + 16;
    for (int index = 1; index < argc; ++index) {
        FILE *file = fopen(argv[index], "rb");
        assert(file && fseek(file, 0, SEEK_END) == 0);
        long length = ftell(file);
        assert(length > 16);
        rewind(file);
        size_t bytes = (size_t)length;
        uint8_t *data = malloc(bytes);
        assert(data && fread(data, 1, bytes, file) == bytes);
        fclose(file);
        for (unsigned p = 0; p < 16; ++p) guard[p] = guard[pixels + 16 + p] = 0xA55A;
        assert(pw_asset_decode(data, bytes, output, pixels, workspace,
                               PW_ASSET_DECODE_WORKSPACE, NULL, NULL));
        // References come from the original JPEG decoded by Apple's ImageIO,
        // not this TJpgDec implementation. RGB565 quantization and JPEG chroma
        // upsampling differ slightly; regional means tolerate those differences.
        for (unsigned region = 0; region < COLOR_REGION_COUNT; ++region) {
            unsigned sums[3] = {0};
            const unsigned *r = color_regions[region];
            for (unsigned y = r[1]; y < r[1] + r[2]; ++y)
                for (unsigned x = r[0]; x < r[0] + r[2]; ++x) {
                    uint16_t pixel = output[y * PW_ASSET_WIDTH + x];
                    sums[0] += (pixel >> 11) * 255 / 31;
                    sums[1] += ((pixel >> 5) & 63) * 255 / 63;
                    sums[2] += (pixel & 31) * 255 / 31;
                }
            for (unsigned channel = 0; channel < 3; ++channel) {
                int actual = (int)(sums[channel] / (r[2] * r[2]));
                int expected = color_reference[index - 1][region][channel];
                if (abs(actual - expected) > COLOR_TOLERANCE) {
                    fprintf(stderr, "Color mismatch %s region %u channel %u: %d != %d\n",
                            argv[index], region, channel, actual, expected);
                    abort();
                }
            }
        }
        assert(!pw_asset_decode(data, bytes - 2, output, pixels, workspace,
                                PW_ASSET_DECODE_WORKSPACE, NULL, NULL));
        assert(!pw_asset_decode(data, bytes, output, pixels - 1, workspace,
                                PW_ASSET_DECODE_WORKSPACE, NULL, NULL));
        assert(!pw_asset_decode(data, bytes, output, pixels, workspace,
                                PW_ASSET_DECODE_WORKSPACE - 1, NULL, NULL));
        countdown = 5;
        assert(!pw_asset_decode(data, bytes, output, pixels, workspace,
                                PW_ASSET_DECODE_WORKSPACE, cancel_after, NULL));
        countdown = 1;
        assert(!pw_asset_decode(data, bytes, output, pixels, workspace,
                                PW_ASSET_DECODE_WORKSPACE, cancel_after, NULL));
        // Even with a terminal EOI marker a truncated entropy stream must fail.
        data[bytes / 2 - 2] = 0xFF; data[bytes / 2 - 1] = 0xD9;
        assert(!pw_asset_decode(data, bytes / 2, output, pixels, workspace,
                                PW_ASSET_DECODE_WORKSPACE, NULL, NULL));
        for (unsigned p = 0; p < 16; ++p)
            assert(guard[p] == 0xA55A && guard[pixels + 16 + p] == 0xA55A);
        free(data);
    }
    free(guard); free(workspace);
    printf("%d JPEGs: independent RGB reference, decode, bounds, truncation and cancellation passed\n", argc - 1);
}
