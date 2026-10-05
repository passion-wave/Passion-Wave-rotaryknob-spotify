#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pw_companion_update.h"
#include "pw_protocol.h"
#include <string.h>
#define PEER_UART UART_NUM_1
static const char *TAG = "pw_companion";
void app_main(void) {
    const uart_config_t config = {.baud_rate = 2000000,
                                  .data_bits = UART_DATA_8_BITS,
                                  .parity = UART_PARITY_DISABLE,
                                  .stop_bits = UART_STOP_BITS_1,
                                  .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                                  .source_clk = UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(PEER_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(PEER_UART, 23, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(PEER_UART, 2048, 0, 0, NULL, 0));
    /* No DAC/MUX/MUTE GPIO output until exact revision is electrically verified. */
    uint32_t session;
    do {
        session = esp_random();
    } while (!session);
    esp_err_t update_err = pw_companion_update_init(session);
    if (update_err != ESP_OK)
        ESP_LOGW(TAG, "OTA locked: journal/init error %s", esp_err_to_name(update_err));
    pw_parser_t parser = {0};
    pw_frame_t incoming,
        reply = {.role = PW_ROLE_COMPANION, .kind = PW_MSG_HELLO, .session = session};
    uint8_t encoded[PW_FRAME_MAX], bytes[256];
    int64_t sent_at = -2000000;
    uint32_t accepted = 0, sequence = 0;
    ESP_LOGI(TAG,
             "Spotify Edition %s companion ready; OTA requires provisioned trust and verified pair",
             esp_app_get_description()->version);
    while (1) {
        int got = uart_read_bytes(PEER_UART, bytes, sizeof bytes, pdMS_TO_TICKS(20));
        for (int i = 0; i < got; i++)
            if (pw_parser_feed(&parser, bytes[i], &incoming) && incoming.role == PW_ROLE_S3) {
                accepted++;
                if (pw_companion_update_handle(&incoming, &reply)) {
                    /* OTA ACK is bound to the incoming command sequence and transaction. */
                } else if (incoming.kind == PW_MSG_HELLO) {
                    reply.kind = PW_MSG_HEALTH;
                    reply.length = 4;
                    memcpy(reply.payload, incoming.payload, incoming.length >= 4 ? 4 : 0);
                    if (incoming.length < 4)
                        memset(reply.payload, 0, 4);
                } else if (incoming.kind != PW_MSG_HEARTBEAT) {
                    reply.kind = PW_MSG_UNSUPPORTED;
                    reply.length = 1;
                    reply.payload[0] = incoming.kind;
                } else
                    continue;
                reply.role = PW_ROLE_COMPANION;
                reply.session = session;
                reply.sequence = ++sequence;
                size_t n = pw_frame_encode(&reply, encoded, sizeof encoded);
                uart_write_bytes(PEER_UART, encoded, n);
            }
        int64_t now = esp_timer_get_time();
        if (now - sent_at >= 2000000) {
            reply.kind = PW_MSG_HEARTBEAT;
            reply.length = 0;
            reply.sequence = ++sequence;
            size_t n = pw_frame_encode(&reply, encoded, sizeof encoded);
            uart_write_bytes(PEER_UART, encoded, n);
            pw_companion_update_report(&reply);
            reply.session = session;
            reply.sequence = ++sequence;
            n = pw_frame_encode(&reply, encoded, sizeof encoded);
            uart_write_bytes(PEER_UART, encoded, n);
            sent_at = now;
        }
        pw_companion_update_tick();
        if (pw_companion_update_reboot_requested()) {
            uart_wait_tx_done(PEER_UART, pdMS_TO_TICKS(500));
            esp_restart();
        }
        static unsigned count = 0;
        if (++count % 500 == 0)
            ESP_LOGI(TAG, "heap=%lu peer_frames=%lu", (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)accepted);
    }
}
