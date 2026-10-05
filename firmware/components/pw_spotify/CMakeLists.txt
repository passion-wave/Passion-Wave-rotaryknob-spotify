idf_component_register(SRCS "pw_spotify.c" "pw_spotify_model.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_http_client esp_timer esp_system mbedtls json nvs_flash)

# Lab-only observation of the pinned HTTP parser; never log response bytes.
if(CONFIG_PW_SPOTIFY_LAB)
    target_sources(${COMPONENT_LIB} PRIVATE "pw_http_diagnostic.c")
    idf_component_get_property(http_parser_lib http_parser COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PRIVATE ${http_parser_lib})
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=http_parser_execute" "-Wl,--undefined=__wrap_http_parser_execute")
endif()
