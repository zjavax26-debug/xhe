cmake_minimum_required(VERSION 3.22.1)
project(exhale_jni_probe LANGUAGES CXX)

if(NOT DEFINED EXHALE_SOURCE_DIR OR NOT EXISTS "${EXHALE_SOURCE_DIR}/include/exhaleDecl.h")
    message(FATAL_ERROR "EXHALE_SOURCE_DIR must point to the pinned Exhale source tree")
endif()

add_library(exhale_jni SHARED exhale_probe.cpp)
target_include_directories(exhale_jni PRIVATE "${EXHALE_SOURCE_DIR}/include")
target_link_libraries(exhale_jni PRIVATE "${EXHALE_SOURCE_DIR}/../build/exhale-android-arm64/src/lib/libexhale.a")
