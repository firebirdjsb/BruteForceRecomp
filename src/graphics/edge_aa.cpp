#include "edge_aa.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bf::graphics {
namespace {
struct Color {
    float b,g,r;
    Color operator+(Color v)const{return {b+v.b,g+v.g,r+v.r};}
    Color operator*(float v)const{return {b*v,g*v,r*v};}
};
Color unpack(uint32_t v){return {float(v&255),float((v>>8)&255),float((v>>16)&255)};}
float luma(Color v){return v.r*0.299f+v.g*0.587f+v.b*0.114f;}
uint32_t pack(Color c,uint32_t alpha) {
    const auto byte=[](float v){return uint32_t(std::clamp(std::lround(v),0l,255l));};
    return alpha|(byte(c.r)<<16)|(byte(c.g)<<8)|byte(c.b);
}
}
bool EdgeAA::apply(uint32_t *pixels,unsigned width,unsigned height) {
    if(!pixels||width<3||height<3||width>8192||height>8192||uint64_t(width)*height>16777216)return false;
    const size_t count=size_t(width)*height,bytes=count*sizeof(uint32_t);
    if(ready_&&width_==width&&height_==height&&source_.size()==count&&!std::memcmp(source_.data(),pixels,bytes)) {
        std::memcpy(pixels,filtered_.data(),bytes);return true;
    }
    ready_=false;
    source_.assign(pixels,pixels+count);filtered_=source_;width_=width;height_=height;
    luma_.resize(count);
    for(size_t i=0;i<count;i++) {
        const auto p=source_[i];luma_[i]=uint8_t((((p>>16)&255)*77+((p>>8)&255)*150+(p&255)*29)>>8);
    }
    const auto sample=[&](float x,float y) {
        x=std::clamp(x,0.f,float(width-1));y=std::clamp(y,0.f,float(height-1));
        const unsigned ix=unsigned(x),iy=unsigned(y),nx=std::min(ix+1,width-1),ny=std::min(iy+1,height-1);
        const float fx=x-ix,fy=y-iy;
        return (unpack(source_[size_t(iy)*width+ix])*(1-fx)+unpack(source_[size_t(iy)*width+nx])*fx)*(1-fy)+
               (unpack(source_[size_t(ny)*width+ix])*(1-fx)+unpack(source_[size_t(ny)*width+nx])*fx)*fy;
    };
    // Original implementation of local-contrast edge-direction filtering.
    // Flat/low-contrast pixels are exact copies. Filter only along the local
    // edge, clamp its span to eight source texels and reject luma overshoot.
    for(unsigned y=1;y+1<height;y++)for(unsigned x=1;x+1<width;x++) {
        const size_t at=size_t(y)*width+x;
        const float lm=luma_[at],nw=luma_[at-width-1],ne=luma_[at-width+1];
        const float sw=luma_[at+width-1],se=luma_[at+width+1];
        const float lo=std::min({lm,nw,ne,sw,se}),hi=std::max({lm,nw,ne,sw,se});
        if(hi-lo<std::max(8.f,hi*0.125f))continue;
        float dx=-((nw+ne)-(sw+se)),dy=(nw+sw)-(ne+se);
        const float reduce=std::max((nw+ne+sw+se)*(0.25f*0.125f),2.f);
        const float reciprocal=1.f/(std::min(std::abs(dx),std::abs(dy))+reduce);
        dx=std::clamp(dx*reciprocal,-8.f,8.f);dy=std::clamp(dy*reciprocal,-8.f,8.f);
        const Color a=(sample(x-dx/6,y-dy/6)+sample(x+dx/6,y+dy/6))*0.5f;
        const Color b=a*0.5f+(sample(x-dx*0.5f,y-dy*0.5f)+sample(x+dx*0.5f,y+dy*0.5f))*0.25f;
        const float lb=luma(b);filtered_[at]=pack(lb<lo||lb>hi?a:b,source_[at]&0xff000000u);
    }
    ready_=true;std::memcpy(pixels,filtered_.data(),bytes);return true;
}
}
