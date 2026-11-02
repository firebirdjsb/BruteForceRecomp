cmake_minimum_required(VERSION 3.20)
project(BruteForceRecomp LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # Upstream runtime targets explicitly use /O2, which conflicts with the
    # default MSVC Debug /RTC1 instrumentation. Keep symbols and /Od for our
    # generated code while allowing those runtime targets to compile.
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()
set(XBOXRECOMP_DIR "" CACHE PATH "Pinned xboxrecomp checkout")
set(BF_GENERATED_DIR "" CACHE PATH "Private generated C source directory")
set(BF_RECOVERED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.local/recovered" CACHE PATH "Private recovered callback source directory")
if(ANDROID)
    message(FATAL_ERROR "Android is not integrated yet. See platform-references.json for the public Vulkan/SDL2 runtime candidate and README.md for the app-lifecycle and device validation gates.")
endif()
if(NOT EXISTS "${XBOXRECOMP_DIR}/CMakeLists.txt" OR NOT EXISTS "${BF_GENERATED_DIR}/recomp_dispatch.c")
    message(FATAL_ERROR "Run scripts/pipeline.py generate first, then set XBOXRECOMP_DIR and BF_GENERATED_DIR.")
endif()
if(NOT EXISTS "${BF_GENERATED_DIR}/native-media-profile.json")
    message(FATAL_ERROR "Prepare the user-approved native media profile with scripts/prepare_native_media.py before building.")
endif()
add_subdirectory("${XBOXRECOMP_DIR}" "${CMAKE_BINARY_DIR}/xboxrecomp")
include(cmake/runtime_fixes.cmake)
bf_apply_runtime_fixes("${XBOXRECOMP_DIR}")
# Opt-in CSV telemetry is compiled into the emulator but does no hashing or
# file writes unless RECOMP_FRAME_STATS is explicitly set.
target_sources(xbox_kernel PRIVATE src/gpu/nv2a_frame_stats.c)
target_include_directories(xbox_kernel PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu")
include(cmake/posix_runtime.cmake)
enable_testing()
add_executable(bf_import_guard_test tests/import_guard_test.cpp)
target_include_directories(bf_import_guard_test PRIVATE src)
add_test(NAME import_guard COMMAND bf_import_guard_test)
add_executable(bf_texture_math_test tests/texture_math_test.c)
target_include_directories(bf_texture_math_test PRIVATE src)
add_test(NAME texture_math COMMAND bf_texture_math_test)
add_executable(bf_render_scale_test tests/render_scale_policy_test.c)
target_include_directories(bf_render_scale_test PRIVATE src)
if(NOT WIN32)
    target_link_libraries(bf_render_scale_test PRIVATE m)
endif()
add_test(NAME render_scale COMMAND bf_render_scale_test)
add_executable(bf_frame_stats_test tests/frame_stats_test.c src/gpu/nv2a_frame_stats.c)
target_include_directories(bf_frame_stats_test PRIVATE src/gpu)
add_test(NAME frame_stats COMMAND bf_frame_stats_test)
# P8 palette unswizzle is shared, independent of D3D11/D3D12 host APIs.
add_library(bf_nv2a_p8 STATIC src/gpu/nv2a_p8_decode.c)
target_include_directories(bf_nv2a_p8 PUBLIC src "${XBOXRECOMP_DIR}/src")
add_executable(bf_nv2a_p8_test tests/nv2a_p8_decode_test.c)
target_link_libraries(bf_nv2a_p8_test PRIVATE bf_nv2a_p8)
add_test(NAME nv2a_p8_decode COMMAND bf_nv2a_p8_test)
# Pre-register the upstream NV2A combiner / vertex HLSL emitters as a
# testable shared renderer component. They are not plugged into guest draw
# dispatch until original-CPU and DX11/DX12 frame parity is established.
add_library(bf_nv2a_hlsl STATIC src/gpu/rc_hlsl.c src/gpu/vsh_hlsl.c)
target_include_directories(bf_nv2a_hlsl PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu"
    "${XBOXRECOMP_DIR}/src/kernel")
add_executable(bf_nv2a_hlsl_test tests/nv2a_hlsl_test.c)
target_link_libraries(bf_nv2a_hlsl_test PRIVATE bf_nv2a_hlsl)
if(WIN32)
    target_compile_definitions(bf_nv2a_hlsl_test PRIVATE COBJMACROS)
    target_link_libraries(bf_nv2a_hlsl_test PRIVATE d3dcompiler)
endif()
add_test(NAME nv2a_hlsl COMMAND bf_nv2a_hlsl_test)
if(WIN32)
    # Compile the original APU voice-processor test harness against our
    # patched private runtime (not against unmodified public source).
    add_executable(bf_apu_hrtf_test tests/apu_vp_hrtf_test.c)
    target_include_directories(bf_apu_hrtf_test PRIVATE
        "${CMAKE_BINARY_DIR}/runtime-fixes"
        "${XBOXRECOMP_DIR}/src"
        "${XBOXRECOMP_DIR}/src/apu"
        "${XBOXRECOMP_DIR}/src/nv2a")
    target_compile_definitions(bf_apu_hrtf_test PRIVATE _USE_MATH_DEFINES)
    add_test(NAME apu_mono_hrtf COMMAND bf_apu_hrtf_test)
    # Windows-only staged persistent DXBC cache, with SHA-256 provenance
    # and atomic publication. Not yet connected to game renderer compilation.
    add_library(bf_nv2a_pscache STATIC src/gpu/shader_cache.c)
    target_include_directories(bf_nv2a_pscache PUBLIC src/gpu)
    target_link_libraries(bf_nv2a_pscache PRIVATE bcrypt)
    add_executable(bf_pscache_test tests/shader_cache_test.c)
    target_include_directories(bf_pscache_test PRIVATE src)
    target_link_libraries(bf_pscache_test PRIVATE bf_nv2a_pscache)
    add_test(NAME shader_cache COMMAND bf_pscache_test)
    set_tests_properties(shader_cache PROPERTIES SKIP_RETURN_CODE 77)
endif()
# This GPU completion state machine is API-neutral and has no game dependencies.
add_executable(bf_gpu_timeline_test tests/gpu_completion_timeline_test.cpp)
target_include_directories(bf_gpu_timeline_test PRIVATE src)
add_test(NAME gpu_timeline COMMAND bf_gpu_timeline_test)
if(WIN32)
    # Optional Direct3D 12 WARP / D3D11On12 interoperability smoke. This does
    # not activate a D3D12 game renderer or change the default presentation.
    add_executable(bf_d3d12_compat_test tests/d3d12_fence_bridge_test.cpp)
    target_include_directories(bf_d3d12_compat_test PRIVATE src)
    target_link_libraries(bf_d3d12_compat_test PRIVATE d3d12 d3d11 dxgi)
    add_test(NAME d3d12_compat COMMAND bf_d3d12_compat_test)
    set_tests_properties(d3d12_compat PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(bf_d3d12_texture_test tests/d3d12_texture_roundtrip_test.cpp)
    target_include_directories(bf_d3d12_texture_test PRIVATE src)
    target_link_libraries(bf_d3d12_texture_test PRIVATE d3d12 dxgi)
    add_test(NAME d3d12_texture COMMAND bf_d3d12_texture_test)
    set_tests_properties(d3d12_texture PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    add_executable(bf_posix_mmio_test tests/posix_mmio_test.c)
    add_test(NAME posix_mmio_context COMMAND bf_posix_mmio_test)
endif()
file(GLOB BF_GENERATED CONFIGURE_DEPENDS "${BF_GENERATED_DIR}/*.c")
add_executable(bruteforce_recomp src/main.cpp src/boot_trace.cpp src/file_hash.cpp
    src/native_media.cpp src/instruction_services.cpp src/startup_sampler.cpp
    src/gpu_completion.cpp src/host_input.cpp "${BF_RECOVERED_DIR}/recomp_recovered.c" ${BF_GENERATED})
target_include_directories(bruteforce_recomp PRIVATE
    "${BF_GENERATED_DIR}" "${BF_GENERATED_DIR}/.."
    "${XBOXRECOMP_DIR}/templates/runtime" "${XBOXRECOMP_DIR}/include")
target_link_libraries(bruteforce_recomp PRIVATE xboxrecomp)
if(WIN32)
    target_link_libraries(bruteforce_recomp PRIVATE dbghelp)
endif()
if(MSVC)
    target_compile_options(bruteforce_recomp PRIVATE /bigobj /fp:strict)
else()
    target_compile_options(bruteforce_recomp PRIVATE -fno-fast-math -ffp-contract=off)
endif()
