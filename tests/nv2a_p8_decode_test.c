/* Swizzled Xbox P8 palette conversion: synthetic rectangular Morton inputs. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gpu/nv2a_p8_decode.h"
#include "d3d/d3d8_swizzle.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "P8 test failed line %d: %s\n",__LINE__,#c); return 1; } } while (0)

int main(void) {
    enum { W=8, H=4, N=W*H };
    uint8_t indices[N] = {0};
    uint32_t palette[32], output[N];
    for (unsigned i=0;i<32;i++) palette[i]=0xFF000000u | (i*0x010101u);
    for (unsigned y=0;y<H;y++) {
        for (unsigned x=0;x<W;x++) {
            uint32_t off=swizzle_offset(x,y,W,H);
            CHECK(off<N);
            indices[off]=(uint8_t)((x+y*3u)&31u);
        }
    }
    CHECK(bf_nv2a_decode_p8_rgba(indices,N,palette,32,W,H,output,N));
    for (unsigned y=0;y<H;y++)
        for (unsigned x=0;x<W;x++)
            CHECK(output[(size_t)y*W+x]==palette[(x+y*3u)&31u]);
    uint8_t invalid[N];
    memcpy(invalid,indices,N);
    invalid[swizzle_offset(1,2,W,H)]=32;
    for (unsigned i=0;i<N;i++)output[i]=0xDEADBEEFu;
    CHECK(!bf_nv2a_decode_p8_rgba(invalid,N,palette,32,W,H,output,N));
    for (unsigned i=0;i<N;i++)CHECK(output[i]==0xDEADBEEFu);
    CHECK(!bf_nv2a_decode_p8_rgba(indices,N-1,palette,32,W,H,output,N));
    CHECK(!bf_nv2a_decode_p8_rgba(indices,N,palette,33,W,H,output,N));
    CHECK(!bf_nv2a_decode_p8_rgba(indices,N,palette,32,7,H,output,N));
    CHECK(!bf_nv2a_decode_p8_rgba(indices,N,palette,32,W,H,output,N-1));
    puts("NV2A swizzled P8 palette decode, bad indices and output guards passed");
    return 0;
}
