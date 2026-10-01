idf_component_register(SRCS "pw_spotify.c" "pw_spotify_model.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_http_client esp_timer esp_system mbedtls json nvs_flash)
