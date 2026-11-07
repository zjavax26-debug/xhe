cmake_minimum_required(VERSION 3.22.1)
project(xhe_audio_converter C CXX)

if(NOT DEFINED LIBXAAC_SOURCE_DIR OR NOT EXISTS "${LIBXAAC_SOURCE_DIR}/encoder/libxaacenc.cmake")
    message(FATAL_ERROR "LIBXAAC_SOURCE_DIR must point to a checked-out libxaac source tree")
endif()
if(NOT DEFINED EXHALE_SOURCE_DIR OR NOT EXISTS "${EXHALE_SOURCE_DIR}/include/exhaleDecl.h")
    message(FATAL_ERROR "EXHALE_SOURCE_DIR must point to the pinned Exhale source tree")
endif()

set(XAAC_ROOT "${LIBXAAC_SOURCE_DIR}")
include("${XAAC_ROOT}/cmake/utils.cmake")
libxaac_add_compile_options()
# libxaac applies -std=gnu99 globally; restrict it to C sources only.
get_property(XAAC_COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)
list(REMOVE_ITEM XAAC_COMPILE_OPTIONS "-std=gnu99")
list(APPEND XAAC_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C>:-std=gnu99>")
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${XAAC_COMPILE_OPTIONS}")
libxaac_add_definitions()
include("${XAAC_ROOT}/common/common.cmake")
include("${XAAC_ROOT}/encoder/libxaacenc.cmake")

add_subdirectory("${EXHALE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/exhale-build" EXCLUDE_FROM_ALL)
# Android needs only the encoder library; never build Exhale's desktop WAV/MP4 CLI.
if(TARGET exhaleApp)
    set_target_properties(exhaleApp PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
# Exhale declares its PCH PUBLIC; prevent it from propagating to JNI consumers.
set_property(TARGET exhaleLib PROPERTY INTERFACE_PRECOMPILE_HEADERS "")

add_library(xhe_jni SHARED jni_bridge.c usac_encoder.c)
target_include_directories(xhe_jni PRIVATE
    "${XAAC_ROOT}/encoder"
    "${XAAC_ROOT}/common"
    "${XAAC_ROOT}/encoder/drc_src"
)
find_library(android_log log)
target_link_libraries(xhe_jni PRIVATE libxaacenc ${android_log} m)

add_library(exhale_jni SHARED exhale_jni.cpp)
target_include_directories(exhale_jni PRIVATE "${EXHALE_SOURCE_DIR}/include")
find_package(Threads REQUIRED)
target_link_libraries(exhale_jni PRIVATE exhaleLib Threads::Threads ${android_log})
