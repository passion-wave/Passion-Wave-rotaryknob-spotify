idf_component_register(SRCS "pw_storage.c" INCLUDE_DIRS "include"
                       REQUIRES nvs_flash mbedtls bootloader_support esp_hw_support esp_rom)
