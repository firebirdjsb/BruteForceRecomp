/* A correctness reference shared by future DX11 and DX12 texture uploads.
 * Swizzle masks and bit deposit come from the pinned XboxRecomp runtime. */
#include "gpu/nv2a_p8_decode.h"
#include "d3d/d3d8_swizzle.h"

int bf_nv2a_decode_p8_rgba(const uint8_t *src, size_t src_n,
                           const uint32_t *pal, unsigned entries,
                           uint32_t w, uint32_t h, uint32_t *out, size_t out_n) {
    if (!src || !pal || !out || !w || !h || w > 4096 || h > 4096 ||
        (w & (w - 1u)) || (h & (h - 1u)) ||
        (entries != 32 && entries != 64 && entries != 128 && entries != 256))
        return 0;
    size_t pixels = (size_t)w * h;
    if (src_n < pixels || out_n < pixels)
        return 0;
    /* Reject all invalid entries before mutating the destination. */
    for (size_t i = 0; i < pixels; ++i)
        if (src[i] >= entries)
            return 0;
    uint32_t mx = 0, my = 0;
    xbox_swizzle_masks(w, h, &mx, &my);
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t sy = swizzle_deposit(y, my);
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t addr = swizzle_deposit(x, mx) | sy;
            out[(size_t)y * w + x] = pal[src[addr]];
        }
    }
    return 1;
}
