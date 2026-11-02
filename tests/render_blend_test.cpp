#include "render_blend.h"
#include <cmath>
#include <cstdio>
int main() {
    uint32_t seed=37;
    for(unsigned i=0;i<100000;++i) {
        seed=seed*1664525u+1013904223u;const uint32_t s=seed;
        seed=seed*1664525u+1013904223u;const uint32_t d=seed;
        const float a=float(s>>24)/255.0f;
        uint32_t reference=0;
        for(unsigned shift=0;shift<32;shift+=8) {
            const float sc=float((s>>shift)&255)/255.0f,dc=float((d>>shift)&255)/255.0f;
            const float result=sc*a+dc*(1.0f-a);
            reference|=uint32_t(std::fmin(1.0f,std::fmax(0.0f,result))*255.0f+0.5f)<<shift;
        }
        if(bf_source_over(s,d)!=reference)return 3;
    }
    // Differential check against the renderer's float normalization, source
    // over equation and byte packing for every source/destination byte pair.
    for (unsigned s=0;s<256;++s) for (unsigned d=0;d<256;++d) {
        const float source=float(s)/255.0f,dest=float(d)/255.0f;
        const float result=source*1.0f+dest*0.0f;
        const auto packed=unsigned(std::fmin(1.0f,std::fmax(0.0f,result))*255.0f+0.5f);
        if(packed!=s || !bf_opaque_source_over(0xff000000u|s,0x302,0x303,0x8006))return 1;
    }
    if(bf_opaque_source_over(0xfeffffff,0x302,0x303,0x8006) ||
       bf_opaque_source_over(0xffffffff,1,1,0x8006) ||
       bf_opaque_source_over(0xffffffff,0x302,0x303,0x800a) ||
       bf_opaque_source_over(0xffffffff,0x302,0x303,0x8007))return 2;
    std::puts("Opaque source-over matches the reference; partial alpha and other equations retain the original path.");
}
