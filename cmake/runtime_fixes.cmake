# Patch a private build copy; never modify the pinned public toolkit checkout.
function(bf_apply_runtime_fixes toolkit)
    set(original "${toolkit}/src/kernel/kernel_bridge.c")
    file(READ "${original}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    string(SHA256 digest "${source}")
    if(NOT digest STREQUAL "a065bdd6d96791f546e849d3d7b752e17a93a002f61234e36d1388a98b2de3b0")
        message(FATAL_ERROR "Kernel bridge differs from the audited pinned source; runtime fix was not applied.")
    endif()
    string(REPLACE "#include \"kernel.h\"" "#include \"kernel.h\"\n#include \"import_guard.h\"" source "${source}")
    set(before "if (!(current & 0x80000000)) {")
    string(FIND "${source}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Expected extra-thunk scan guard was not found.")
    endif()
    string(REPLACE "${before}" "if (!bf_valid_extra_kernel_thunk(current)) {" source "${source}")
    # Per-field read logging can issue millions of host writes while loading a
    # pack. Keep failed reads visible and make successful payload tracing opt-in.
    set(read_log "{\n        const uint8_t *p = (const uint8_t *)XBOX_TO_NATIVE(buffer_va);")
    string(FIND "${source}" "${read_log}" read_found)
    if(read_found EQUAL -1)
        message(FATAL_ERROR "Expected read-trace block was not found.")
    endif()
    string(REPLACE "${read_log}"
        "if (getenv(\"BF_TRACE_FILE_IO\") || (uint32_t)ios.Status >= 0x80000000u) {\n        const uint8_t *p = (const uint8_t *)XBOX_TO_NATIVE(buffer_va);"
        source "${source}")
    set(patched "${CMAKE_BINARY_DIR}/runtime-fixes/kernel_bridge.c")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime-fixes")
    # configure_file preserves timestamps when the replacement has not changed.
    file(WRITE "${CMAKE_BINARY_DIR}/runtime-fixes/kernel_bridge.c.in" "${source}")
    configure_file("${CMAKE_BINARY_DIR}/runtime-fixes/kernel_bridge.c.in" "${patched}" COPYONLY)
    get_target_property(kernel_sources xbox_kernel SOURCES)
    get_target_property(kernel_directory xbox_kernel SOURCE_DIR)
    set(replacement_sources)
    foreach(item IN LISTS kernel_sources)
        get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${kernel_directory}")
        if(absolute STREQUAL original)
            list(APPEND replacement_sources "${patched}")
        else()
            list(APPEND replacement_sources "${absolute}")
        endif()
    endforeach()
    set_property(TARGET xbox_kernel PROPERTY SOURCES "${replacement_sources}")
    target_include_directories(xbox_kernel PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    set(executor "${toolkit}/src/kernel/nv2a_pb_exec.c")
    file(READ "${executor}" gpu_source)
    string(REPLACE "\r\n" "\n" gpu_source "${gpu_source}")
    string(SHA256 gpu_digest "${gpu_source}")
    if(NOT gpu_digest STREQUAL "407369da9e1447c1a5dea9af343fd66877cb31e85a86cda30768f47b2625d573")
        message(FATAL_ERROR "GPU executor differs from audited source; semaphore integration was not applied.")
    endif()
    set(gpu_before "if (subch == 0 && method == 0x1D70 && s_sem_va) {")
    string(FIND "${gpu_source}" "${gpu_before}" gpu_found)
    if(gpu_found EQUAL -1)
        message(FATAL_ERROR "Expected semaphore release handler was not found.")
    endif()
    string(REPLACE "${gpu_before}"
        "if (subch == 0 && method == 0x1D70) {\n        extern uint32_t bf_gpu_semaphore_target(void);\n        if (!s_sem_va) s_sem_va = bf_gpu_semaphore_target();"
        gpu_source "${gpu_source}")
    # Adapt the MIT-licensed generic texture sampling fast path from
    # Tzeentchnet/BruteForce patch 0013 to the already hash-pinned executor.
    # Keep all earlier import, GPU semaphore and platform fixes untouched.
    set(texture_header "#include \"nv2a_combiner.h\"")
    string(FIND "${gpu_source}" "${texture_header}" texture_header_found)
    if(texture_header_found EQUAL -1)
        message(FATAL_ERROR "Expected NV2A combiner include was not found.")
    endif()
    string(REPLACE "${texture_header}" "${texture_header}\n#include \"bf_texture_sample.h\"" gpu_source "${gpu_source}")
    set(texture_old_wrap [=[static uint32_t wrap_coord(uint32_t c, uint32_t size, uint32_t mode)
{
    /* Coordinates are signed: a bilinear tap one texel left of or above
     * texel 0 is -1, which must wrap to the far edge or clamp to 0 -- as an
     * unsigned value it clamped to the far edge instead. */
    int32_t sc = (int32_t)c, n = (int32_t)size;
    if (!size)
        return 0;
    if (mode == 1)                         /* wrap */
        return (uint32_t)(((sc % n) + n) % n);
    if (mode == 2) {                       /* mirror */
        int32_t m = ((sc % (2 * n)) + 2 * n) % (2 * n);
        return (uint32_t)(m < n ? m : 2 * n - 1 - m);
    }
    return sc < 0 ? 0u : (sc >= n ? size - 1 : c);   /* clamp, and the rest */
}]=])
    string(FIND "${gpu_source}" "${texture_old_wrap}" texture_wrap_found)
    if(texture_wrap_found EQUAL -1)
        message(FATAL_ERROR "Expected pinned texture coordinate function was not found.")
    endif()
    string(REPLACE "${texture_old_wrap}"
        "/* Texture coordinate math moved to bf_texture_sample.h; scalar reference is in tests. */"
        gpu_source "${gpu_source}")
    set(texture_u "u = wrap_coord(u, t->width,  t->addr_u);")
    set(texture_v "v = wrap_coord(v, t->height, t->addr_v);")
    set(texture_slot "size_t slot = ((key / bb) ^ (key >> 16)) & 4095;")
    foreach(anchor IN ITEMS "${texture_u}" "${texture_v}" "${texture_slot}")
        string(FIND "${gpu_source}" "${anchor}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Expected texture sampling call site was not found: ${anchor}")
        endif()
    endforeach()
    string(REPLACE "${texture_u}" "u = bf_texture_wrap_coord(u, t->width,  t->addr_u);" gpu_source "${gpu_source}")
    string(REPLACE "${texture_v}" "v = bf_texture_wrap_coord(v, t->height, t->addr_v);" gpu_source "${gpu_source}")
    string(REPLACE "${texture_slot}" "size_t slot = bf_texture_dxt_slot(key, bb);" gpu_source "${gpu_source}")
    # Tzeentchnet patch 0019: cache a draw-hot environment lookup once.
    # The pinned runtime already caches the per-method form below; only this
    # separate primitive-draw call is still uncached.
    set(verbose_before "if (getenv(\"RECOMP_PB_EXEC_VERBOSE\")) {")
    string(FIND "${gpu_source}" "${verbose_before}" verbose_found)
    if(verbose_found EQUAL -1)
        message(FATAL_ERROR "Expected uncached draw logging guard was not found.")
    endif()
    set(verbose_after "static int bf_verbose_draws = -1;\n    if (bf_verbose_draws < 0)\n        bf_verbose_draws = getenv(\"RECOMP_PB_EXEC_VERBOSE\") != NULL;\n    if (bf_verbose_draws) {")
    string(REPLACE "${verbose_before}" "${verbose_after}" gpu_source "${gpu_source}")
    set(gpu_patched "${CMAKE_BINARY_DIR}/runtime-fixes/nv2a_pb_exec.c")
    file(WRITE "${gpu_patched}.in" "${gpu_source}")
    configure_file("${gpu_patched}.in" "${gpu_patched}" COPYONLY)
    get_target_property(patched_sources xbox_kernel SOURCES)
    list(REMOVE_ITEM patched_sources "${executor}")
    list(APPEND patched_sources "${gpu_patched}")
    set_property(TARGET xbox_kernel PROPERTY SOURCES "${patched_sources}")
    # Windows-only correctness backport from Tzeentchnet patch 0005.
    # Leave the original toolkit and the Linux SDL presenter untouched.
    if(WIN32)
        set(video_original "${toolkit}/src/video/fb_present.c")
        file(READ "${video_original}" video_source)
        string(REPLACE "\r\n" "\n" video_source "${video_source}")
        set(bf_frame_old_0 [=[static volatile LONG s_present_idx = -1;   /* -1 until the first flip */]=])
        set(bf_frame_new_0 [=[static LONG          s_present_idx = -1;   /* -1 until the first flip */
static SRWLOCK       s_present_lock = SRWLOCK_INIT;]=])
        string(FIND "${video_source}" "${bf_frame_old_0}" frame_anchor_0)
        if(frame_anchor_0 EQUAL -1)
            message(FATAL_ERROR "Expected framebuffer declarations anchor not found.")
        endif()
        string(REPLACE "${bf_frame_old_0}" "${bf_frame_new_0}" video_source "${video_source}")
        set(bf_frame_old_1 [=[    if (getenv("RECOMP_FB_VA"))
        return;                       /* pinned: leave the old path alone */
    next = (s_present_idx == 0) ? 1 : 0;]=])
        set(bf_frame_new_1 [=[    if (getenv("RECOMP_FB_VA"))
        return;                       /* pinned: leave the old path alone */
    AcquireSRWLockExclusive(&s_present_lock);
    next = (s_present_idx == 0) ? 1 : 0;]=])
        string(FIND "${video_source}" "${bf_frame_old_1}" frame_anchor_1)
        if(frame_anchor_1 EQUAL -1)
            message(FATAL_ERROR "Expected framebuffer writer_begin anchor not found.")
        endif()
        string(REPLACE "${bf_frame_old_1}" "${bf_frame_new_1}" video_source "${video_source}")
        set(bf_frame_old_2 [=[        if (!s_present[next])
            return;]=])
        set(bf_frame_new_2 [=[        if (!s_present[next]) {
            ReleaseSRWLockExclusive(&s_present_lock);
            fprintf(stderr, "  [FBWIN] cannot allocate a presentation frame\n");
            return;
        }]=])
        string(FIND "${video_source}" "${bf_frame_old_2}" frame_anchor_2)
        if(frame_anchor_2 EQUAL -1)
            message(FATAL_ERROR "Expected framebuffer allocation anchor not found.")
        endif()
        string(REPLACE "${bf_frame_old_2}" "${bf_frame_new_2}" video_source "${video_source}")
        set(bf_frame_old_3 [=[    /* Published only once it is whole. */
    InterlockedExchange(&s_present_idx, next);
}

/* Title bar]=])
        set(bf_frame_new_3 [=[    /* Published under the same lock used by the window's copy. */
    s_present_idx = next;
    ReleaseSRWLockExclusive(&s_present_lock);
}

static int bf_copy_presented_frame(uint32_t *dst)
{
    int copied = 0;
    AcquireSRWLockShared(&s_present_lock);
    if (s_present_idx >= 0) {
        memcpy(dst, s_present[s_present_idx],
               (size_t)s_fb_width * s_fb_height * 4);
        copied = 1;
    }
    ReleaseSRWLockShared(&s_present_lock);
    return copied;
}

/* Title bar]=])
        string(FIND "${video_source}" "${bf_frame_old_3}" frame_anchor_3)
        if(frame_anchor_3 EQUAL -1)
            message(FATAL_ERROR "Expected framebuffer publish anchor not found.")
        endif()
        string(REPLACE "${bf_frame_old_3}" "${bf_frame_new_3}" video_source "${video_source}")
        set(bf_frame_old_4 [=[        if (s_present_idx >= 0 && s_rgb) {
            /* A finished frame, published by the flip. Copied into s_rgb so
             * the dump path and GDI see one consistent image even if the
             * next flip lands mid-blit. */
            LONG idx = s_present_idx;
            if (s_present[idx])
                memcpy(s_rgb, s_present[idx],
                       (size_t)s_fb_width * s_fb_height * 4);
            if (!fb_pinned)]=])
        set(bf_frame_new_4 [=[        if (s_rgb && bf_copy_presented_frame(s_rgb)) {
            /* The snapshot was copied while excluding producer writes.
             * GDI and dump paths now see the same complete image. */
            if (!fb_pinned)]=])
        string(FIND "${video_source}" "${bf_frame_old_4}" frame_anchor_4)
        if(frame_anchor_4 EQUAL -1)
            message(FATAL_ERROR "Expected framebuffer reader anchor not found.")
        endif()
        string(REPLACE "${bf_frame_old_4}" "${bf_frame_new_4}" video_source "${video_source}")
        set(video_patched "${CMAKE_BINARY_DIR}/runtime-fixes/fb_present.c")
        file(WRITE "${video_patched}.in" "${video_source}")
        configure_file("${video_patched}.in" "${video_patched}" COPYONLY)
        get_target_property(video_sources xbox_video SOURCES)
        get_target_property(video_directory xbox_video SOURCE_DIR)
        set(video_replacements)
        foreach(item IN LISTS video_sources)
            get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${video_directory}")
            if(absolute STREQUAL video_original)
                list(APPEND video_replacements "${video_patched}")
            else()
                list(APPEND video_replacements "${absolute}")
            endif()
        endforeach()
        set_property(TARGET xbox_video PROPERTY SOURCES "${video_replacements}")
    endif()
endfunction()
