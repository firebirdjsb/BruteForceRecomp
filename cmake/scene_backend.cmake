include("${PROJECT_SOURCE_DIR}/cmake/upscaler_state.cmake")
bf_add_upscaler_state()
if(NOT BF_EXPERIMENTAL_NV2A_D3D11)
    return()
endif()
if(NOT WIN32)
    message(FATAL_ERROR "The experimental D3D11 scene backend is Windows-only; retain the CPU path here.")
endif()
find_package(Git REQUIRED)
set(input "${CMAKE_BINARY_DIR}/runtime-fixes/nv2a_pb_exec.c")
file(READ "${input}" source)
string(REPLACE "\r\n" "\n" source "${source}")
string(SHA256 digest "${source}")
if(NOT digest STREQUAL "e439de1f2e5ae9284d4dddfe5b48da64c6f68b4c158394bfe3d7620a5073d265")
    message(FATAL_ERROR "Unaudited private executor; experimental draw connection refused.")
endif()
set(overlay "${CMAKE_BINARY_DIR}/runtime-fixes/scene-backend")
file(MAKE_DIRECTORY "${overlay}")
file(WRITE "${overlay}/nv2a_pb_exec.c" "${source}")
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
    "--directory=${overlay}" "${PROJECT_SOURCE_DIR}/patches/nv2a-d3d11/executor.patch"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot apply audited NV2A draw overlay: ${error}")
endif()
file(READ "${overlay}/nv2a_pb_exec.c" connected)
set(ids_anchor "    vb.nverts = nv;")
string(FIND "${connected}" "${ids_anchor}" ids_found)
if(ids_found LESS 0)
    message(FATAL_ERROR "Missing original-vertex identity handoff.")
endif()
string(REPLACE "${ids_anchor}" "${ids_anchor}\n    vb.vertex_ids = s_xvs_uidx; /* exact original indices, synchronous callback */\n    vb.command_va = s_motion_primitive_va;" connected "${connected}")
set(origin_storage "static uint32_t *s_xvs_map, *s_xvs_tri, *s_xvs_uidx;")
set(origin_begin "    case NV097_SET_BEGIN_END:\n        if (param) {")
foreach(origin IN ITEMS "${origin_storage}" "${origin_begin}")
    string(FIND "${connected}" "${origin}" found)
    if(found LESS 0)
        message(FATAL_ERROR "Missing original primitive-origin boundary.")
    endif()
endforeach()
string(REPLACE "${origin_storage}" "${origin_storage}\nstatic uint32_t s_motion_primitive_va;" connected "${connected}")
string(REPLACE "${origin_begin}" "${origin_begin}\n            s_motion_primitive_va = bf_motion_parameter_va; /* retain BEGIN origin through END/state uploads */" connected "${connected}")
string(REPLACE "clock_ok && !nv2a_pb_scan_in_call()"
    "clock_ok && (nv2a_pb_dma_walk() || !nv2a_pb_scan_in_call())" connected "${connected}")
set(present_anchor "                xbox_FramebufferWindowPresent(dma_resolve(done), pitch);")
string(FIND "${connected}" "${present_anchor}" present_found)
if(present_found LESS 0)
    message(FATAL_ERROR "Missing authenticated completed-source publication.")
endif()
string(REPLACE "${present_anchor}"
    "                if(backend_draws()&&s_xbackend&&s_xbackend->prepare_present)\n                    s_xbackend->prepare_present(dma_resolve(done));\n${present_anchor}" connected "${connected}")
file(WRITE "${overlay}/nv2a_pb_exec.c.in" "${connected}")
configure_file("${overlay}/nv2a_pb_exec.c.in" "${overlay}/nv2a_pb_exec.c" COPYONLY)
get_target_property(sources xbox_kernel SOURCES)
list(REMOVE_ITEM sources "${input}" "${XBOXRECOMP_DIR}/src/kernel/nv2a_vsh_interp.c")
list(APPEND sources "${overlay}/nv2a_pb_exec.c" "${PROJECT_SOURCE_DIR}/src/gpu/runtime/nv2a_vsh_interp.c")
set_property(TARGET xbox_kernel PROPERTY SOURCES "${sources}")

if(BF_RUNTIME_CHECKS_ONLY)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_library(bf_scene_test_ui STATIC src/ui/host_ui.cpp src/ui/ui_draw.cpp)
    target_link_libraries(bf_scene_test_ui PUBLIC bf_aspect_state bf_upscaler_state)
    add_executable(bf_hud_anchor_test tests/hud_anchor_test.cpp src/ui/hud_anchor.cpp)
    target_include_directories(bf_hud_anchor_test PRIVATE src "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
    target_link_libraries(bf_hud_anchor_test PRIVATE bf_scene_test_ui)
    add_test(NAME hud_widget_anchors COMMAND bf_hud_anchor_test)
    add_test(NAME hud_owner_trace COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/check_hud_owner_trace.py" "$<TARGET_FILE:bf_hud_anchor_test>")
    add_executable(bf_gpu_scene_test tests/gpu_scene_test.c tests/gpu_scene_textures.c)
    target_include_directories(bf_gpu_scene_test PRIVATE src src/gpu src/gpu/runtime
        "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
    target_compile_definitions(bf_gpu_scene_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(bf_gpu_scene_test PRIVATE xboxrecomp bf_scene_backend bf_scene_test_ui)
    bf_add_ui_composition_backend_tests()
    add_test(NAME motion_disabled_clear_parity COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/check_motion_backend_off.py" "$<TARGET_FILE:bf_gpu_scene_test>"
        "${XBOXRECOMP_DIR}/tools/conformance/test.xbe" "${CMAKE_BINARY_DIR}/motion-disabled-clear")
    set_tests_properties(motion_disabled_clear_parity PROPERTIES TIMEOUT 120)
    add_test(NAME nv2a_gpu_scene_parity COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/check_gpu_scene.py" "$<TARGET_FILE:bf_gpu_scene_test>"
        "${XBOXRECOMP_DIR}/tools/conformance/test.xbe" "${CMAKE_BINARY_DIR}/gpu-parity")
    add_test(NAME nv2a_d3d12_scene_parity COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/check_gpu_scene.py" "$<TARGET_FILE:bf_gpu_scene_test>"
        "${XBOXRECOMP_DIR}/tools/conformance/test.xbe" "${CMAKE_BINARY_DIR}/gpu-parity-d3d12" --d3d12)
    add_test(NAME scene_aa_ui_preservation COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/check_scene_aa.py" "$<TARGET_FILE:bf_gpu_scene_test>"
        "${XBOXRECOMP_DIR}/tools/conformance/test.xbe" "${CMAKE_BINARY_DIR}/scene-aa-ui")
    foreach(snapshot_api IN ITEMS d3d11 d3d12)
        add_test(NAME ordered_scene_snapshot_${snapshot_api} COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/check_scene_snapshot.py" "$<TARGET_FILE:bf_gpu_scene_test>"
            "${XBOXRECOMP_DIR}/tools/conformance/test.xbe" "${CMAKE_BINARY_DIR}/scene-snapshot-${snapshot_api}" ${snapshot_api})
        set_tests_properties(ordered_scene_snapshot_${snapshot_api} PROPERTIES TIMEOUT 90)
        add_test(NAME depth_probe_${snapshot_api} COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/check_depth_probe.py" "$<TARGET_FILE:bf_gpu_scene_test>"
            "$<TARGET_FILE:bf_scene_snapshot_test>" "${XBOXRECOMP_DIR}/tools/conformance/test.xbe"
            "${CMAKE_BINARY_DIR}/depth-probe-${snapshot_api}" ${snapshot_api})
        set_tests_properties(depth_probe_${snapshot_api} PROPERTIES TIMEOUT 90)
        add_test(NAME ordered_scene_aspect_${snapshot_api} COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/check_scene_aspect.py" "$<TARGET_FILE:bf_gpu_scene_test>"
            "${XBOXRECOMP_DIR}/tools/conformance/test.xbe" "${CMAKE_BINARY_DIR}/scene-aspect-${snapshot_api}" ${snapshot_api})
        set_tests_properties(ordered_scene_aspect_${snapshot_api} PROPERTIES TIMEOUT 90)
    endforeach()
    foreach(api IN ITEMS d3d11 d3d12)
        set(profile_api)
        if(api STREQUAL "d3d12")
            set(profile_api --d3d12)
        endif()
        add_test(NAME texture_profile_${api} COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/check_texture_profile.py" "$<TARGET_FILE:bf_gpu_scene_test>"
            "${XBOXRECOMP_DIR}/tools/conformance/test.xbe" "${CMAKE_BINARY_DIR}/texture-profile-${api}" ${profile_api})
        set_tests_properties(texture_profile_${api} PROPERTIES TIMEOUT 120)
    endforeach()
endif()
target_include_directories(xbox_kernel BEFORE PRIVATE "${PROJECT_SOURCE_DIR}/src/gpu/runtime" "${PROJECT_SOURCE_DIR}/src/gpu")
target_include_directories(bf_nv2a_hlsl BEFORE PUBLIC "${PROJECT_SOURCE_DIR}/src/gpu/runtime")
bf_add_scene_device()
include("${PROJECT_SOURCE_DIR}/cmake/motion_renderer.cmake")
bf_add_motion_renderer()
add_library(bf_scene_backend STATIC src/gpu/d3d11_backend.c src/gpu/texture_decode.c src/gpu/asset_upscale.cpp src/gpu/d3d11_device_status.c)
target_include_directories(bf_scene_backend PRIVATE src src/gpu src/gpu/runtime
    "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
target_compile_definitions(bf_scene_backend PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(bf_scene_backend PUBLIC bf_aspect_state bf_scene_snapshot bf_scene_device bf_game_temporal bf_motion_renderer bf_ui_composition bf_nv2a_hlsl bf_nv2a_pscache d3d11 d3d12 dxgi d3dcompiler)

# The asynchronous visibility callback must also progress while the guest is
# waiting and is not publishing a new PUT. No USB/event/IRQ source is replaced.
get_target_property(sources xbox_kernel SOURCES)
foreach(file IN LISTS sources)
    get_filename_component(name "${file}" NAME)
    if(name STREQUAL "xbox_memory_layout.c")
        file(READ "${file}" text)
        string(PREPEND text "extern int nv2a_pb_dma_walk(void);\n")
        string(REPLACE "&& (put != last_put || (now_ms - last_put_ms) > 2000)"
            "&& (put != last_put || (now_ms - last_put_ms) > 2000 || *(volatile uint32_t *)((char *)regs + NV2A_USER_DMA_GET) != put)"
            text "${text}")
        string(REPLACE "if (put != last_put)\n                            nv2a_pb_scan(put);"
            "if (put != last_put || get_now != put)\n                            nv2a_pb_scan(put);" text "${text}")
        string(REPLACE "*get = put;\n                    get_written = put;"
            "if (!nv2a_pb_dma_walk()) *get = put;\n                    get_written = *get;" text "${text}")
        string(REPLACE "        dsp_ack_tick();"
            "        { extern void nv2a_pb_exec_poll(void); nv2a_pb_exec_poll(); }\n        dsp_ack_tick();" text "${text}")
        set(copy "${overlay}/xbox_memory_layout.c")
        file(WRITE "${copy}.in" "${text}")
        configure_file("${copy}.in" "${copy}" COPYONLY)
        list(REMOVE_ITEM sources "${file}")
        list(APPEND sources "${copy}")
        break()
    endif()
endforeach()
set_property(TARGET xbox_kernel PROPERTY SOURCES "${sources}")
