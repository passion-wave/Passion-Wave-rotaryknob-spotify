#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "pw_app.h"
#include "pw_board.h"
#include "pw_ui.h"
#include "pw_storage.h"
#include "pw_update_service.h"
#include "pw_setup_usb.h"
void app_main(void) {
    ESP_LOGI("pw", "Spotify Edition %s; %s", esp_app_get_description()->version, PW_APP_HARDWARE);
    /* Generate NVS keys before board peripherals, ADC or Wi-Fi can use the RNG source. */
    ESP_ERROR_CHECK(pw_storage_init());
    ESP_ERROR_CHECK(pw_board_init());
    ESP_ERROR_CHECK(pw_app_init());
    ESP_ERROR_CHECK(pw_ui_init());
    ESP_ERROR_CHECK(pw_setup_usb_init());
#if CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC
    ESP_LOGI("pw", "TLS allocator=PSRAM; free=%zu; largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    /* Factory/local boot can finish without Internet. An OTA journal prevents
     * early cancellation of rollback; the future pair coordinator owns that. */
    if (pw_update_service_may_finalize_boot())
        esp_ota_mark_app_valid_cancel_rollback();
}
