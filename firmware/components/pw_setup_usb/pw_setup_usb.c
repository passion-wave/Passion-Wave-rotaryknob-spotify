// SPDX-License-Identifier: MIT
#include "pw_setup_usb.h"
#include "sdkconfig.h"
#if !CONFIG_PW_SPOTIFY_LAB
esp_err_t pw_setup_usb_init(void) {
    return ESP_OK;
}
#else
#include "cJSON.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "pw_app.h"
#include "pw_setup_protocol.h"
#include "pw_spotify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(PW_SETUP_CODE_BYTES == PW_SPOTIFY_AUTH_CODE_BYTES, "OAuth code limit mismatch");
_Static_assert(PW_SETUP_STATE_BYTES == PW_SPOTIFY_AUTH_STATE_BYTES, "OAuth state limit mismatch");
#if !CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_NUM != 0
#error "USB lab setup requires the qualified UART0 console profile"
#endif

static TaskHandle_t task;
static char *input_line;
static pw_setup_request_t *request;
static pw_spotify_snapshot_t *snapshot;
static bool authorization_started;

static bool setup_open(void) {
    pw_app_view_t view;
    pw_app_get_view(&view);
    bool opened = view.setup_open && view.setup_seconds_left > 0;
    mbedtls_platform_zeroize(&view, sizeof view);
    return opened;
}
static const char *error_code(esp_err_t err) {
    if (err == ESP_ERR_NOT_SUPPORTED)
        return "lab_disabled";
    if (err == ESP_ERR_INVALID_STATE)
        return "not_ready";
    if (err == ESP_ERR_INVALID_ARG)
        return "invalid_request";
    if (err == ESP_ERR_NO_MEM)
        return "busy";
    if (err == ESP_ERR_TIMEOUT)
        return "busy";
    return "device_error";
}
static void send_response(cJSON *root, uint32_t id) {
    char *json = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    size_t size = json ? strlen(json) : 0;
    char fallback[160];
    char *wire = NULL;
    size_t wire_size = 0;
    if (size && size < PW_SETUP_LINE_BYTES - sizeof(PW_SETUP_PREFIX) - 1) {
        wire_size = sizeof(PW_SETUP_PREFIX) - 1 + size + 1;
        wire = malloc(wire_size + 1);
        if (wire)
            snprintf(wire, wire_size + 1, PW_SETUP_PREFIX "%s\n", json);
    }
    if (json) {
        mbedtls_platform_zeroize(json, size);
        cJSON_free(json);
    }
    if (wire) {
        uart_write_bytes(UART_NUM_0, wire, wire_size);
        mbedtls_platform_zeroize(wire, wire_size);
        free(wire);
    } else {
        int n = snprintf(fallback, sizeof fallback,
                         PW_SETUP_PREFIX "{\"id\":%lu,\"ok\":false,\"error\":\"busy\"}\n",
                         (unsigned long)id);
        if (n > 0 && n < (int)sizeof fallback)
            uart_write_bytes(UART_NUM_0, fallback, n);
    }
}
static cJSON *response_base(uint32_t id, bool ok) {
    cJSON *root = cJSON_CreateObject();
    if (!root || !cJSON_AddNumberToObject(root, "id", id) ||
        !cJSON_AddBoolToObject(root, "ok", ok)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}
static void failure(uint32_t id, const char *error) {
    cJSON *root = response_base(id, false);
    if (root && !cJSON_AddStringToObject(root, "error", error)) {
        cJSON_Delete(root);
        root = NULL;
    }
    send_response(root, id);
}
static bool add_identity(cJSON *root, bool opened) {
    return cJSON_AddStringToObject(root, "role", "controller_s3") &&
           cJSON_AddStringToObject(root, "hardware", PW_APP_HARDWARE) &&
           cJSON_AddStringToObject(root, "version", esp_app_get_description()->version) &&
           cJSON_AddBoolToObject(root, "lab_enabled", true) &&
           cJSON_AddBoolToObject(root, "setup_open", opened);
}
static void handle(const pw_setup_request_t *r) {
    bool opened = setup_open();
    if (r->method != PW_SETUP_HELLO && !opened) {
        failure(r->id, "setup_closed");
        return;
    }
    cJSON *root = response_base(r->id, true);
    if (!root) {
        send_response(NULL, r->id);
        return;
    }
    esp_err_t err = ESP_OK;
    bool complete = true;
    switch (r->method) {
    case PW_SETUP_HELLO:
        complete = add_identity(root, opened);
        break;
    case PW_SETUP_STATUS: {
        complete = add_identity(root, opened);
        pw_spotify_get_snapshot(snapshot);
        cJSON *spotify = cJSON_AddObjectToObject(root, "spotify");
        complete =
            complete && spotify && cJSON_AddBoolToObject(spotify, "linked", snapshot->linked) &&
            cJSON_AddStringToObject(spotify, "state", pw_spotify_state_name(snapshot->state)) &&
            cJSON_AddStringToObject(spotify, "error", pw_spotify_error_name(snapshot->error)) &&
            cJSON_AddBoolToObject(spotify, "connected", snapshot->connected) &&
            cJSON_AddNumberToObject(spotify, "session", snapshot->session) &&
            cJSON_AddStringToObject(spotify, "authorization_id", snapshot->authorization_id) &&
            cJSON_AddNumberToObject(spotify, "http_status", snapshot->http_status);
        mbedtls_platform_zeroize(snapshot, sizeof *snapshot);
        break;
    }
    case PW_SETUP_AUTHORIZE: {
        pw_spotify_auth_start_t *auth = calloc(1, sizeof *auth);
        if (!auth) {
            err = ESP_ERR_NO_MEM;
            break;
        }
        err = pw_spotify_begin_auth(auth);
        if (err == ESP_OK) {
            complete =
                cJSON_AddStringToObject(root, "authorization_url", auth->url) &&
                cJSON_AddNumberToObject(root, "expires_in_seconds", auth->expires_in_seconds);
            authorization_started = true;
            if (!complete) {
                pw_spotify_cancel_auth();
                authorization_started = false;
            }
        }
        mbedtls_platform_zeroize(auth, sizeof *auth);
        free(auth);
        break;
    }
    case PW_SETUP_CALLBACK:
        err = pw_spotify_complete_auth(r->code, r->state);
        if (err == ESP_OK) {
            complete = cJSON_AddBoolToObject(root, "accepted", true) != NULL;
            authorization_started = false;
        }
        break;
    case PW_SETUP_CANCEL:
        pw_spotify_cancel_auth();
        authorization_started = false;
        break;
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        failure(r->id, error_code(err));
    } else if (!complete) {
        cJSON_Delete(root);
        send_response(NULL, r->id);
    } else
        send_response(root, r->id);
}
static void worker(void *unused) {
    (void)unused;
    uint8_t chunk[128];
    size_t used = 0;
    bool overflow = false;
    int64_t last_byte = 0;
    for (;;) {
        int got = uart_read_bytes(UART_NUM_0, chunk, sizeof chunk, pdMS_TO_TICKS(100));
        int64_t now = esp_timer_get_time();
        if (used && now - last_byte > 5LL * 1000000) {
            mbedtls_platform_zeroize(input_line, used);
            used = 0;
            overflow = false;
        }
        for (int i = 0; i < got; i++) {
            unsigned char c = chunk[i];
            last_byte = now;
            if (c == '\n') {
                if (!overflow && used && input_line[used - 1] == '\r')
                    used--;
                input_line[used] = 0;
                if (!overflow && pw_setup_decode(input_line, used, request))
                    handle(request);
                mbedtls_platform_zeroize(request, sizeof *request);
                mbedtls_platform_zeroize(input_line, used);
                used = 0;
                overflow = false;
            } else if (!overflow) {
                if (used + 1 < PW_SETUP_LINE_BYTES)
                    input_line[used++] = (char)c;
                else
                    overflow = true;
            }
        }
        mbedtls_platform_zeroize(chunk, sizeof chunk);
        if (authorization_started && !setup_open()) {
            pw_spotify_cancel_auth();
            authorization_started = false;
        }
    }
}
esp_err_t pw_setup_usb_init(void) {
    if (task)
        return ESP_OK;
    if (uart_is_driver_installed(UART_NUM_0))
        return ESP_ERR_INVALID_STATE;
    input_line = calloc(1, PW_SETUP_LINE_BYTES);
    request = calloc(1, sizeof *request);
    snapshot = calloc(1, sizeof *snapshot);
    if (!input_line || !request || !snapshot)
        goto allocation_failed;
    uart_config_t config = {.baud_rate = 115200,
                            .data_bits = UART_DATA_8_BITS,
                            .parity = UART_PARITY_DISABLE,
                            .stop_bits = UART_STOP_BITS_1,
                            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                            .source_clk = UART_SCLK_DEFAULT};
    esp_err_t err = uart_param_config(UART_NUM_0, &config);
    if (err != ESP_OK)
        goto init_failed;
    /* Keep ROM/default console pins. In particular never reconfigure UART1. */
    err = uart_driver_install(UART_NUM_0, PW_SETUP_LINE_BYTES, 0, 0, NULL, 0);
    if (err != ESP_OK)
        goto init_failed;
    uart_vfs_dev_use_driver(UART_NUM_0);
    if (xTaskCreate(worker, "pw_usb_setup", 6144, NULL, 3, &task) != pdPASS) {
        /* Keep the installed console driver usable; do not point VFS at a freed driver. */
        err = ESP_ERR_NO_MEM;
        goto init_failed;
    }
    return ESP_OK;
allocation_failed:
    free(input_line);
    free(request);
    free(snapshot);
    input_line = NULL;
    request = NULL;
    snapshot = NULL;
    return ESP_ERR_NO_MEM;
init_failed:
    free(input_line);
    free(request);
    free(snapshot);
    input_line = NULL;
    request = NULL;
    snapshot = NULL;
    return err;
}
#endif
