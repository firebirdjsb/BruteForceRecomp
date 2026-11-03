#include "gpu/frame_interval.h"
#include "nv2a_flip_hold.h"
#include <cstdio>
#include <initializer_list>

int main() {
    const auto require=[](bool pass,const char *message) {
        if(!pass) std::fprintf(stderr,"%s\n",message);
        return pass;
    };
    // Drive the real runtime hold, including its vblank wraparound arithmetic.
    for (uint32_t count : {100u, 0xFFFFFFFEu}) {
        XboxFlipHold original{}, faster{};
        original.edge=faster.edge=1;
        original.have_last=faster.have_last=1;
        original.last_flip=faster.last_flip=count;
        const uint32_t swap=0x12340041u;
        xbox_FlipHoldSwap(&original,bf_swap_interval(swap,30));
        xbox_FlipHoldSwap(&faster,bf_swap_interval(swap,60));
        if(!require(xbox_FlipHoldStall(&original,count,1,1)==1,"Original interval must wait") ||
           !require(xbox_FlipHoldStall(&faster,count,1,1)==1,"Probe must retain vblank pacing") ||
           !require(xbox_FlipHoldPoll(&original,count+1,2,1)==1,"Default must retain two vblanks") ||
           !require(xbox_FlipHoldPoll(&faster,count+1,2,1)==0,"Probe must release at one vblank") ||
           !require(xbox_FlipHoldPoll(&original,count+2,3,1)==0,"Default must release at two vblanks")) return 1;
        if(!require((bf_swap_interval(swap,60)&~0xE0u)==(swap&~0xE0u),"Framebuffer/method bits changed"))return 1;
    }
    const uint32_t untouched[]={0,5,0x12340021u,0x12340061u};
    for(uint32_t parameter:untouched)
        if(!require(bf_swap_interval(parameter,60)==parameter,"Non-TWO method changed"))return 1;
    if(!require(bf_swap_interval(0x12340141u,60)==0x12340121u,"TWO_OR_IMMEDIATE must retain IMMEDIATE"))return 1;
    if(!require(bf_swap_interval(0x12340041u,120)==0x12340041u,"Unsupported request must keep original pacing"))return 1;
    return 0;
}
