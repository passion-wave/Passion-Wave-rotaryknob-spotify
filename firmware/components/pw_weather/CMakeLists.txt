idf_component_register(SRCS "pw_weather.c" "pw_weather_model.c" "pw_weather_inflate.c" "pw_weather_avatar.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_netif esp_timer esp_system mbedtls json esp_rom)
