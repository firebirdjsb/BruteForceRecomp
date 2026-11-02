# Apply only after the existing hash-checked text/menu input integrations.
# The pinned toolkit remains unchanged; the replacement is private build output.
if(NOT WIN32)
    return()
endif()
bf_ui_read(xbox_input game-ui-mkb.c)
function(bf_mkb_replace before after)
    string(FIND "${BF_UI_SOURCE}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Desktop input source no longer matches the audited poll path.")
    endif()
    string(REPLACE "${before}" "${after}" changed "${BF_UI_SOURCE}")
    set(BF_UI_SOURCE "${changed}" PARENT_SCOPE)
endfunction()
bf_mkb_replace("static volatile LONG s_active = 1;" [=[/* Aim reports persist for one 60-Hz sampling window. USB may poll much
 * faster; draining mouse deltas on every USB poll loses most camera motion. */
static double s_aim_time;
static int s_aim_initialized;
static SHORT s_aim_x, s_aim_y;
static void mkb_reset_aim(void) {
    s_aim_initialized = 0; s_aim_x = s_aim_y = 0;
    InterlockedExchange(&s_dx, 0); InterlockedExchange(&s_dy, 0);
    InterlockedExchange(&s_wheel, 0);
    InterlockedExchange(&s_mouse_btn, 0);
}
static volatile LONG s_active = 1;]=])
bf_mkb_replace("len *= s_cfg.sens;" "len *= s_cfg.sens * bf_ui_mouse_sensitivity();")
bf_mkb_replace("if (btn & 0x02) analog[XBOX_BUTTON_LTRIGGER] = 255;  /* ADS/aim   */"
    "if (btn & 0x02) b |= XBOX_GAMEPAD_RIGHT_THUMB; /* Brute Force: toggle scope, not grenade trigger */\n    if (MK_VK('G')) analog[XBOX_BUTTON_LTRIGGER] = 255; /* grenade / inventory */")
bf_mkb_replace("btn   = InterlockedExchange(&s_mouse_btn, 0);" [=[/* Buttons are held state, not a queue. Only raw UP releases them. */
    btn   = InterlockedCompareExchange(&s_mouse_btn, 0, 0);]=])
bf_mkb_replace([=[        if (mx && my) { mx = (int)(mx * 70.71); my = (int)(my * 70.71); }
        if (mx || my) mkb_aim_vector(mx / 100.0, my / 100.0, &mov_x, &mov_y);]=] [=[        /* Full speed on key-down; opposite keys cancel. Diagonals have
         * the same vector length. Movement never uses mouse sensitivity,
         * aim acceleration or aim deadzone compensation. Signed 16-bit axes. */
        const int full = mx && my ? 23170 : 32767;
        mov_x = (SHORT)(mx * full); mov_y = (SHORT)(my * full);]=])
bf_mkb_replace([=[    dx    = InterlockedExchange(&s_dx, 0);
    dy    = InterlockedExchange(&s_dy, 0);]=] [=[    dx = dy = 0; /* Mouse deltas are drained only at the sampling boundary. */]=])
bf_mkb_replace([=[    if (dx || dy) {
        double ax =  (double)dx / (double)s_cfg.full;
        double ay = -(double)dy / (double)s_cfg.full;
        if (s_cfg.invert_y) ay = -ay;
        mkb_aim_vector(ax, ay, &aim_x, &aim_y);
    }]=] [=[    {
        LARGE_INTEGER tick, frequency;
        const double interval = 1000.0 / 60.0;
        if (QueryPerformanceCounter(&tick) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
            double now = (double)tick.QuadPart * 1000.0 / (double)frequency.QuadPart;
            if (!s_aim_initialized) { s_aim_time = now - interval; s_aim_initialized = 1; }
            double elapsed = now - s_aim_time;
            if (elapsed + 0.000001 >= interval) {
                dx = InterlockedExchange(&s_dx, 0); dy = InterlockedExchange(&s_dy, 0);
                /* Counts per reference interval, independent of USB poll
                 * frequency. Late frames do not amplify the mouse velocity. */
                double factor = interval / (elapsed > interval ? elapsed : interval);
                double ax = dx * factor / (double)s_cfg.full;
                double ay = -dy * factor / (double)s_cfg.full;
                if (s_cfg.invert_y) ay = -ay;
                mkb_aim_vector(ax, ay, &s_aim_x, &s_aim_y);
                s_aim_time = now;
            }
            aim_x = s_aim_x; aim_y = s_aim_y;
        }
    }]=])
bf_mkb_replace("if (!s_active) {\n        mkb_manage_cursor(0);" "if (!s_active) {\n        mkb_reset_aim();\n        mkb_manage_cursor(0);")
bf_mkb_replace("if (!game_is_foreground()) {\n        mkb_manage_cursor(0);" "if (!game_is_foreground()) {\n        mkb_reset_aim();\n        mkb_manage_cursor(0);")
bf_mkb_replace("if (bf_text_active() || bf_ui_capture()) {\n        mkb_manage_cursor(0);" "if (bf_text_active() || bf_ui_capture()) {\n        mkb_reset_aim();\n        mkb_manage_cursor(0);")
# Remove the old poll-thread cursor side effects completely. A window-thread
# tick and WM_SETCURSOR now own hiding/confinement and release on focus loss.
string(FIND "${BF_UI_SOURCE}" "static void mkb_manage_cursor(int capture)" cursor_start)
string(FIND "${BF_UI_SOURCE}" "/* Radial deadzone compensation:" cursor_end)
if(cursor_start LESS 0 OR cursor_end LESS cursor_start)
    message(FATAL_ERROR "Expected audited cursor owner was not found.")
endif()
math(EXPR cursor_length "${cursor_end}-${cursor_start}")
string(SUBSTRING "${BF_UI_SOURCE}" ${cursor_start} ${cursor_length} old_cursor)
string(REPLACE "${old_cursor}" "static void mkb_manage_cursor(int capture) { (void)capture; }\n\nint bf_mkb_capture_requested(void) {\n    return mkb_enabled() && s_thread_up && s_active && game_is_foreground()\n        && !bf_text_active() && !bf_ui_capture();\n}\n\n" BF_UI_SOURCE "${BF_UI_SOURCE}")
bf_ui_write(xbox_input game-desktop-mkb.c)

bf_ui_read(xbox_video game-ui-window.c)
string(PREPEND BF_UI_SOURCE "#include \"input/desktop_cursor.h\"\nextern int bf_mkb_capture_requested(void);\nstatic BfDesktopCursor s_desktop_cursor;\n")
bf_mkb_replace("    case WM_CLOSE:" "    case WM_SETCURSOR:\n        if(LOWORD(l)==HTCLIENT && bf_desktop_cursor_update(&s_desktop_cursor,h,bf_mkb_capture_requested()))return TRUE;\n        break;\n    case WM_CLOSE:\n        bf_desktop_cursor_update(&s_desktop_cursor,h,0);")
bf_mkb_replace("    case WM_KILLFOCUS:" "    case WM_KILLFOCUS:\n        bf_desktop_cursor_update(&s_desktop_cursor,h,0);")
bf_mkb_replace("        if (s_present_idx >= 0 && s_rgb) {" "        bf_desktop_cursor_update(&s_desktop_cursor,hwnd,bf_mkb_capture_requested());\n        if (s_present_idx >= 0 && s_rgb) {")
bf_ui_write(xbox_video game-desktop-window.c)

add_executable(bf_desktop_input_test tests/desktop_input_test.c)
target_include_directories(bf_desktop_input_test PRIVATE src
    "${CMAKE_BINARY_DIR}/runtime-fixes" "${XBOXRECOMP_DIR}/src/input"
    "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/include")
add_test(NAME desktop_input_reports COMMAND bf_desktop_input_test)
add_executable(bf_desktop_cursor_test tests/desktop_cursor_test.c)
target_include_directories(bf_desktop_cursor_test PRIVATE src)
add_test(NAME desktop_cursor_ownership COMMAND bf_desktop_cursor_test)
