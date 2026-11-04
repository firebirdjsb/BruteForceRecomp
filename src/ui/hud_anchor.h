#pragma once
#include <stdint.h>
enum { BF_HUD_LEFT=0, BF_HUD_CENTER=1, BF_HUD_RIGHT=2 };
/* Exactly one outer component owns every descendant, including raw geometry.
 * The token contains the prior depth/anchor; unknown root draws stay centered. */
typedef struct {unsigned depth,anchor;} BfHudOwnerScope;
static inline uint32_t bf_hud_scope_enter(BfHudOwnerScope *scope,unsigned root_anchor) {
    uint32_t token;
    if(!scope||scope->depth>=64)return 0;
    token=0x80000000u|(scope->depth<<2)|scope->anchor;
    if(!scope->depth)scope->anchor=root_anchor<=BF_HUD_RIGHT?root_anchor:BF_HUD_CENTER;
    ++scope->depth;return token;
}
static inline void bf_hud_scope_leave(BfHudOwnerScope *scope,uint32_t token) {
    const unsigned depth=(token&0x7FFFFFFFu)>>2;
    if(scope&&(token&0x80000000u)&&scope->depth==depth+1){scope->depth=depth;scope->anchor=token&3u;}
}
#ifdef __cplusplus
extern "C" {
#endif
unsigned bf_hud_owner_anchor(uint32_t component,int menu);
/* Diagnostic only: 1=sprite, 2=quad, 3=font, 4=complete component draw. */
uint32_t bf_hud_trace_begin(unsigned stage,uint32_t frame,uint32_t component);
int bf_hud_trace_configured(void);
void bf_hud_trace_end(uint32_t token);
void bf_hud_trace_rect(uint32_t token,float x,float width,unsigned anchor);
#ifdef __cplusplus
}
#endif
