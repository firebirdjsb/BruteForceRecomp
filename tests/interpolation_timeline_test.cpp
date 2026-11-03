#include "graphics/interpolation_timeline.h"
#include <cstdio>
int main() {
    using namespace bf::graphics;
    for(unsigned mode:{60u,120u}) {
        InterpolationTimeline clock;const uint64_t start=1000000000,step=33333332;
        auto result=clock.advance({1,start,start,mode});if(!result.upload||result.pair)return 1;
        result=clock.advance({2,start+step,start+step,mode});if(!result.upload||!result.pair||result.alpha!=0)return 2;
        for(unsigned i=1;i<mode/30;++i) {
            result=clock.advance({2,start+step,start+step+step*i/(mode/30)+1,mode});
            if(result.upload||!result.pair||result.alpha!=float(i)/(mode/30))return 3;
        }
        result=clock.advance({2,start+step,start+step*6,mode});if(result.pair)return 4;
        result=clock.advance({4,start+step*7,start+step*7,mode});if(result.pair||!result.reset)return 5;
        result=clock.advance({5,start+step*8,start+step*8,0});if(result.pair||!result.reset)return 6;
        result=clock.advance({6,start+step*9,start+step*9,mode});if(result.pair)return 7;
        result=clock.advance({7,start+step*10,start+step*10-1,mode});if(result.pair)return 8;
    }
    std::puts("30 Hz source history yields 2x/4x visual phases; stalls, skips, mode changes and invalid clocks reset history.");return 0;
}
