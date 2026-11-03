#include "nv2a_flip_hold.h"
#include "graphics/interpolation_timeline.h"
#include <cstdio>
int main() {
    for(unsigned output:{0u,60u,120u}) {
        XboxFlipHold hold{};hold.edge=1;hold.have_last=1;hold.last_flip=100;
        const uint32_t swap=0x05600141u;
        if(!xbox_FlipHoldSwap(&hold,swap)||hold.swap_interval!=2||!hold.swap_immediate)return 1;
        bf::graphics::InterpolationTimeline visuals;
        visuals.advance({1,1000000000,1000000000,output});
        visuals.advance({2,1033333333,1050000000,output});
        if(!xbox_FlipHoldStall(&hold,100,1,1)||hold.target!=102)return 2;
        if(!xbox_FlipHoldPoll(&hold,101,2,1)||xbox_FlipHoldPoll(&hold,102,3,1))return 3;
    }
    std::puts("All visual modes retain the original TWO_OR_IMMEDIATE game pacing.");return 0;
}
