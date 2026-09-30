#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "pw_app.h"
#include "pw_board.h"
#include "pw_ui.h"
#include "pw_storage.h"
#include "pw_update_service.h"
void app_main(void) {
    ESP_LOGI("pw", "Spotify Edition %s; %s", esp_app_get_description()->version, PW_APP_HARDWARE);
    /* Generate NVS keys before board peripherals, ADC or Wi-Fi can use the RNG source. */
    ESP_ERROR_CHECK(pw_storage_init());
    ESP_ERROR_CHECK(pw_board_init());
    ESP_ERROR_CHECK(pw_app_init());
    ESP_ERROR_CHECK(pw_ui_init());
    /* Factory/local boot can finish without Internet. An OTA journal prevents
     * early cancellation of rollback; the future pair coordinator owns that. */
    if (pw_update_service_may_finalize_boot())
        esp_ota_mark_app_valid_cancel_rollback();
}
