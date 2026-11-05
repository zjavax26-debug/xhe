cmake_minimum_required(VERSION 3.22.1)
project(xhe_audio_converter C)

if(NOT DEFINED LIBXAAC_SOURCE_DIR OR NOT EXISTS "${LIBXAAC_SOURCE_DIR}/encoder/libxaacenc.cmake")
    message(FATAL_ERROR "LIBXAAC_SOURCE_DIR must point to a checked-out libxaac source tree")
endif()

set(XAAC_ROOT "${LIBXAAC_SOURCE_DIR}")
set(XAAC_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/libxaac-config")

include("${XAAC_ROOT}/cmake/utils.cmake")
libxaac_add_compile_options()
libxaac_add_definitions()

include("${XAAC_ROOT}/common/common.cmake")
include("${XAAC_ROOT}/encoder/libxaacenc.cmake")

add_library(xhe_jni SHARED jni_bridge.c)
target_include_directories(xhe_jni PRIVATE
    "${XAAC_ROOT}/encoder"
    "${XAAC_ROOT}/common"
    "${XAAC_ROOT}/encoder/drc_src"
)
find_library(android_log log)
target_link_libraries(xhe_jni PRIVATE libxaacenc ${android_log} m)
