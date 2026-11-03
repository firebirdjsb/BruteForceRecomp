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
# F1 is the only settings/capture entry point. Remove the upstream F9 toggle.
bf_mkb_replace([=[        if (vk == MKB_TOGGLE_VK) {
            if (k->Message == WM_KEYDOWN && !s_vk_down[vk & 0xFF]) {
                LONG now = !s_active;
                InterlockedExchange(&s_active, now);
                fprintf(stderr, "  [MKB] %s\n", now ? "captured"
                                                    : "released (F9)");
                fflush(stderr);
            }
        }
]=] "")
bf_mkb_replace([=[/* F9 is read on the input thread, so the toggle works even while the
 * game is stalled on the poll side. */
#define MKB_TOGGLE_VK  VK_F9]=] "/* F1 and foreground ownership control capture; no raw-input hotkey. */")
bf_mkb_replace("/* F9 toggles this               */" "/* enabled while adapter is loaded */")
bf_mkb_replace("mouse+keyboard captured (F9 releases; " "mouse+keyboard captured (F1 opens settings; ")
# Keep a separate raw-motion queue for the title's once-per-frame input update.
# USB polling may consume the virtual-stick queue before that update executes.
bf_mkb_replace("static volatile LONG s_dx, s_dy;" "static volatile LONG s_dx, s_dy;\nstatic volatile LONG s_native_dx, s_native_dy;\nstatic int game_is_foreground(void);")
bf_mkb_replace("if (m->lLastX) InterlockedAdd(&s_dx, m->lLastX);" "if (m->lLastX) { InterlockedAdd(&s_dx, m->lLastX); InterlockedAdd(&s_native_dx, m->lLastX); }")
bf_mkb_replace("if (m->lLastY) InterlockedAdd(&s_dy, m->lLastY);" "if (m->lLastY) { InterlockedAdd(&s_dy, m->lLastY); InterlockedAdd(&s_native_dy, m->lLastY); }\n            if ((m->lLastX || m->lLastY) && game_is_foreground()) bf_ui_mouse_activity();")
bf_mkb_replace("    s_aim_initialized = 0; s_aim_x = s_aim_y = 0;" "    s_aim_initialized = 0; s_aim_x = s_aim_y = 0;\n    InterlockedExchange(&s_native_dx,0); InterlockedExchange(&s_native_dy,0);")
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
string(APPEND BF_UI_SOURCE [=[
/* Called by the authenticated title input update, after its stick deadzones.
 * Positive raw Y points down; the title's processed stick Y does too.
 * Counts are scaled to one reference 30-Hz title update. Elapsed-time
 * normalization makes the accumulated rotation independent of report rate.
 * Values may exceed one stick throw; mouse motion has no stick-speed ceiling.
 * The bound rejects remote-pointer warps while retaining 32 full throws. */
int bf_mkb_native_aim(float *x, float *y) {
    static double previous;
    static int ready;
    LARGE_INTEGER tick,frequency;
    const char *legacy=getenv("BF_MOUSE_RESPONSE");
    if (!x || !y || (legacy && !strcmp(legacy,"legacy"))) return 0;
    if (!bf_mkb_capture_requested() || !bf_ui_keyboard_mode()) {
        InterlockedExchange(&s_native_dx,0);InterlockedExchange(&s_native_dy,0);
        ready=0;return 0;
    }
    if (!QueryPerformanceCounter(&tick) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) return 0;
    double now=(double)tick.QuadPart*1000.0/(double)frequency.QuadPart;
    double elapsed=ready?now-previous:1000.0/30.0;
    previous=now;ready=1;
    if (elapsed<1.0) elapsed=1.0;
    LONG dx=InterlockedExchange(&s_native_dx,0),dy=InterlockedExchange(&s_native_dy,0);
    double gain=0.1*bf_ui_mouse_sensitivity()*(1000.0/30.0)/elapsed;
    double ax=dx*gain,ay=dy*gain;
    if(s_cfg.invert_y)ay=-ay;
    *x=(float)fmax(-32.0,fmin(32.0,ax));*y=(float)fmax(-32.0,fmin(32.0,ay));
    return 1;
}
]=])
bf_ui_write(xbox_input game-desktop-mkb.c)

# The original successful input-update path runs once per controller frame.
# Port +0xBC is assigned 0..3 by original 0x001E7450. Its 0x24-byte temporary
# report starts at ESP+4; processed right X/Y are report+0x1C/+0x20. The plain
# C callback does not change guest registers, stack slots or call/return ABI.
set(native_mouse_matches 0)
foreach(file IN LISTS BF_GENERATED)
    file(READ "${file}" text)
    string(FIND "${text}" "loc_001F33D4: ;\n    edx = esp + 4;" found)
    if(NOT found EQUAL -1)
        string(REPLACE "\r\n" "\n" text "${text}")
        string(SHA256 digest "${text}")
        if(NOT digest STREQUAL "8126887f51e22b1ae03f07a0d690bf98533315a074a07e0b5e90d1d8992bb2f1")
            message(FATAL_ERROR "Unaudited title input update; immediate mouse integration refused.")
        endif()
        string(PREPEND text "extern int bf_mkb_native_aim(float *,float *);\n")
        string(REPLACE "loc_001F33D4: ;\n    edx = esp + 4;" [=[loc_001F33D4: ;
    /* Successful port-0 input only. Raw mouse bypasses the stick's adaptive
     * deadzone; all original buttons, movement and device ownership remain. */
    if (MEM32(edi + 0xBC) == 0) {
        float mouse_x,mouse_y;
        if (bf_mkb_native_aim(&mouse_x,&mouse_y)) {
            MEMF(esp + 0x20)=mouse_x; MEMF(esp + 0x24)=mouse_y;
        }
    }
    edx = esp + 4;]=] text "${text}")
        set(copy "${CMAKE_BINARY_DIR}/runtime-fixes/game-immediate-mouse.c")
        file(WRITE "${copy}.in" "${text}")
        configure_file("${copy}.in" "${copy}" COPYONLY)
        list(REMOVE_ITEM BF_GENERATED "${file}")
        list(APPEND BF_GENERATED "${copy}")
        math(EXPR native_mouse_matches "${native_mouse_matches}+1")
    endif()
endforeach()
if(NOT native_mouse_matches EQUAL 1)
    message(FATAL_ERROR "Expected exactly one audited title input update.")
endif()

bf_ui_read(xbox_video game-ui-window.c)
string(PREPEND BF_UI_SOURCE "#include \"input/desktop_cursor.h\"\nextern int bf_mkb_capture_requested(void);\nstatic BfDesktopCursor s_desktop_cursor;\n")
bf_mkb_replace("    case WM_CLOSE:" "    case WM_SETCURSOR:\n        if(LOWORD(l)==HTCLIENT && bf_desktop_cursor_update(&s_desktop_cursor,h,bf_mkb_capture_requested()))return TRUE;\n        break;\n    case WM_CLOSE:\n        bf_desktop_cursor_update(&s_desktop_cursor,h,0);")
bf_mkb_replace("    case WM_KILLFOCUS:" "    case WM_KILLFOCUS:\n        bf_desktop_cursor_update(&s_desktop_cursor,h,0);")
bf_mkb_replace("bf_ui_mouse((short)LOWORD(l),(short)HIWORD(l),r.right,r.bottom,0);" "bf_ui_mouse((short)LOWORD(l),(short)HIWORD(l),r.right,r.bottom,(w & MK_LBUTTON)?4:0);")
bf_mkb_replace("if(m==WM_LBUTTONDOWN || m==WM_RBUTTONDOWN)bf_ui_mouse" "if(m==WM_LBUTTONUP)bf_ui_mouse((short)LOWORD(l),(short)HIWORD(l),r.right,r.bottom,3);\n                if(m==WM_LBUTTONDOWN || m==WM_RBUTTONDOWN)bf_ui_mouse")
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
