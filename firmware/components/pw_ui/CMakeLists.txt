idf_component_register(SRCS "pw_ui.c" "fonts/pw_font_de_14.c" "fonts/pw_font_de_18.c"
         "fonts/pw_font_de_24.c" "fonts/pw_font_de_32.c" INCLUDE_DIRS "include"
    REQUIRES lvgl pw_board pw_app pw_weather pw_assets pw_spotify esp_timer heap freertos)
