# Append only after the pinned executor overlays have authenticated their input.
add_library(bf_scene_boundary STATIC src/ui/scene_boundary.cpp src/ui/scene_aa_state.cpp src/graphics/edge_aa.cpp)
target_include_directories(bf_scene_boundary PRIVATE src "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
target_link_libraries(xbox_kernel PUBLIC bf_scene_boundary)
add_executable(bf_scene_boundary_test tests/scene_boundary_test.cpp)
target_include_directories(bf_scene_boundary_test PRIVATE src)
target_link_libraries(bf_scene_boundary_test PRIVATE bf_scene_boundary)
add_test(NAME ordered_scene_boundary COMMAND bf_scene_boundary_test)
if(TARGET bf_scene_backend)
    target_sources(bf_scene_backend PRIVATE src/gpu/scene_antialias.cpp)
    target_link_libraries(bf_scene_backend PUBLIC bf_scene_boundary)
endif()
get_target_property(sources xbox_kernel SOURCES)
get_target_property(directory xbox_kernel SOURCE_DIR)
set(matches 0)
foreach(item IN LISTS sources)
    get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${directory}")
    get_filename_component(name "${absolute}" NAME)
    if(name STREQUAL "nv2a_pb_exec.c")
        file(READ "${absolute}" text)
        set(anchor "case NV097_NO_OPERATION:")
        string(FIND "${text}" "${anchor}" found)
        if(found LESS 0)
            message(FATAL_ERROR "Expected ordered NV2A NO_OPERATION handler.")
        endif()
        set(boundary [=[case NV097_NO_OPERATION:
        if(param==BF_UI_SCENE_BOUNDARY) {
            Nv2aSurface ui_surface;
            xf_queue_flush();
]=])
        if(TARGET bf_scene_backend)
            string(APPEND boundary [=[            if(backend_draws()&&s_xbackend&&s_xbackend->draw)xbackend_flush();
            else if(backend_draws())backend_flush(!s_xf_no_tex);
            current_surface(&ui_surface);
            if(backend_draws()) {
                if(s_xbackend&&s_xbackend->before_ui)s_xbackend->before_ui(&ui_surface);
            } else
]=])
        else()
            string(APPEND boundary "            current_surface(&ui_surface);\n")
        endif()
        string(APPEND boundary [=[                bf_scene_aa_cpu(ui_surface.color_va,ui_surface.width,ui_surface.height,
                    ui_surface.pitch,ui_surface.bytes_per_pixel,s_gpu.flips);
            return;
        }
]=])
        string(PREPEND text "#include \"ui/scene_boundary.h\"\n")
        string(REPLACE "${anchor}" "${boundary}" text "${text}")
        set(copy "${CMAKE_BINARY_DIR}/runtime-fixes/aa-boundary/nv2a_pb_exec.c")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime-fixes/aa-boundary")
        file(WRITE "${copy}.in" "${text}")
        configure_file("${copy}.in" "${copy}" COPYONLY)
        list(REMOVE_ITEM sources "${item}")
        list(APPEND sources "${copy}")
        math(EXPR matches "${matches}+1")
    endif()
endforeach()
if(NOT matches EQUAL 1)
    message(FATAL_ERROR "Expected exactly one authenticated executor for scene AA.")
endif()
target_include_directories(xbox_kernel PRIVATE "${PROJECT_SOURCE_DIR}/src")
set_property(TARGET xbox_kernel PROPERTY SOURCES "${sources}")
