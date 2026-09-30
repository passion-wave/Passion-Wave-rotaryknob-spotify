// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
// EC1 behavior and ST77916 register sequence ported from upstream 9cc5576.
// See README.md and UPSTREAM-LICENSE.txt. Other code is a native ESP-IDF port.
#include "pw_board.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "st77916_init.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "pw_board supports only the JC3636K518C_I_YR1 ESP32-S3 controller"
#endif

// Source-bound pin profile. No candidate audio/boot-strapping pins are used.
enum {
    PIN_EC1_LEFT = 7, PIN_EC1_RIGHT = 8, PIN_TOUCH_IRQ = 9,
    PIN_TOUCH_RESET = 10, PIN_SDA = 11, PIN_SCL = 12,
    PIN_LCD_CLK = 13, PIN_LCD_CS = 14, PIN_LCD_D0 = 15,
    PIN_LCD_D1 = 16, PIN_LCD_D2 = 17, PIN_LCD_D3 = 18,
    PIN_LCD_RESET = 21, PIN_BACKLIGHT = 47,
    STRIPE_ROWS = 20, DRAW_QUEUE_DEPTH = 2, I2C_TIMEOUT_MS = 5,
};

static const char *TAG = "pw_board";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool attempted, ready;
static pw_board_input_t input_state;
static pw_board_diagnostics_t diagnostics;
static QueueHandle_t draw_queue;
static SemaphoreHandle_t color_done;
static esp_lcd_panel_io_handle_t panel_io;
static uint8_t *dma_pixels;
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t touch_device, haptic_device;
static pcnt_unit_handle_t counters[2];
static pcnt_channel_handle_t channels[2];
static int previous_counts[2];
static uint8_t requested_effect;
static bool effect_pending;
static TaskHandle_t display_task_handle, input_task_handle;
static bool spi_initialized, ledc_initialized;

typedef struct {
    int x0, y0, x1, y1;
    const uint16_t *pixels;
    pw_board_draw_done_t done;
    void *context;
} draw_request_t;

static void delay_at_least_ms(unsigned ms) {
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

static esp_err_t lcd_command(uint8_t command, const void *data, size_t length) {
    // QSPI single-wire register command: 02 00 <register> 00.
    return esp_lcd_panel_io_tx_param(panel_io, (0x02U << 24) | ((uint32_t)command << 8), data, length);
}

static bool lcd_color_done(esp_lcd_panel_io_handle_t io,
                           esp_lcd_panel_io_event_data_t *event, void *context) {
    (void)io; (void)event; (void)context;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(color_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t init_display(void) {
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_LCD_RESET) | (1ULL << PIN_TOUCH_RESET),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "reset pins");
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t channel = {
        .gpio_num = PIN_BACKLIGHT, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel");
    ledc_initialized = true;
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_CLK, .data0_io_num = PIN_LCD_D0,
        .data1_io_num = PIN_LCD_D1, .data2_io_num = PIN_LCD_D2,
        .data3_io_num = PIN_LCD_D3, .data4_io_num = -1, .data5_io_num = -1,
        .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = PW_BOARD_WIDTH * STRIPE_ROWS * 2,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "LCD SPI bus");
    spi_initialized = true;
    esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = PIN_LCD_CS, .dc_gpio_num = -1,
        .spi_mode = 0, .pclk_hz = 80000000, .trans_queue_depth = 2,
        .on_color_trans_done = lcd_color_done,
        .lcd_cmd_bits = 32, .lcd_param_bits = 8, .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &panel_io), TAG, "LCD panel IO");
    gpio_set_level(PIN_LCD_RESET, 1);
    delay_at_least_ms(5);
    gpio_set_level(PIN_LCD_RESET, 0);
    delay_at_least_ms(10);
    gpio_set_level(PIN_LCD_RESET, 1);
    delay_at_least_ms(120);
    for (size_t i = 0; i < sizeof(panel_init_commands) / sizeof(panel_init_commands[0]); ++i) {
        const struct panel_init_command *entry = &panel_init_commands[i];
        if (entry->delay_ms) delay_at_least_ms(entry->delay_ms);
        else ESP_RETURN_ON_ERROR(lcd_command(entry->command, entry->data, entry->length), TAG, "panel init");
    }
    const uint8_t rgb_order = 0x00;
    ESP_RETURN_ON_ERROR(lcd_command(0x36, &rgb_order, 1), TAG, "RGB order");
    diagnostics.display_ready = true;
    return ESP_OK;
}

static esp_err_t init_counter(unsigned index, int pin) {
    pcnt_unit_config_t unit = {
        .low_limit = -32768, .high_limit = 32767, .flags.accum_count = true,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit, &counters[index]), TAG, "EC1 unit");
    pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 10000};
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(counters[index], &filter), TAG, "EC1 filter");
    pcnt_chan_config_t channel = {
        .edge_gpio_num = pin, .level_gpio_num = -1, .flags.virt_level_io_level = true,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_channel(counters[index], &channel, &channels[index]), TAG, "EC1 channel");
    ESP_RETURN_ON_ERROR(gpio_pullup_en(pin), TAG, "EC1 pullup");
    ESP_RETURN_ON_ERROR(gpio_pulldown_dis(pin), TAG, "EC1 pulldown");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(channels[index], PCNT_CHANNEL_EDGE_ACTION_HOLD,
                                                    PCNT_CHANNEL_EDGE_ACTION_INCREASE), TAG, "EC1 falling edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(channels[index], PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                     PCNT_CHANNEL_LEVEL_ACTION_KEEP), TAG, "EC1 level");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(counters[index], -32768), TAG, "EC1 low watch");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(counters[index], 32767), TAG, "EC1 high watch");
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(counters[index]), TAG, "EC1 enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(counters[index]), TAG, "EC1 initial clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(counters[index]), TAG, "EC1 start");
    return pcnt_unit_get_count(counters[index], &previous_counts[index]);
}

static esp_err_t reg_read(i2c_master_dev_handle_t device, uint8_t reg, void *data, size_t length) {
    return i2c_master_transmit_receive(device, &reg, 1, data, length, I2C_TIMEOUT_MS);
}
static esp_err_t reg_write(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(device, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t init_input_devices(void) {
    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << PIN_EC1_LEFT) | (1ULL << PIN_EC1_RIGHT) | (1ULL << PIN_TOUCH_IRQ),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "input pins");
    ESP_RETURN_ON_ERROR(init_counter(0, PIN_EC1_LEFT), TAG, "left EC1");
    ESP_RETURN_ON_ERROR(init_counter(1, PIN_EC1_RIGHT), TAG, "right EC1");
    diagnostics.encoder_ready = true;
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &i2c_bus), TAG, "I2C bus");
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x15,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &device, &touch_device), TAG, "touch device");
    device.device_address = 0x5A;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &device, &haptic_device), TAG, "haptic device");
    gpio_set_level(PIN_TOUCH_RESET, 1);
    delay_at_least_ms(5);
    gpio_set_level(PIN_TOUCH_RESET, 0);
    delay_at_least_ms(5);
    gpio_set_level(PIN_TOUCH_RESET, 1);
    delay_at_least_ms(50);
    uint8_t chip_id = 0;
    if (reg_read(touch_device, 0xA7, &chip_id, 1) == ESP_OK &&
        (chip_id == 0xB4 || chip_id == 0xB5 || chip_id == 0xB6)) {
        // CST816S/T/D: motion IRQ, disable automatic sleep for always-on USB UI.
        diagnostics.touch_ready = reg_write(touch_device, 0xFA, 0x70) == ESP_OK &&
                                  reg_write(touch_device, 0xFE, 0x01) == ESP_OK;
    }
    diagnostics.touch_chip_id = chip_id;
    uint8_t status = 0, feedback = 0;
    if (reg_read(haptic_device, 0x00, &status, 1) == ESP_OK && status != 0xFF &&
        reg_read(haptic_device, 0x1A, &feedback, 1) == ESP_OK) {
        // Preserve factory/default gain/calibration. Set LRA mode and library 6.
        // No RTP, continuous drive, voltage increase or calibration is requested.
        diagnostics.haptic_ready = reg_write(haptic_device, 0x01, 0x00) == ESP_OK &&
            reg_write(haptic_device, 0x1A, feedback | 0x80) == ESP_OK &&
            reg_write(haptic_device, 0x03, 0x06) == ESP_OK &&
            reg_write(haptic_device, 0x0C, 0x00) == ESP_OK;
    }
    input_state.touch_available = diagnostics.touch_ready;
    ESP_LOGI(TAG, "CST816 ID=0x%02x ready=%d, DRV2605 ready=%d",
             chip_id, diagnostics.touch_ready, diagnostics.haptic_ready);
    return ESP_OK;
}

static esp_err_t render_request(const draw_request_t *request) {
    const int width = request->x1 - request->x0;
    for (int y = request->y0; y < request->y1; y += STRIPE_ROWS) {
        const int rows = request->y1 - y < STRIPE_ROWS ? request->y1 - y : STRIPE_ROWS;
        const uint16_t *source = request->pixels + (y - request->y0) * width;
        const size_t count = (size_t)width * rows;
        for (size_t p = 0; p < count; ++p) {
            dma_pixels[2 * p] = source[p] >> 8;
            dma_pixels[2 * p + 1] = source[p] & 0xFF;
        }
        const uint8_t columns[4] = {request->x0 >> 8, request->x0 & 0xFF,
                                    (request->x1 - 1) >> 8, (request->x1 - 1) & 0xFF};
        const uint8_t lines[4] = {y >> 8, y & 0xFF, (y + rows - 1) >> 8, (y + rows - 1) & 0xFF};
        ESP_RETURN_ON_ERROR(lcd_command(0x2A, columns, sizeof(columns)), TAG, "columns");
        ESP_RETURN_ON_ERROR(lcd_command(0x2B, lines, sizeof(lines)), TAG, "rows");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(panel_io, 0x32002C00, dma_pixels, count * 2), TAG, "pixels");
        if (xSemaphoreTake(color_done, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void display_worker(void *context) {
    (void)context;
    bool fault = false;
    draw_request_t request;
    for (;;) {
        if (xQueueReceive(draw_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        const esp_err_t result = fault ? ESP_ERR_INVALID_STATE : render_request(&request);
        if (result != ESP_OK) {
            // A timed-out DMA buffer must never be reused. Subsequent work fails
            // promptly until restart; input/network remain available for recovery.
            fault = true;
            portENTER_CRITICAL(&state_lock);
            diagnostics.display_errors++;
            diagnostics.display_ready = false;
            portEXIT_CRITICAL(&state_lock);
        }
        if (request.done) request.done(request.context, result);
    }
}

static void sample_encoder(void) {
    int values[2];
    const esp_err_t left_error = pcnt_unit_get_count(counters[0], &values[0]);
    const esp_err_t right_error = pcnt_unit_get_count(counters[1], &values[1]);
    if (left_error != ESP_OK || right_error != ESP_OK) {
        portENTER_CRITICAL(&state_lock);
        diagnostics.encoder_errors++;
        portEXIT_CRITICAL(&state_lock);
        return;
    }
    // Unsigned modular subtraction also handles the long-run 32-bit wrap.
    const uint32_t left = (uint32_t)values[0] - (uint32_t)previous_counts[0];
    const uint32_t right = (uint32_t)values[1] - (uint32_t)previous_counts[1];
    previous_counts[0] = values[0]; previous_counts[1] = values[1];
    portENTER_CRITICAL(&state_lock);
    if (left > INT32_MAX || right > INT32_MAX) {
        diagnostics.encoder_errors++;
    } else {
        diagnostics.left_pulses += left;
        diagnostics.right_pulses += right;
        const uint32_t sum = left + right;
        if (sum > diagnostics.max_encoder_batch) diagnostics.max_encoder_batch = sum;
        if (left && right) diagnostics.ambiguous_batches++;
        else {
            const int64_t accumulated = (int64_t)input_state.rotation_delta + (int64_t)right - left;
            input_state.rotation_delta = accumulated > INT32_MAX ? INT32_MAX :
                                         accumulated < INT32_MIN ? INT32_MIN : (int32_t)accumulated;
        }
    }
    input_state.sampled_at_us = esp_timer_get_time();
    portEXIT_CRITICAL(&state_lock);
}

static void input_worker(void *context) {
    (void)context;
    TickType_t wake = xTaskGetTickCount();
    int64_t last_touch_read = 0;
    for (;;) {
        sample_encoder();
        if (diagnostics.touch_ready) {
            uint8_t packet[7];
            const esp_err_t result = reg_read(touch_device, 0x00, packet, sizeof(packet));
            const int64_t now = esp_timer_get_time();
            portENTER_CRITICAL(&state_lock);
            if (result == ESP_OK) {
                const uint16_t x = ((packet[3] & 0x0F) << 8) | packet[4];
                const uint16_t y = ((packet[5] & 0x0F) << 8) | packet[6];
                const uint8_t event = packet[3] >> 6;
                input_state.touch_pressed = (packet[2] & 0x03) != 0 && event != 1 &&
                                            x < PW_BOARD_WIDTH && y < PW_BOARD_HEIGHT;
                if (input_state.touch_pressed) {
                    input_state.touch_x = x;
                    input_state.touch_y = y;
                }
                last_touch_read = now;
            } else {
                diagnostics.touch_errors++;
                if (now - last_touch_read > 100000) input_state.touch_pressed = false;
            }
            portEXIT_CRITICAL(&state_lock);
        }
        uint8_t effect = 0;
        bool pending;
        portENTER_CRITICAL(&state_lock);
        pending = effect_pending;
        effect = requested_effect;
        effect_pending = false;
        portEXIT_CRITICAL(&state_lock);
        if (pending) {
            esp_err_t result = reg_write(haptic_device, 0x0C, 0x00);
            if (effect && result == ESP_OK) {
                result = reg_write(haptic_device, 0x04, effect);
                if (result == ESP_OK) result = reg_write(haptic_device, 0x05, 0);
                if (result == ESP_OK) result = reg_write(haptic_device, 0x0C, 1);
            }
            if (result != ESP_OK) {
                portENTER_CRITICAL(&state_lock);
                diagnostics.haptic_errors++;
                portEXIT_CRITICAL(&state_lock);
            }
        }
        TickType_t interval = pdMS_TO_TICKS(10);
        vTaskDelayUntil(&wake, interval > 0 ? interval : 1);
    }
}

static void cleanup_failed_init(void) {
    if (display_task_handle) { vTaskDelete(display_task_handle); display_task_handle = NULL; }
    if (input_task_handle) { vTaskDelete(input_task_handle); input_task_handle = NULL; }
    if (panel_io) { esp_lcd_panel_io_del(panel_io); panel_io = NULL; }
    if (spi_initialized) { spi_bus_free(SPI2_HOST); spi_initialized = false; }
    if (ledc_initialized) ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    for (unsigned i = 0; i < 2; ++i) {
        if (counters[i]) { pcnt_unit_stop(counters[i]); pcnt_unit_disable(counters[i]); }
        if (channels[i]) { pcnt_del_channel(channels[i]); channels[i] = NULL; }
        if (counters[i]) { pcnt_del_unit(counters[i]); counters[i] = NULL; }
    }
    if (touch_device) { i2c_master_bus_rm_device(touch_device); touch_device = NULL; }
    if (haptic_device) { i2c_master_bus_rm_device(haptic_device); haptic_device = NULL; }
    if (i2c_bus) { i2c_del_master_bus(i2c_bus); i2c_bus = NULL; }
    if (draw_queue) { vQueueDelete(draw_queue); draw_queue = NULL; }
    if (color_done) { vSemaphoreDelete(color_done); color_done = NULL; }
    heap_caps_free(dma_pixels); dma_pixels = NULL;
    memset(&diagnostics, 0, sizeof(diagnostics));
}

esp_err_t pw_board_init(void) {
    if (attempted) return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    attempted = true;
    draw_queue = xQueueCreate(DRAW_QUEUE_DEPTH, sizeof(draw_request_t));
    color_done = xSemaphoreCreateBinary();
    dma_pixels = heap_caps_malloc(PW_BOARD_WIDTH * STRIPE_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    esp_err_t result = ESP_ERR_NO_MEM;
    if (!draw_queue || !color_done || !dma_pixels) goto fail;
    result = init_display();
    if (result != ESP_OK) goto fail;
    result = init_input_devices();
    if (result != ESP_OK) goto fail;
    result = ESP_ERR_NO_MEM;
    if (xTaskCreate(display_worker, "pw_display", 4096, NULL, 5, &display_task_handle) != pdPASS) goto fail;
    if (xTaskCreate(input_worker, "pw_input", 4096, NULL, 6, &input_task_handle) != pdPASS) goto fail;
    ready = true;
    ESP_LOGI(TAG, "%s: 360x360 QSPI, EC1 PCNT, asynchronous IO ready", PW_BOARD_MODEL);
    // UI owns when to illuminate the first complete frame, avoiding uninitialized
    // panel contents. Brightness stays at zero until explicitly requested.
    return ESP_OK;
fail:
    cleanup_failed_init();
    ESP_LOGE(TAG, "initialization failed: %s", esp_err_to_name(result));
    return result;
}

esp_err_t pw_board_draw_bitmap(int x0, int y0, int x1, int y1, const uint16_t *rgb565,
                               pw_board_draw_done_t done, void *context) {
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (!rgb565 || !done || x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0 ||
        x1 > PW_BOARD_WIDTH || y1 > PW_BOARD_HEIGHT) return ESP_ERR_INVALID_ARG;
    const draw_request_t request = {x0, y0, x1, y1, rgb565, done, context};
    if (xQueueSend(draw_queue, &request, 0) != pdTRUE) {
        portENTER_CRITICAL(&state_lock);
        diagnostics.draw_queue_full++;
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t pw_board_poll_input(pw_board_input_t *input) {
    if (!input) return ESP_ERR_INVALID_ARG;
    if (!ready) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&state_lock);
    *input = input_state;
    input_state.rotation_delta = 0;
    portEXIT_CRITICAL(&state_lock);
    return ESP_OK;
}

esp_err_t pw_board_set_brightness(uint8_t percent) {
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    return ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                   ((uint32_t)percent * 1023 + 50) / 100, 0);
}

esp_err_t pw_board_haptic(uint8_t effect) {
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (effect > 123) return ESP_ERR_INVALID_ARG;
    if (!diagnostics.haptic_ready) return ESP_ERR_NOT_SUPPORTED;
    portENTER_CRITICAL(&state_lock);
    requested_effect = effect;
    effect_pending = true;
    portEXIT_CRITICAL(&state_lock);
    return ESP_OK;
}

void pw_board_get_diagnostics(pw_board_diagnostics_t *output) {
    if (!output) return;
    portENTER_CRITICAL(&state_lock);
    *output = diagnostics;
    portEXIT_CRITICAL(&state_lock);
}
