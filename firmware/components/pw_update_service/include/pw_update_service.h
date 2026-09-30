// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "pw_protocol.h"
#include "pw_update_stage.h"

typedef struct {
    pw_stage_view_t job;
    bool upload_enabled, busy, activation_enabled;
    uint8_t peer_phase;
    char reason[48];
} pw_update_service_view_t;
typedef int (*pw_update_upload_read_t)(void *context, uint8_t *buffer, size_t capacity);
/* Called once by the worker; it releases the async HTTP request even on error. */
typedef void (*pw_update_upload_reply_t)(void *context, bool accepted, const char *error);

esp_err_t pw_update_service_init(void); /* after pw_storage_init + pw_weather_init */
void pw_update_service_get_view(pw_update_service_view_t *);
esp_err_t pw_update_service_upload(uint32_t bytes, bool usb_power_confirmed,
                                   pw_update_upload_read_t read, pw_update_upload_reply_t reply,
                                   void *context);
/* Called by the EXISTING sole UART task, never creates/reads a UART itself. */
void pw_update_service_transport_session(uint32_t boot_nonce);
void pw_update_service_receive(const pw_frame_t *);
bool pw_update_service_take_frame(pw_frame_t *);
/* Root boot finalizer must consult this before cancelling IDF rollback. */
bool pw_update_service_may_finalize_boot(void);
void pw_update_service_local_health(uint32_t flags);
/* Weak, closed default. Only authenticated local provisioning may override. */
const pw_stage_config_t *pw_update_service_provisioned_config(void);
