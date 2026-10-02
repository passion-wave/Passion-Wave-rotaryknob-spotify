// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "pw_companion_update_core.h"
#ifdef __cplusplus
extern "C" {
#endif
// Initializes NVS journal (does not erase NVS on errors) and inactivated-slot API.
esp_err_t pw_companion_update_init(uint32_t boot_nonce);
bool pw_companion_update_handle(const pw_frame_t *request, pw_frame_t *reply);
void pw_companion_update_report(pw_frame_t *report);
void pw_companion_update_tick(void);
bool pw_companion_update_reboot_requested(void);
// Weak default returns NULL. Provisioning translation unit may override; never
// accepts trust or policy from uploaded JSON. No production key in this component.
const pw_companion_update_config_t *pw_companion_update_provisioned_config(void);
#ifdef __cplusplus
}
#endif
