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
    # The public bridge treats even RECOMP_ASYNC_IO=0 as enabled. Preserve a
    # real disabled control for title-profile comparisons, without changing
    # the existing read, event, APC or IOSB completion implementation.
    set(async_before "on = getenv(\"RECOMP_ASYNC_IO\") ? 1 : 0;")
    string(FIND "${source}" "${async_before}" async_found)
    if(async_found EQUAL -1)
        message(FATAL_ERROR "Expected asynchronous-read mode guard was not found.")
    endif()
    string(REPLACE "${async_before}"
        "{ const char *value = getenv(\"RECOMP_ASYNC_IO\"); on = value && *value && *value != '0'; }"
        source "${source}")
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
    string(REPLACE "if (!bridge_buf_ok(buffer_va, length, \"NtReadFile\")) {"
        "if (!bridge_buf_ok(buffer_va, length, \"NtReadFile\")) {\n        if (getenv(\"BF_TRACE_BINK\")) {\n            fprintf(stderr, \"[READ-FAILURE] caller=%08X esp=%08X\\n\", STACK_ARG(-1), g_esp);\n            for (int i = 0; i < 96; ++i) fprintf(stderr, \" %08X\", STACK_ARG(i));\n            fputc(10, stderr);\n        }"
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
    # Nonzero NO_OPERATION is an XDK software interrupt, not an empty draw.
    string(REPLACE "case NV097_NO_OPERATION:"
        "case NV097_NO_OPERATION:\n        { extern int bf_gpu_software_method(uint32_t, uint32_t);\n          if (bf_gpu_software_method(param, s_gpu.zstencil_clear)) return; }"
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
    # Opt-in frame statistics from upstream 0014, recorded from the same
    # pinned and already semaphore-patched GPU executor. RECOMP_FRAME_STATS
    # is off by default; hardware-backend pixels are reported as unknown.
    set(bf_stats_before_0 [=[#include "nv2a_backend.h"]=])
    set(bf_stats_after_0 [=[#include "nv2a_backend.h"
#include "nv2a_frame_stats.h"]=])
    string(FIND "${gpu_source}" "${bf_stats_before_0}" bf_stats_anchor_0)
    if(bf_stats_anchor_0 EQUAL -1)
        message(FATAL_ERROR "NV2A pinned frame-statistics add include anchor missing.")
    endif()
    string(REPLACE "${bf_stats_before_0}" "${bf_stats_after_0}" gpu_source "${gpu_source}")
    set(bf_stats_before_1 [=[/* The flip hold (nv2a_flip_hold.h).]=])
    set(bf_stats_after_1 [=[/* RECOMP_FRAME_STATS=<file>: one line per flip with a hash of the presented
 * picture (nv2a_frame_stats.h). The picture is the surface the last batch
 * drew into, as the presenter and RECOMP_FB_DUMP read it. With a registered
 * back end the pixels are not in guest memory, so the hash is unknown. */
static Nv2aFrameStats s_frame_stats;
static int s_frame_stats_on = -1;
/* Constant uploads since the last flip (TRANSFORM_CONSTANT words and the
 * load index), for the frame statistics' constants column. */
static uint64_t s_frame_constants;

static void frame_constants_mix(uint32_t word)
{
    s_frame_constants = (s_frame_constants ^ word) * 0x100000001B3ull + 0x9E37u;
}

static int frame_stats_on(void)
{
    if (s_frame_stats_on < 0) {
        const char *path = getenv("RECOMP_FRAME_STATS");
        s_frame_stats_on = 0;
        if (path && *path) {
            if (nv2a_frame_stats_open(&s_frame_stats, path)) {
                s_frame_stats_on = 1;
                fprintf(stderr, "[GPU] frame statistics -> %s\n", path);
            } else {
                fprintf(stderr, "[GPU] cannot open RECOMP_FRAME_STATS file %s\n", path);
            }
        }
    }
    return s_frame_stats_on;
}

static void frame_stats_flip(void)
{
    static uint32_t draws_at_flip, program_verts_at_flip;
    uint64_t hash = 0;

    if (!frame_stats_on())
        return;
    if (!backend_draws()) {
        int drawn = s_gpu.drawn_offset != 0;
        uint32_t offset = drawn ? s_gpu.drawn_offset : s_gpu.color_offset;
        uint32_t pitch = drawn ? s_gpu.drawn_pitch : s_gpu.pitch;
        uint32_t x = drawn ? s_gpu.drawn_x : s_gpu.clip_x;
        uint32_t y = drawn ? s_gpu.drawn_y : s_gpu.clip_y;
        uint32_t w = drawn ? s_gpu.drawn_w : s_gpu.clip_w;
        uint32_t h = drawn ? s_gpu.drawn_h : s_gpu.clip_h;
        uint32_t bpp = drawn ? s_gpu.drawn_bpp : surface_bpp();
        uint32_t base = offset ? dma_resolve(offset) : 0;

        if (base && w && h && (bpp == 2 || bpp == 4) && pitch >= (x + w) * bpp
            && guest_span_ok(base, pitch * (y + h)))
            hash = nv2a_frame_hash((const uint8_t *)xbox_GetMemoryOffset() + base,
                                   pitch, x, y, w, h, bpp);
    }
    nv2a_frame_stats_flip(&s_frame_stats, xbox_HostNowNs() / 1000u,
                          s_gpu.draws - draws_at_flip,
                          s_gpu.verts_program - program_verts_at_flip, hash,
                          s_frame_constants);
    s_frame_constants = 0;
    draws_at_flip = s_gpu.draws;
    program_verts_at_flip = s_gpu.verts_program;
}

/* The flip hold (nv2a_flip_hold.h).]=])
    string(FIND "${gpu_source}" "${bf_stats_before_1}" bf_stats_anchor_1)
    if(bf_stats_anchor_1 EQUAL -1)
        message(FATAL_ERROR "NV2A pinned frame-statistics add stats functions anchor missing.")
    endif()
    string(REPLACE "${bf_stats_before_1}" "${bf_stats_after_1}" gpu_source "${gpu_source}")
    set(bf_stats_before_2 [=[        inited = 1;]=])
    set(bf_stats_after_2 [=[        inited = 1;
        (void)frame_stats_on();]=])
    string(FIND "${gpu_source}" "${bf_stats_before_2}" bf_stats_anchor_2)
    if(bf_stats_anchor_2 EQUAL -1)
        message(FATAL_ERROR "NV2A pinned frame-statistics init anchor missing.")
    endif()
    string(REPLACE "${bf_stats_before_2}" "${bf_stats_after_2}" gpu_source "${gpu_source}")
    set(bf_stats_before_3 [=[        s_gpu.drawn_stale = 1;
        if (s_backend && s_backend->flip)]=])
    set(bf_stats_after_3 [=[        frame_stats_flip();
        s_gpu.drawn_stale = 1;
        if (s_backend && s_backend->flip)]=])
    string(FIND "${gpu_source}" "${bf_stats_before_3}" bf_stats_anchor_3)
    if(bf_stats_anchor_3 EQUAL -1)
        message(FATAL_ERROR "NV2A pinned frame-statistics flip anchor missing.")
    endif()
    string(REPLACE "${bf_stats_before_3}" "${bf_stats_after_3}" gpu_source "${gpu_source}")
    set(bf_stats_before_4 [=[        nv2a_vsh_set_constant_load(param);]=])
    set(bf_stats_after_4 [=[        if (s_frame_stats_on > 0)
            frame_constants_mix(param | 0x80000000u);
        nv2a_vsh_set_constant_load(param);]=])
    string(FIND "${gpu_source}" "${bf_stats_before_4}" bf_stats_anchor_4)
    if(bf_stats_anchor_4 EQUAL -1)
        message(FATAL_ERROR "NV2A pinned frame-statistics constant load anchor missing.")
    endif()
    string(REPLACE "${bf_stats_before_4}" "${bf_stats_after_4}" gpu_source "${gpu_source}")
    set(bf_stats_before_5 [=[            nv2a_vsh_constant_word(param);]=])
    set(bf_stats_after_5 [=[            if (s_frame_stats_on > 0)
                frame_constants_mix(param);
            nv2a_vsh_constant_word(param);]=])
    string(FIND "${gpu_source}" "${bf_stats_before_5}" bf_stats_anchor_5)
    if(bf_stats_anchor_5 EQUAL -1)
        message(FATAL_ERROR "NV2A pinned frame-statistics constant word anchor missing.")
    endif()
    string(REPLACE "${bf_stats_before_5}" "${bf_stats_after_5}" gpu_source "${gpu_source}")
    set(bf_stats_before_6 [=[    flip_hold_report();]=])
    set(bf_stats_after_6 [=[    flip_hold_report();
    if (s_frame_stats_on > 0) {
        uint32_t flips, distinct, unknown;
        nv2a_frame_stats_take_report(&s_frame_stats, &flips, &distinct, &unknown);
        fprintf(stderr, "[GPU] frames since last report: %u flips, %u distinct, %u unknown\n",
                flips, distinct, unknown);
    }]=])
    string(FIND "${gpu_source}" "${bf_stats_before_6}" bf_stats_anchor_6)
    if(bf_stats_anchor_6 EQUAL -1)
        message(FATAL_ERROR "NV2A pinned frame-statistics report anchor missing.")
    endif()
    string(REPLACE "${bf_stats_before_6}" "${bf_stats_after_6}" gpu_source "${gpu_source}")
    set(gpu_patched "${CMAKE_BINARY_DIR}/runtime-fixes/nv2a_pb_exec.c")
    file(WRITE "${gpu_patched}.in" "${gpu_source}")
    configure_file("${gpu_patched}.in" "${gpu_patched}" COPYONLY)
    get_target_property(patched_sources xbox_kernel SOURCES)
    list(REMOVE_ITEM patched_sources "${executor}")
    list(APPEND patched_sources "${gpu_patched}")
    set_property(TARGET xbox_kernel PROPERTY SOURCES "${patched_sources}")
    # Adapt the minimal mono-HRTF stereo fix from upstream patch 0035.
    # Preserve PCM source channel count; only filtered *output* is binaural.
    # Patch the private pinned APU copy; applies to Windows and Linux.
    set(apu_original "${toolkit}/src/apu/apu_vp.c")
    file(READ "${apu_original}" apu_source)
    string(REPLACE "\r\n" "\n" apu_source "${apu_source}")
    set(hrtf_a [=[    unsigned int channels = stereo ? 2 : 1;
    bool paused = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,]=])
    set(hrtf_b [=[    unsigned int channels = stereo ? 2 : 1;
    unsigned int output_channels = channels;
    bool paused = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,]=])
    set(hrtf_c [=[            hrtf_filter_process(&d->vp.filters[v].hrtf, samples, samples);
        }
    }]=])
    set(hrtf_d [=[            hrtf_filter_process(&d->vp.filters[v].hrtf, samples, samples);
            output_channels = 2;
        }
    }]=])
    set(hrtf_e [=[mixbins[bin[b]][i] += (g0 + step * (i + 1)) * samples[i][b % channels];]=])
    set(hrtf_f [=[mixbins[bin[b]][i] += (g0 + step * (i + 1)) * samples[i][b % output_channels];]=])
    foreach(old IN ITEMS hrtf_a hrtf_c hrtf_e)
        string(FIND "${apu_source}" "${${old}}" hrtf_found)
        if(hrtf_found EQUAL -1)
            message(FATAL_ERROR "Pinned APU mono-HRTF correction anchor ${old} not found.")
        endif()
    endforeach()
    string(REPLACE "${hrtf_a}" "${hrtf_b}" apu_source "${apu_source}")
    string(REPLACE "${hrtf_c}" "${hrtf_d}" apu_source "${apu_source}")
    string(REPLACE "${hrtf_e}" "${hrtf_f}" apu_source "${apu_source}")
    set(apu_patched "${CMAKE_BINARY_DIR}/runtime-fixes/apu_vp.c")
    file(WRITE "${apu_patched}.in" "${apu_source}")
    configure_file("${apu_patched}.in" "${apu_patched}" COPYONLY)
    get_target_property(apu_sources xbox_apu SOURCES)
    get_target_property(apu_directory xbox_apu SOURCE_DIR)
    set(apu_replacements)
    foreach(item IN LISTS apu_sources)
        get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${apu_directory}")
        if(absolute STREQUAL apu_original)
            list(APPEND apu_replacements "${apu_patched}")
        else()
            list(APPEND apu_replacements "${absolute}")
        endif()
    endforeach()
    set_property(TARGET xbox_apu PROPERTY SOURCES "${apu_replacements}")
    # Windows framebuffer publication is adapted once by frame_handoff.cmake,
    # which also retains native icon and frame-statistics integration.
endfunction()
