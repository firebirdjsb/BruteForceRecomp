# Authenticated title-only camera integration. Include after other generated
# adapters and immediately before add_executable(bruteforce_recomp ...).
if(BF_RUNTIME_CHECKS_ONLY OR NOT WIN32)
    return()
endif()
get_filename_component(BF_ASPECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(BF_ASPECT_OUTPUT "${CMAKE_BINARY_DIR}/runtime-fixes/native-aspect")
file(REAL_PATH "${BF_GENERATED_DIR}/recomp_0023.c" BF_ASPECT_CANONICAL)
foreach(auth IN ITEMS
    "recomp_0022.c|7a8ab670472c875ffb5f546105979b24e47853823b6619d9566764785f96beee"
    "recomp_0023.c|fb2a07684f5526f8277e1d07e2215902b110944d5434f4e0f91b64b3732180a1"
    "recomp_types.h|1bd7870381e3ab354646efbf3241389223488401db479ce048c557c20b79cc2d")
    string(REPLACE "|" ";" auth_parts "${auth}")
    list(GET auth_parts 0 auth_name)
    list(GET auth_parts 1 auth_expected)
    file(READ "${BF_GENERATED_DIR}/${auth_name}" auth_source)
    string(REPLACE "\r\n" "\n" auth_source "${auth_source}")
    string(SHA256 auth_hash "${auth_source}")
    if(NOT auth_hash STREQUAL auth_expected)
        message(FATAL_ERROR "Unaudited native aspect guest/helper layout: ${auth_name}")
    endif()
    if(auth_name STREQUAL "recomp_0023.c")
        set(BF_ASPECT_ORIGINAL "${auth_source}")
    endif()
endforeach()

# Match the unchanged original call frame, including each argument PUSH and
# original return-address DWORD. Replace only the callee, at exactly two sites.
set(BF_ASPECT_PATCHED "${BF_ASPECT_ORIGINAL}")
foreach(branch IN ITEMS "edx|0006FCE5" "ecx|0006FE8B")
    string(REPLACE "|" ";" branch_parts "${branch}")
    list(GET branch_parts 0 context_register)
    list(GET branch_parts 1 return_address)
    set(anchor "    PUSH32(esp, eax);\n    PUSH32(esp, ${context_register});\n    PUSH32(esp, 0x${return_address}u); RECOMP_ABI_CALL(0x000D9F00u, sub_000D9F00); /* call 0x000D9F00 */")
    string(FIND "${BF_ASPECT_PATCHED}" "${anchor}" first)
    if(first LESS 0)
        message(FATAL_ERROR "Missing native aspect dispatch anchor ${return_address}")
    endif()
    string(LENGTH "${anchor}" anchor_length)
    math(EXPR tail_start "${first} + ${anchor_length}")
    string(SUBSTRING "${BF_ASPECT_PATCHED}" ${tail_start} -1 tail)
    string(FIND "${tail}" "${anchor}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "Nonunique native aspect dispatch anchor ${return_address}")
    endif()
    string(REPLACE "sub_000D9F00" "bf_native_aspect_dispatch" replacement "${anchor}")
    string(REPLACE "${anchor}" "${replacement}" BF_ASPECT_PATCHED "${BF_ASPECT_PATCHED}")
endforeach()
string(REPLACE "#include <math.h>\n" "#include <math.h>\nstatic void bf_native_aspect_dispatch(void);\n"
    BF_ASPECT_PATCHED "${BF_ASPECT_PATCHED}")
file(READ "${BF_ASPECT_ROOT}/src/native_aspect_guest.inc" BF_ASPECT_WRAPPER)
string(REPLACE "\r\n" "\n" BF_ASPECT_WRAPPER "${BF_ASPECT_WRAPPER}")
string(APPEND BF_ASPECT_PATCHED "\n${BF_ASPECT_WRAPPER}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BF_ASPECT_ROOT}/src/native_aspect_guest.inc"
    "${BF_GENERATED_DIR}/recomp_0022.c" "${BF_GENERATED_DIR}/recomp_0023.c" "${BF_GENERATED_DIR}/recomp_types.h")

set(BF_ASPECT_SOURCES)
set(BF_ASPECT_MATCHES 0)
set(BF_ASPECT_COPY "${BF_ASPECT_OUTPUT}/recomp_0023.c")
foreach(source IN LISTS BF_GENERATED)
    file(REAL_PATH "${source}" source_real BASE_DIRECTORY "${BF_ASPECT_ROOT}")
    if(source_real STREQUAL BF_ASPECT_CANONICAL)
        math(EXPR BF_ASPECT_MATCHES "${BF_ASPECT_MATCHES}+1")
        list(APPEND BF_ASPECT_SOURCES "${BF_ASPECT_COPY}")
    else()
        list(APPEND BF_ASPECT_SOURCES "${source}")
    endif()
endforeach()
if(NOT BF_ASPECT_MATCHES EQUAL 1)
    message(FATAL_ERROR "Expected exactly one canonical chunk 0023 for native aspect; got ${BF_ASPECT_MATCHES}")
endif()
file(MAKE_DIRECTORY "${BF_ASPECT_OUTPUT}")
file(WRITE "${BF_ASPECT_COPY}.in" "${BF_ASPECT_PATCHED}")
configure_file("${BF_ASPECT_COPY}.in" "${BF_ASPECT_COPY}" COPYONLY)
list(APPEND BF_ASPECT_SOURCES "${BF_ASPECT_ROOT}/src/graphics/native_aspect.cpp")
set(BF_GENERATED "${BF_ASPECT_SOURCES}")
# Source-local include paths preserve compilation flags for every other chunk.
set_source_files_properties("${BF_ASPECT_COPY}" PROPERTIES INCLUDE_DIRECTORIES "${BF_ASPECT_ROOT}/src")
set_source_files_properties("${BF_ASPECT_ROOT}/src/graphics/native_aspect.cpp" PROPERTIES
    INCLUDE_DIRECTORIES "${XBOXRECOMP_DIR}/src/kernel;${XBOXRECOMP_DIR}/src")

# Retail translated bodies/constants are generated only in the ignored build
# tree. This fixture is optional and requires the exact supported owned XBE.
if(BF_VIDEO_MODE_FIXTURE_XBE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${BF_ASPECT_ROOT}/scripts/prepare_native_aspect_fixture.py" "${BF_VIDEO_MODE_FIXTURE_XBE}")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${BF_ASPECT_ROOT}/scripts/prepare_native_aspect_fixture.py"
        --generated-dir "${BF_GENERATED_DIR}" --xbe "${BF_VIDEO_MODE_FIXTURE_XBE}" --output "${BF_ASPECT_OUTPUT}/fixture"
        RESULT_VARIABLE fixture_result ERROR_VARIABLE fixture_error)
    if(NOT fixture_result EQUAL 0)
        message(FATAL_ERROR "Native aspect original-helper fixture preparation failed: ${fixture_error}")
    endif()
    add_executable(bf_native_aspect_fixture "${BF_ASPECT_ROOT}/tests/native_aspect_fixture.c"
        "${BF_ASPECT_ROOT}/src/graphics/native_aspect.cpp")
    target_include_directories(bf_native_aspect_fixture PRIVATE "${BF_GENERATED_DIR}"
        "${BF_ASPECT_ROOT}/src" "${BF_ASPECT_OUTPUT}/fixture")
    target_link_libraries(bf_native_aspect_fixture PRIVATE bf_aspect_state)
    add_test(NAME native_aspect_original_helpers COMMAND bf_native_aspect_fixture)
    set_tests_properties(native_aspect_original_helpers PROPERTIES TIMEOUT 15)
endif()
