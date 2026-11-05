#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "graphics/ui_composition.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
using Pixel=std::array<double,4>;
void require(bool value,const char *why){if(!value)throw std::runtime_error(why);}
void expect_near(double a,double b,const char *why){require(std::abs(a-b)<1e-12,why);}
// The original RGB blend equation and the independent affine accumulation use
// different representations. Their equivalence must hold over entire ordered
// sequences and arbitrary world colours, including zero/full source coverage.
void sequence(const std::array<Pixel,6> &sources,const std::array<BfUiBlendMode,6> &modes) {
    for(const Pixel world:{Pixel{0,0,0,1},Pixel{1,1,1,1},Pixel{.1,.73,.25,1},Pixel{.91,.2,.44,1}}) {
        Pixel original=world,overlay{};
        for(size_t j=0;j<sources.size();++j) {
            const auto &source=sources[j];const auto mode=modes[j];
            if(mode==BF_UI_BLEND_NO_RGB)continue;
            const double source_factor=mode==BF_UI_BLEND_OPAQUE?1:source[3];
            const double destination_factor=mode==BF_UI_BLEND_OPAQUE?0:1-source[3];
            for(size_t channel=0;channel<3;++channel)
                original[channel]=source[channel]*source_factor+original[channel]*destination_factor;
            const double coverage=mode==BF_UI_BLEND_OPAQUE?1:source[3];
            for(size_t channel=0;channel<3;++channel)
                overlay[channel]=source[channel]*source_factor+overlay[channel]*(1-coverage);
            overlay[3]=coverage+overlay[3]*(1-coverage);
            for(size_t channel=0;channel<3;++channel)
                expect_near(overlay[channel]+world[channel]*(1-overlay[3]),original[channel],"Ordered affine composition differs from original blend");
        }
    }
}
}
int main(){
    try {
        require(!bf_ui_composition_enabled(),"Capture must start disabled");
        bf_ui_composition_request(1);require(bf_ui_composition_enabled(),"Explicit capture request");
        bf_ui_composition_request(0);require(!bf_ui_composition_enabled(),"Capture off path");
        constexpr uint32_t rgb=0x00010101u;
        require(bf_ui_blend_mode(0,0x306,0x300,0x800A,rgb)==BF_UI_BLEND_OPAQUE,"Disabled blend ignores irrelevant blend fields");
        require(bf_ui_blend_mode(1,1,0,0x8006,rgb)==BF_UI_BLEND_OPAQUE,"ONE/ZERO opaque equation");
        require(bf_ui_blend_mode(1,0x302,0x303,0x8006,rgb)==BF_UI_BLEND_SOURCE_ALPHA,"Straight source-over policy");
        require(bf_ui_blend_mode(1,0x306,0x300,0x800A,0x01000000)==BF_UI_BLEND_NO_RGB,"Alpha-only original draws do not alter overlay");
        for(auto source:{0u,1u,0x300u,0x301u,0x304u,0x305u,0x306u,0x307u,0x308u,0x8001u})
            require(bf_ui_blend_mode(1,source,0x303,0x8006,rgb)==BF_UI_BLEND_UNSUPPORTED,"Unproved blend must reject whole capture");
        for(auto equation:{0x8007u,0x8008u,0x800Au,0x800Bu})
            require(bf_ui_blend_mode(1,0x302,0x303,equation,rgb)==BF_UI_BLEND_UNSUPPORTED,"Non-additive blend rejection");
        require(bf_ui_blend_mode(1,0x302,1,0x8006,rgb)==BF_UI_BLEND_UNSUPPORTED,"Additive intermediate-clipping rejection");
        for(auto mask:{1u,0x100u,0x10000u,0x101u,0x10100u,0x10001u})
            require(bf_ui_blend_mode(0,1,0,0,mask)==BF_UI_BLEND_UNSUPPORTED,"Partial RGB mask rejection");
        const std::array<Pixel,6> sources={Pixel{.9,.1,.5,0},Pixel{.2,.8,.3,.25},Pixel{.7,.4,.1,.6},
            Pixel{.1,.2,.9,1},Pixel{.9,.4,.7,.3},Pixel{.1,.9,.4,.75}};
        sequence(sources,{BF_UI_BLEND_SOURCE_ALPHA,BF_UI_BLEND_SOURCE_ALPHA,BF_UI_BLEND_SOURCE_ALPHA,
            BF_UI_BLEND_SOURCE_ALPHA,BF_UI_BLEND_SOURCE_ALPHA,BF_UI_BLEND_SOURCE_ALPHA});
        sequence(sources,{BF_UI_BLEND_OPAQUE,BF_UI_BLEND_SOURCE_ALPHA,BF_UI_BLEND_NO_RGB,
            BF_UI_BLEND_SOURCE_ALPHA,BF_UI_BLEND_OPAQUE,BF_UI_BLEND_SOURCE_ALPHA});
        // Demonstrate why accepting additive/premultiplied without a range
        // proof would be wrong: intermediate UNORM clipping followed by a fade
        // is nonlinear and cannot be encoded by one background-independent RGBA.
        const double original=std::min(1.0,.8+.8)*.5;
        const double naive_affine=std::min(1.0,(.8+.8)*.5);
        require(std::abs(original-naive_affine)>.2,"Clipping counterexample must distinguish unsafe affine capture");
        std::puts("PASS default-off capture, conservative original-state policy and ordered affine RGB equations");
    }catch(const std::exception &error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
    return 0;
}
