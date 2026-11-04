# Preserve the pinned toolkit. Option 6 uses a different bit layout from the
# low-bit AV capability query; retail D3D filters its mode table with both.
set(video_hal "${XBOXRECOMP_DIR}/src/kernel/kernel_hal.c")
file(READ "${video_hal}" video_source)
string(REPLACE "\r\n" "\n" video_source "${video_source}")
string(SHA256 video_hash "${video_source}")
if(NOT video_hash STREQUAL "259001754a96ce0c20678f4722f2af23eeb5d03fddd2ea62dc6fa93f2f2babb1")
    message(FATAL_ERROR "Unaudited AV encoder implementation; HD capability fix refused.")
endif()
set(video_anchor "                | AV_REFRESH_60Hz;")
string(REGEX MATCHALL "\\| AV_REFRESH_60Hz" video_matches "${video_source}")
list(LENGTH video_matches video_count)
if(NOT video_count EQUAL 1)
    message(FATAL_ERROR "Expected one option-6 AV capability response.")
endif()
string(REPLACE "${video_anchor}"
    "                | AV_REFRESH_60Hz\n                /* Option-6 mode flags: 480p bit19, 720p bit17.\n                 * Keep pack/standard/refresh and the separate low-bit query. */\n                | 0x00080000u | 0x00020000u;"
    video_source "${video_source}")
set(video_copy "${CMAKE_BINARY_DIR}/runtime-fixes/video/kernel_hal.c")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime-fixes/video")
file(WRITE "${video_copy}.in" "${video_source}")
configure_file("${video_copy}.in" "${video_copy}" COPYONLY)
get_target_property(video_items xbox_kernel SOURCES)
get_target_property(video_directory xbox_kernel SOURCE_DIR)
set(video_replaced)
foreach(item IN LISTS video_items)
    get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${video_directory}")
    if(absolute STREQUAL video_hal)
        list(APPEND video_replaced "${video_copy}")
    else()
        list(APPEND video_replaced "${absolute}")
    endif()
endforeach()
set_property(TARGET xbox_kernel PROPERTY SOURCES "${video_replaced}")
# Compile the real, patched AV function for the focused public regression.
# Only its logging is silenced; all option dispatch and result writes are real.
string(FIND "${video_source}" "VOID __stdcall xbox_AvSendTVEncoderOption(" first)
string(FIND "${video_source}" "VOID __stdcall xbox_AvSetSavedDataAddress(" last)
if(first LESS 0 OR last LESS_EQUAL first)
    message(FATAL_ERROR "Expected bounded AV encoder function.")
endif()
math(EXPR length "${last}-${first}")
string(SUBSTRING "${video_source}" ${first} ${length} video_function)
set(video_fixture "#include \"kernel.h\"\n#define xbox_log(...) ((void)0)\nstatic ULONG g_av_display_mode;\n${video_function}")
file(WRITE "${CMAKE_BINARY_DIR}/runtime-fixes/video/av-fixture.c.in" "${video_fixture}")
configure_file("${CMAKE_BINARY_DIR}/runtime-fixes/video/av-fixture.c.in"
    "${CMAKE_BINARY_DIR}/runtime-fixes/video/av-fixture.c" COPYONLY)

function(bf_add_video_capability_checks)
    add_executable(bf_video_capabilities_test "${PROJECT_SOURCE_DIR}/tests/video_capabilities_test.c"
        "${CMAKE_BINARY_DIR}/runtime-fixes/video/av-fixture.c")
    target_include_directories(bf_video_capabilities_test PRIVATE "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
    add_test(NAME video_capabilities COMMAND bf_video_capabilities_test)
    set(BF_VIDEO_MODE_FIXTURE_XBE "" CACHE FILEPATH "Optional authenticated local XBE for original mode-selector regression")
    if(BF_VIDEO_MODE_FIXTURE_XBE AND NOT BF_RUNTIME_CHECKS_ONLY)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/prepare_video_mode_fixture.py"
            --xbe "${BF_VIDEO_MODE_FIXTURE_XBE}" --generated "${BF_GENERATED_DIR}"
            --output "${CMAKE_BINARY_DIR}/runtime-fixes/video/retail-mode-fixture.c"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Cannot authenticate original video fixture: ${error}")
        endif()
        add_executable(bf_video_modes_test "${PROJECT_SOURCE_DIR}/tests/video_modes_test.c"
            "${CMAKE_BINARY_DIR}/runtime-fixes/video/av-fixture.c"
            "${CMAKE_BINARY_DIR}/runtime-fixes/video/retail-mode-fixture.c")
        target_include_directories(bf_video_modes_test PRIVATE "${BF_GENERATED_DIR}" "${XBOXRECOMP_DIR}/templates/runtime"
            "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
        add_test(NAME retail_video_modes COMMAND bf_video_modes_test)
    endif()
endfunction()
