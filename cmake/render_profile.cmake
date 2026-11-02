# Opt-in batch timing. This wraps the actual software renderer; it does not
# change the game's timing, submitted commands or rasterization.
set(profile_source "${CMAKE_BINARY_DIR}/runtime-fixes/nv2a_pb_exec.c")
file(READ "${profile_source}" profile_text)
string(PREPEND profile_text "#include \"render_blend.h\"\n")
# Common source-over keeps precisely the original float order/rounding, using
# four SIMD lanes on x64. Other factors/equations retain the generic renderer.
set(blend_begin "if (s_gpu.blend_enable\n        && !(s_gpu.blend_sfactor == 1 && s_gpu.blend_dfactor == 0)) {")
set(blend_fast [=[
if (s_gpu.blend_enable && s_gpu.blend_sfactor == 0x302 &&
        s_gpu.blend_dfactor == 0x303 && s_gpu.blend_equation == 0x8006) {
        uint32_t dst;
        if (bpp == 4) dst = ((const uint32_t *)row)[xi];
        else {
            uint32_t t = ((const uint16_t *)row)[xi];
            dst = 0xFF000000u | ((t & 0xF800u) << 8) | ((t & 0x07E0u) << 5) | ((t & 0x001Fu) << 3);
        }
        argb = bf_source_over(argb, dst);
    } else if (s_gpu.blend_enable
        && !(s_gpu.blend_sfactor == 1 && s_gpu.blend_dfactor == 0)) {
]=])
string(REPLACE "${blend_begin}" "${blend_fast}" profile_text "${profile_text}")
string(REPLACE "&& !(s_gpu.blend_sfactor == 1 && s_gpu.blend_dfactor == 0)) {"
    "&& !(s_gpu.blend_sfactor == 1 && s_gpu.blend_dfactor == 0)\n        && !bf_opaque_source_over(argb, s_gpu.blend_sfactor, s_gpu.blend_dfactor, s_gpu.blend_equation)) {"
    profile_text "${profile_text}")
string(REPLACE "static void raster_batch(void)\n{" "static void bf_original_raster_batch(void)\n{" profile_text "${profile_text}")
set(profile_wrapper [=[
static void raster_batch(void)
{
    static int enabled = -1;
    static uint64_t start, total;
    static uint32_t frames, calls;
    uint64_t before, now;
    if (enabled < 0) enabled = getenv("BF_PROFILE_RENDER") != NULL;
    if (!enabled) { bf_original_raster_batch(); return; }
    before = xbox_HostNowNs();
    bf_original_raster_batch();
    now = xbox_HostNowNs(); total += now - before; calls++;
    if (!start) { start = before; frames = s_gpu.flips; }
    if (now - start >= 1000000000ull) {
        uint32_t count = s_gpu.flips - frames;
        fprintf(stderr, "[PERF] renderer %.1f ms/s; %.2f ms/flip; %.1f flips/s; batches=%u blend=%x/%x eq=%x\n",
            total * 1e3 / (double)(now - start), count ? total / 1e6 / count : 0.0,
            count * 1e9 / (double)(now - start), calls,s_gpu.blend_sfactor,s_gpu.blend_dfactor,s_gpu.blend_equation);
        start = now; total = calls = 0; frames = s_gpu.flips;
    }
}

static void frame_trace_flip(void)
]=])
string(REPLACE "static void frame_trace_flip(void)" "${profile_wrapper}" profile_text "${profile_text}")
file(WRITE "${profile_source}.in" "${profile_text}")
configure_file("${profile_source}.in" "${profile_source}" COPYONLY)
