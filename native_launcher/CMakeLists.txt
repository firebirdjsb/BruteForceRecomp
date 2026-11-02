include(FetchContent)
FetchContent_Declare(bf_launcher_runtime
    GIT_REPOSITORY https://github.com/xenophobentx/xboxrecomp.git
    GIT_TAG ebcbf6ff086bea64b5181741951f0d815db90968
    SOURCE_SUBDIR src/install)
FetchContent_Declare(bf_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab
    SOURCE_SUBDIR backends)
if(WIN32 OR ANDROID)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(bf_sdl
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 5d249570393f7a37e037abf22cd6012a4cc56a71)
    FetchContent_MakeAvailable(bf_sdl)
else()
    find_package(SDL2 REQUIRED)
endif()
FetchContent_MakeAvailable(bf_launcher_runtime bf_imgui)
if(ANDROID)
    message(FATAL_ERROR "The native launcher uses SDL2 and portable installer code, but Android application/game lifecycle linking is not integrated yet. No APK is produced.")
endif()
add_library(bf_launcher_ui STATIC
    "${bf_imgui_SOURCE_DIR}/imgui.cpp" "${bf_imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${bf_imgui_SOURCE_DIR}/imgui_tables.cpp" "${bf_imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${bf_imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp"
    "${bf_imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp")
target_include_directories(bf_launcher_ui PUBLIC "${bf_imgui_SOURCE_DIR}" "${bf_imgui_SOURCE_DIR}/backends")
target_link_libraries(bf_launcher_ui PUBLIC SDL2::SDL2)
add_executable(BruteForceLauncher WIN32 main.cpp app.cpp theme.cpp library.cpp process.cpp file_picker.cpp manifest.cpp)
set(BF_PRIVATE_BRANDING_DIR "" CACHE PATH "Private, locally extracted title icon/art; never committed or distributed as source")
if(BF_PRIVATE_BRANDING_DIR AND EXISTS "${BF_PRIVATE_BRANDING_DIR}/title.bmp")
    add_custom_command(TARGET BruteForceLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:BruteForceLauncher>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BF_PRIVATE_BRANDING_DIR}/title.bmp" "$<TARGET_FILE_DIR:BruteForceLauncher>/assets/title.bmp")
endif()
bf_attach_private_icon(BruteForceLauncher)
target_link_libraries(BruteForceLauncher PRIVATE bf_launcher_ui xbox_install)
if(TARGET SDL2::SDL2main)
    target_link_libraries(BruteForceLauncher PRIVATE SDL2::SDL2main)
endif()
if(WIN32)
    target_link_libraries(BruteForceLauncher PRIVATE comdlg32)
    add_custom_command(TARGET BruteForceLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:BruteForceLauncher>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bf_sdl_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:BruteForceLauncher>/assets/SDL2-LICENSE.txt")
endif()
add_custom_command(TARGET BruteForceLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:BruteForceLauncher>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bf_imgui_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:BruteForceLauncher>/assets/ImGui-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bf_launcher_runtime_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:BruteForceLauncher>/assets/XboxRecomp-LICENSE.txt")
