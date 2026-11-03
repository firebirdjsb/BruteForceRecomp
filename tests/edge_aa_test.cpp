#include "graphics/edge_aa.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"AA failed line %d\n",__LINE__);return 1;}}while(0)
int main() {
    bf::graphics::EdgeAA aa;
    std::vector<uint32_t> flat(640*480,0x7f407080),out=flat;
    CHECK(aa.apply(out.data(),640,480)&&out==flat);
    CHECK(!aa.apply(nullptr,640,480)&&!aa.apply(out.data(),8193,480));
    std::vector<uint32_t> diagonal(640*480);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)diagonal[y*640+x]=x>y?0x7fffffff:0x7f000000;
    out=diagonal;CHECK(aa.apply(out.data(),640,480)&&out!=diagonal);
    unsigned blended=0;
    for(size_t i=0;i<out.size();i++) {
        CHECK((out[i]&0xff000000u)==0x7f000000u);
        if((out[i]&255)!=0&&(out[i]&255)!=255)blended++;
    }
    CHECK(blended>400&&blended<6000); // Localized edges, not a full-screen blur.
    auto cached=diagonal;CHECK(aa.apply(cached.data(),640,480)&&cached==out);
    flat.assign(flat.size(),0xffd02060);CHECK(aa.apply(flat.data(),640,480));
    CHECK(std::all_of(flat.begin(),flat.end(),[](auto p){return p==0xffd02060;}));
    // Resizing and returning to an older frame must invalidate cached pixels.
    std::vector<uint32_t> small(12*9,0xff101010);CHECK(aa.apply(small.data(),12,9));
    cached=diagonal;CHECK(aa.apply(cached.data(),640,480)&&cached==out);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<60;i++){cached=diagonal;cached[1000]^=i;CHECK(aa.apply(cached.data(),640,480));}
    std::printf("AA edge/alpha/cache/resize checks passed; 640x480 fresh-frame average %.3f ms\n",
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/60);
}
