set(setup_requires pw_app pw_spotify esp_timer json mbedtls app_update)
# IDF expands requirements before CONFIG_* choices are loaded. Target is known.
if(IDF_TARGET STREQUAL "esp32s3")
 list(APPEND setup_requires esp_driver_usb_serial_jtag)
endif()
idf_component_register(SRCS "pw_setup_usb.c" "pw_setup_protocol.c" INCLUDE_DIRS "include"
 REQUIRES ${setup_requires})
