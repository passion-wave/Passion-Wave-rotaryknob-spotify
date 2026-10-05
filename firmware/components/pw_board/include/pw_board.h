// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PW_BOARD_WIDTH 360
#define PW_BOARD_HEIGHT 360
#define PW_BOARD_MODEL "JC3636K518C_I_YR1"

typedef struct {
    int32_t rotation_delta;       // EC1 only; + clockwise. Ambiguous pulses rejected.
    uint16_t touch_x;
    uint16_t touch_y;
    bool touch_pressed;
    bool touch_available;
    uint64_t sampled_at_us;
} pw_board_input_t;

typedef struct {
    bool display_ready;
    bool encoder_ready;
    bool touch_ready;
    bool haptic_ready;
    uint8_t touch_chip_id;
    uint64_t left_pulses;
    uint64_t right_pulses;
    uint32_t ambiguous_batches;
    uint32_t encoder_errors;
    uint32_t touch_errors;
    uint32_t display_errors;
    uint32_t haptic_errors;
    uint32_t draw_queue_full;
    uint32_t max_encoder_batch;
} pw_board_diagnostics_t;

// Call once from a task at startup. Reset/initialization waits occur only here.
// Fatal display/encoder failures return an error. Optional absent touch/haptics
// are reported by diagnostics; no audio, UART, strapping or power pins are driven.
esp_err_t pw_board_init(void);

// Nonblocking submission. RGB565 uses native uint16_t endian, tightly packed.
// x1/y1 are exclusive. Buffer must remain alive AND unchanged until callback.
// Callback runs on the display worker, never an ISR; keep it short, no waits.
// Returns ESP_ERR_TIMEOUT when the bounded queue is full: no ownership transfer
// and no callback then. Every ESP_OK submission gets exactly one completion.
typedef void (*pw_board_draw_done_t)(void *context, esp_err_t result);
esp_err_t pw_board_draw_bitmap(int x0, int y0, int x1, int y1,
                               const uint16_t *rgb565,
                               pw_board_draw_done_t done, void *context);

// No I2C/SPI waits. One UI consumer takes accumulated rotation once; cached
// touch remains current. Call at 5-10 ms cadence. Tasks only, not ISR safe.
esp_err_t pw_board_poll_input(pw_board_input_t *input);
esp_err_t pw_board_set_brightness(uint8_t percent);
// Effect 1..123, zero stops. Latest request replaces a pending effect; the
// background input worker serializes all I2C. ESP_ERR_NOT_SUPPORTED if absent.
esp_err_t pw_board_haptic(uint8_t effect);
void pw_board_get_diagnostics(pw_board_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
