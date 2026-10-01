idf_component_register(SRCS "pw_setup_usb.c" "pw_setup_protocol.c" INCLUDE_DIRS "include"
 REQUIRES pw_app pw_spotify esp_driver_uart esp_timer json mbedtls app_update)
