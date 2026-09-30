cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/pw_protocol"
    "${CMAKE_CURRENT_LIST_DIR}/../components/pw_update_verify"
    "${CMAKE_CURRENT_LIST_DIR}/../components/pw_companion_update")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../VERSION" PROJECT_VER LIMIT_COUNT 1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(passionwave_companion VERSION 0.1.0)
