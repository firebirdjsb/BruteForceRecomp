# Optional official AMD SDK. The default build remains independent of its DLLs.
set(BF_FSR_SDK_DIR "" CACHE PATH "AMD FSR SDK 2.4.0 checkout (optional)")
function(bf_attach_fsr_runtime target)
    # Keep the ordinary build independent of optional SDK targets and files.
    if(NOT BF_FSR_SDK_DIR)
        return()
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot attach AMD FSR runtime: target '${target}' does not exist.")
    endif()
    get_target_property(attached "${target}" BF_FSR_RUNTIME_ATTACHED)
    if(attached)
        return()
    endif()
    if(NOT TARGET bf_fsr_temporal)
        bf_add_fsr_temporal()
    endif()
    if(NOT TARGET bf_fsr_temporal)
        message(FATAL_ERROR "Cannot attach AMD FSR runtime: bf_fsr_temporal was not created.")
    endif()
    target_link_libraries("${target}" PRIVATE bf_fsr_temporal)
    target_compile_definitions("${target}" PRIVATE BF_HAS_FSR_TEMPORAL=1)
    set(kit "${BF_FSR_SDK_DIR}/Kits/FidelityFX")
    foreach(dll loader upscaler framegeneration)
        add_custom_command(TARGET "${target}" POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${kit}/signedbin/amd_fidelityfx_${dll}_dx12.dll"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endforeach()
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${kit}/docs/license.md"
            "$<TARGET_FILE_DIR:${target}>/AMD-FSR-SDK-LICENSE.md"
        VERBATIM)
    set_property(TARGET "${target}" PROPERTY BF_FSR_RUNTIME_ATTACHED TRUE)
endfunction()

function(bf_add_fsr_temporal)
    if(NOT BF_FSR_SDK_DIR OR TARGET bf_fsr_temporal)
        return()
    endif()
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The optional signed FSR adapter currently requires Windows x64/DX12.")
    endif()
    set(kit "${BF_FSR_SDK_DIR}/Kits/FidelityFX")
    foreach(file docs/license.md api/include/ffx_api.h upscalers/include/ffx_upscale.h framegeneration/include/ffx_framegeneration.h
        signedbin/amd_fidelityfx_loader_dx12.dll signedbin/amd_fidelityfx_upscaler_dx12.dll signedbin/amd_fidelityfx_framegeneration_dx12.dll)
        if(NOT EXISTS "${kit}/${file}")
            message(FATAL_ERROR "Incomplete AMD FSR SDK: ${kit}/${file}")
        endif()
    endforeach()
    add_library(bf_fsr_temporal STATIC "${PROJECT_SOURCE_DIR}/src/graphics/fsr_temporal.cpp")
    target_include_directories(bf_fsr_temporal PUBLIC "${PROJECT_SOURCE_DIR}/src" PRIVATE
        "${kit}/api/include" "${kit}/upscalers/include" "${kit}/framegeneration/include")
    target_link_libraries(bf_fsr_temporal PUBLIC d3d12 dxgi dxguid)
    target_compile_definitions(bf_fsr_temporal PRIVATE _WINDOWS)
    add_executable(bf_fsr_temporal_smoke "${PROJECT_SOURCE_DIR}/tests/fsr_temporal_smoke.cpp")
    bf_attach_fsr_runtime(bf_fsr_temporal_smoke)
    foreach(mode fsr3 fsr4 framegen fsr4-framegen fsr4-bgra)
        add_test(NAME ${mode}_hardware COMMAND bf_fsr_temporal_smoke --${mode})
        set_tests_properties(${mode}_hardware PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RUN_SERIAL TRUE)
    endforeach()
endfunction()
