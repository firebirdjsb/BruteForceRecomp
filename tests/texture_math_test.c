/* Compare the adapted fast path to the pinned xboxrecomp scalar reference. */
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include "bf_texture_sample.h"

static uint32_t reference_wrap(uint32_t c, uint32_t size, uint32_t mode)
{
    int32_t sc = (int32_t)c, n = (int32_t)size;
    if (!size) return 0;
    if (mode == 1) return (uint32_t)(((sc % n) + n) % n);
    if (mode == 2) {
        int32_t m = ((sc % (2 * n)) + 2 * n) % (2 * n);
        return (uint32_t)(m < n ? m : 2 * n - 1 - m);
    }
    return sc < 0 ? 0u : (sc >= n ? size - 1 : c);
}

int main(void)
{
    static const uint32_t sizes[] = {0,1,2,3,7,16,63,128,255,256,512,4096,32768,65535};
    static const int32_t coords[] = {INT32_MIN,-65537,-4097,-513,-257,-16,-2,-1,0,1,2,15,16,255,256,511,512,4096,65535,INT32_MAX};
    static const uint32_t blocks[] = {1,3,7,8,16,24,32,255};
    uint64_t state = UINT64_C(0x915A3E17B5B54D29);
    for (size_t s=0;s<sizeof(sizes)/sizeof(sizes[0]);++s) {
        for (uint32_t mode=0;mode<16;++mode) {
            for (size_t i=0;i<sizeof(coords)/sizeof(coords[0]);++i) {
                const uint32_t c = (uint32_t)coords[i];
                if (bf_texture_wrap_coord(c,sizes[s],mode) != reference_wrap(c,sizes[s],mode)) return 1;
            }
            for (int i=0;i<2048;++i) {
                state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                uint32_t c=(uint32_t)(state>>32);
                if (bf_texture_wrap_coord(c,sizes[s],mode) != reference_wrap(c,sizes[s],mode)) return 2;
            }
        }
    }
    for (int i=0;i<1000000;++i) {
        uint32_t block=blocks[i%(sizeof(blocks)/sizeof(blocks[0]))];
        state=state*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
        uintptr_t key=(uintptr_t)state;
        size_t expected=((key/block)^(key>>16))&4095u;
        if (bf_texture_dxt_slot(key,block)!=expected) return 3;
    }
    puts("texture math tests passed");
    return 0;
}
