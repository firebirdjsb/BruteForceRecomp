add_executable(bf_ui_overlay_test "${PROJECT_SOURCE_DIR}/tests/ui_overlay_test.cpp")
target_link_libraries(bf_ui_overlay_test PRIVATE bf_host_ui)
add_test(NAME transparent_ui_overlay COMMAND bf_ui_overlay_test)
