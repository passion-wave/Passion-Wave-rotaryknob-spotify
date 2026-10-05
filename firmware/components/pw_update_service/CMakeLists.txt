idf_component_register(SRCS "pw_update_stage.c" "pw_update_service.c"
    INCLUDE_DIRS "include" REQUIRES pw_update_verify pw_protocol pw_weather
    app_update bootloader_support nvs_flash spi_flash esp_timer esp_app_format)
