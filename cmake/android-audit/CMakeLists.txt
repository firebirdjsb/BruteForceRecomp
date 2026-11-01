cmake_minimum_required(VERSION 3.20)
project(BruteForceAndroidTranslationAudit LANGUAGES C)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This audit requires the Android NDK toolchain and arm64-v8a.")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(BF_GENERATED_DIR "" CACHE PATH "Private generated C sources")
set(BF_RECOVERED_DIR "" CACHE PATH "Private recovered callback sources")
set(XBOXRECOMP_DIR "" CACHE PATH "Pinned public runtime")
if(NOT EXISTS "${BF_GENERATED_DIR}/recomp_dispatch.c" OR
   NOT EXISTS "${BF_RECOVERED_DIR}/recomp_recovered.c" OR
   NOT EXISTS "${XBOXRECOMP_DIR}/templates/runtime/recomp_types.h")
    message(FATAL_ERROR "Supply the existing private generated/recovered directories and pinned toolkit.")
endif()
file(GLOB translated CONFIGURE_DEPENDS "${BF_GENERATED_DIR}/*.c")
add_library(bf_android_translation_audit OBJECT ${translated} "${BF_RECOVERED_DIR}/recomp_recovered.c")
target_include_directories(bf_android_translation_audit PRIVATE
    "${BF_GENERATED_DIR}" "${BF_GENERATED_DIR}/.." "${XBOXRECOMP_DIR}/templates/runtime")
target_compile_options(bf_android_translation_audit PRIVATE -fno-fast-math -ffp-contract=off
    -Werror=implicit-function-declaration)
message(STATUS "Compiling all translated functions as Android ARM64 objects. This audit does not link a runtime or produce a playable app.")
