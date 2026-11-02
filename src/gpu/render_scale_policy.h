/* Portable logical-vs-physical render sizing policy for DX11, DX12 and
 * future Vulkan. This never changes guest logical coordinates or texture
 * layout. Adapted from MIT Tzeentchnet/BruteForce render_scale dimensions.
 * Integration into the NV2A render target and swap-chain is still pending. */
#ifndef BF_GPU_RENDER_SCALE_POLICY_H
#define BF_GPU_RENDER_SCALE_POLICY_H
#include <stdint.h>
#include <math.h>

typedef struct {
    uint32_t x, y, width, height;
} BfGpuViewport;

static inline int bf_gpu_scaled_surface(uint32_t w, uint32_t h,
                                        uint32_t aa_x, uint32_t aa_y,
                                        double scale,
                                        uint32_t *out_w, uint32_t *out_h) {
    if (!out_w || !out_h || !w || !h || !isfinite(scale) ||
        scale < 0.5 || scale > 10.0)
        return 0;
    double sx = scale / (aa_x > 1 ? aa_x : 1);
    double sy = scale / (aa_y > 1 ? aa_y : 1);
    double pw = floor((double)w * sx + 0.5);
    double ph = floor((double)h * sy + 0.5);
    if (pw < 1.0 || ph < 1.0 || pw > 16384.0 || ph > 16384.0)
        return 0;
    *out_w = (uint32_t)pw;
    *out_h = (uint32_t)ph;
    return 1;
}

/* Match output height to logical height, but never independently stretch X.
 * A 4:3 game still produces a 4:3 game frame on a 16:9 monitor.
 * Native widescreen requires additional *verified guest-visible geometry*. */
static inline int bf_gpu_native_scale(uint32_t logical_h, uint32_t output_h,
                                      double *scale) {
    if (!scale || !logical_h || !output_h)
        return 0;
    double next = (double)output_h / logical_h;
    if (next < 0.5 || next > 10.0 || !isfinite(next))
        return 0;
    *scale = next;
    return 1;
}

/* Center and preserve the guest aspect in a display surface; no crop or
 * anamorphic stretch. Backend owns the actual sampler and resolve operation. */
static inline int bf_gpu_fit_viewport(uint32_t src_w, uint32_t src_h,
                                      uint32_t dst_w, uint32_t dst_h,
                                      BfGpuViewport *view) {
    if (!view || !src_w || !src_h || !dst_w || !dst_h)
        return 0;
    double ratio_w = (double)dst_w / src_w;
    double ratio_h = (double)dst_h / src_h;
    double scale = ratio_w < ratio_h ? ratio_w : ratio_h;
    uint32_t w = (uint32_t)floor((double)src_w * scale + 0.5);
    uint32_t h = (uint32_t)floor((double)src_h * scale + 0.5);
    if (!w || !h || w > dst_w || h > dst_h)
        return 0;
    BfGpuViewport result = {(dst_w - w) / 2, (dst_h - h) / 2, w, h};
    *view = result;
    return 1;
}
#endif
