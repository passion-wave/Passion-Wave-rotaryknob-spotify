idf_component_register(SRCS "pw_app.c" "pw_validation.c" INCLUDE_DIRS "include"
 REQUIRES pw_board pw_weather pw_protocol pw_storage pw_update_service esp_wifi esp_netif esp_http_server esp_http_client esp_timer nvs_flash json mbedtls esp_driver_uart app_update)
set(PW_WEB "${CMAKE_CURRENT_LIST_DIR}/../../web")
target_add_binary_data(${COMPONENT_LIB} "${PW_WEB}/index.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "${PW_WEB}/app.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "${PW_WEB}/style.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "${PW_WEB}/places-de.json.gz" BINARY)
