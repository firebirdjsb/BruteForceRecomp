#pragma once
#include <stdint.h>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif
/* NV2A/GL SRC_ALPHA, ONE_MINUS_SRC_ALPHA and FUNC_ADD. For opaque source
 * pixels the original floating-point equation is src*1 + dst*0, including
 * alpha. Color masking and target-format packing still run afterward. */
static inline int bf_opaque_source_over(uint32_t color, uint32_t src,
                                        uint32_t dst, uint32_t equation) {
    return (color >> 24) == 255 && src == 0x302 && dst == 0x303 && equation == 0x8006;
}
static inline uint32_t bf_source_over(uint32_t src, uint32_t dst) {
    if ((src >> 24) == 255) return src;
    if (!(src >> 24)) return dst;
#if defined(__SSE2__) || defined(_M_X64)
    const __m128 divisor = _mm_set1_ps(255.0f), one = _mm_set1_ps(1.0f);
    const __m128 s = _mm_div_ps(_mm_cvtepi32_ps(_mm_setr_epi32(src&255,(src>>8)&255,(src>>16)&255,src>>24)),divisor);
    const __m128 d = _mm_div_ps(_mm_cvtepi32_ps(_mm_setr_epi32(dst&255,(dst>>8)&255,(dst>>16)&255,dst>>24)),divisor);
    const __m128 a = _mm_shuffle_ps(s,s,_MM_SHUFFLE(3,3,3,3));
    __m128 result = _mm_add_ps(_mm_mul_ps(s,a),_mm_mul_ps(d,_mm_sub_ps(one,a)));
    result = _mm_min_ps(one,_mm_max_ps(_mm_setzero_ps(),result));
    const __m128i bytes = _mm_cvttps_epi32(_mm_add_ps(_mm_mul_ps(result,divisor),_mm_set1_ps(0.5f)));
    const __m128i packed = _mm_packs_epi32(bytes,bytes);
    return (uint32_t)_mm_cvtsi128_si32(_mm_packus_epi16(packed,packed));
#else
    const float a=(float)(src>>24)/255.0f;
    uint32_t out=0;
    for(unsigned shift=0;shift<32;shift+=8) {
        const float s=(float)((src>>shift)&255)/255.0f,d=(float)((dst>>shift)&255)/255.0f;
        float result=s*a+d*(1.0f-a);
        if(result>1.0f)result=1.0f;
        out|=(uint32_t)(result*255.0f+0.5f)<<shift;
    }
    return out;
#endif
}
