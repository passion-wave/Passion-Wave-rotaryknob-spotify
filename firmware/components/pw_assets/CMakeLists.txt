include("${CMAKE_CURRENT_LIST_DIR}/assets.cmake")
idf_component_register(SRCS "pw_assets.c" "pw_assets_index.c" "pw_assets_lookup.c" "pw_asset_decode.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "."
    REQUIRES lvgl esp_timer freertos heap
    EMBED_FILES ${PW_ASSET_FILES})
