/* Backend-neutral NV2A P8 swizzled-paletted texture decode.
 * Based on Tzeentchnet/BruteForce's MIT gpu_texture_decode_p8_cpu path.
 * Caller validates mapped guest bounds; this helper validates host buffer
 * lengths before reading. No DirectX dependency. */
#ifndef BF_NV2A_P8_DECODE_H
#define BF_NV2A_P8_DECODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int bf_nv2a_decode_p8_rgba(const uint8_t *indices, size_t source_bytes,
                           const uint32_t *palette, unsigned palette_entries,
                           uint32_t width, uint32_t height,
                           uint32_t *rgba, size_t output_pixels);
#ifdef __cplusplus
}
#endif
#endif
