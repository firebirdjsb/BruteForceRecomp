# The window and software GPU run on different Windows threads. The published
# two-buffer index alone does not prevent a fast producer from reusing the
# buffer the window is still copying. Protect both copies with the same lock.
if(WIN32)
    set(original "${XBOXRECOMP_DIR}/src/video/fb_present.c")
    file(READ "${original}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    string(SHA256 hash "${source}")
    if(NOT hash STREQUAL "f0a3f553ccf4afc2e1bcfc1020030bfe76a1fc584caefc3d2367ba87f8ed9782")
        message(FATAL_ERROR "Framebuffer source differs from the audited pinned revision.")
    endif()
    string(REPLACE "static volatile LONG s_present_idx = -1;"
        "static SRWLOCK s_present_lock = SRWLOCK_INIT;\nstatic volatile LONG s_present_idx = -1;" source "${source}")
    string(REPLACE "wc.hInstance     = GetModuleHandleA(NULL);"
        "wc.hInstance     = GetModuleHandleA(NULL);\n        wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(101));" source "${source}")
    string(REPLACE "SetWindowTextW(hwnd, tb);"
        "SetWindowTextW(hwnd, tb);\n                if (getenv(\"BF_FRAME_STATS\")) fprintf(stderr, \"[FRAME] %.1f fps; draws=%ld\\n\", t0 ? (f-f0)*1000.0/(now-t0) : 0.0, s_frame_draws);" source "${source}")
    string(REPLACE "next = (s_present_idx == 0) ? 1 : 0;"
        "AcquireSRWLockExclusive(&s_present_lock);\n    next = (s_present_idx == 0) ? 1 : 0;" source "${source}")
    string(REPLACE "if (!s_present[next])\n            return;"
        "if (!s_present[next]) {\n            ReleaseSRWLockExclusive(&s_present_lock);\n            return;\n        }" source "${source}")
    string(REPLACE "InterlockedExchange(&s_present_idx, next);"
        "InterlockedExchange(&s_present_idx, next);\n    ReleaseSRWLockExclusive(&s_present_lock);" source "${source}")
    string(REPLACE "LONG idx = s_present_idx;"
        "AcquireSRWLockExclusive(&s_present_lock);\n            LONG idx = s_present_idx;" source "${source}")
    string(REPLACE "(size_t)s_fb_width * s_fb_height * 4);\n            if (!fb_pinned)"
        "(size_t)s_fb_width * s_fb_height * 4);\n            ReleaseSRWLockExclusive(&s_present_lock);\n            if (!fb_pinned)" source "${source}")
    set(copy "${CMAKE_BINARY_DIR}/runtime-fixes/fb_present.c")
    file(WRITE "${copy}.in" "${source}")
    configure_file("${copy}.in" "${copy}" COPYONLY)
    get_target_property(sources xbox_video SOURCES)
    get_target_property(directory xbox_video SOURCE_DIR)
    set(replaced)
    foreach(item IN LISTS sources)
        get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${directory}")
        if(absolute STREQUAL original)
            list(APPEND replaced "${copy}")
        else()
            list(APPEND replaced "${absolute}")
        endif()
    endforeach()
    set_property(TARGET xbox_video PROPERTY SOURCES "${replaced}")
    target_include_directories(xbox_video PRIVATE "${directory}")
endif()
