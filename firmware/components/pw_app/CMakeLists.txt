idf_component_register(SRCS "pw_app.c" "pw_validation.c" "pw_http_socket.c" INCLUDE_DIRS "include"
 REQUIRES pw_board pw_weather pw_protocol pw_storage pw_update_service pw_spotify esp_wifi esp_netif esp_http_server esp_http_client esp_timer nvs_flash json mbedtls esp_driver_uart app_update)
set(PW_WEB "${CMAKE_CURRENT_LIST_DIR}/../../web")
set(PW_WEB_PACK "${CMAKE_CURRENT_LIST_DIR}/../../../tools/pw_web_pack.py")
set(PW_WEB_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/web")
set(PW_WEB_SOURCES "${PW_WEB}/index.html" "${PW_WEB}/app.js" "${PW_WEB}/style.css")
set(PW_WEB_OUTPUTS "${PW_WEB_OUTPUT_DIR}/index.html.gz"
                   "${PW_WEB_OUTPUT_DIR}/app.js.gz" "${PW_WEB_OUTPUT_DIR}/style.css.gz")
idf_build_get_property(PW_WEB_PYTHON PYTHON)
add_custom_command(OUTPUT ${PW_WEB_OUTPUTS}
    COMMAND "${PW_WEB_PYTHON}" "${PW_WEB_PACK}"
            --source-dir "${PW_WEB}" --output-dir "${PW_WEB_OUTPUT_DIR}"
    DEPENDS ${PW_WEB_SOURCES} "${PW_WEB_PACK}"
    COMMENT "Compressing the local device website"
    VERBATIM)
add_custom_target(pw_web_assets DEPENDS ${PW_WEB_OUTPUTS})
add_dependencies(${COMPONENT_LIB} pw_web_assets)
foreach(PW_WEB_OUTPUT IN LISTS PW_WEB_OUTPUTS)
    target_add_binary_data(${COMPONENT_LIB} "${PW_WEB_OUTPUT}" BINARY DEPENDS pw_web_assets)
endforeach()
target_add_binary_data(${COMPONENT_LIB} "${PW_WEB}/places-de.json.gz" BINARY)
