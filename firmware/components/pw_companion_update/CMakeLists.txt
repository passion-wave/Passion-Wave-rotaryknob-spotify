idf_component_register(SRCS "pw_companion_update_core.c" "pw_companion_update.c"
    INCLUDE_DIRS "include" REQUIRES pw_protocol pw_update_verify app_update nvs_flash esp_timer spi_flash esp_app_format)
