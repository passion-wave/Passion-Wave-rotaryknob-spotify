// SPDX-License-Identifier: MIT
// Length-prefixed test records only. No device, serial port or credential output.
#include "pw_setup_protocol.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    uint8_t header[4];
    while (fread(header, 1, 4, stdin) == 4) {
        uint32_t size = (uint32_t)header[0] | (uint32_t)header[1] << 8 |
                        (uint32_t)header[2] << 16 | (uint32_t)header[3] << 24;
        if (size > 65536) return 2;
        char *line = calloc(size + 1, 1);
        if (!line || fread(line, 1, size, stdin) != size) return 3;
        pw_setup_request_t out;
        bool valid = pw_setup_decode(line, size, &out);
        printf("%u %lu %u\n", (unsigned)valid, (unsigned long)out.id, (unsigned)out.method);
        free(line);
    }
    return ferror(stdin) ? 4 : 0;
}
