#pragma once
#include <stdint.h>
enum {
    BF_MOTION_DRAW_ENABLED=1u,
    BF_MOTION_DRAW_TRACKED=2u,
    BF_MOTION_DRAW_PREVIOUS=4u,
    BF_MOTION_DRAW_JITTER=8u,
    BF_MOTION_DRAW_CANONICAL=16u,
    BF_MOTION_DRAW_TRANSPARENT=32u
};
/* Exactly three 16-byte constant registers, shared by VS/PS b2. */
typedef struct {
    uint32_t flags,previous_count,expected_frame,original_count;
    float width,height,jitter_x,jitter_y;
    float depth_a,depth_b,material_reactivity,reserved;
} BfMotionDrawConstants;

#define BF_MOTION_LOOKUP_HLSL \
"struct BFHistoryPosition {float4 clip;uint id;uint frame;uint valid;uint pad;};\n" \
"bool bf_history_previous(StructuredBuffer<BFHistoryPosition> positions,uint count,\n" \
" uint originalID,uint expectedFrame,out float4 clip) {\n" \
" clip=0;if(expectedFrame==0)return false;uint lo=0,hi=count;\n" \
" while(lo<hi){uint mid=lo+(hi-lo)/2;if(positions[mid].id<originalID)lo=mid+1;else hi=mid;}\n" \
" if(lo>=count)return false;BFHistoryPosition p=positions[lo];\n" \
" if(p.id!=originalID||p.frame!=expectedFrame||!p.valid||!all(isfinite(p.clip))||abs(p.clip.w)<=1e-20)return false;\n" \
" clip=p.clip;return true;}\n"

#define GPU_MOTION_CBUFFER \
"cbuffer BFMotion : register(b2){uint4 bf_motion_info;float4 bf_motion_view;float4 bf_motion_depth;};\n"

#define GPU_MOTION_VARYINGS \
" float4 bf_previous_clip:TEXCOORD5; noperspective float bf_history_valid:TEXCOORD6;\n" \
" float4 bf_capture_clip:TEXCOORD7; nointerpolation uint bf_original_id:TEXCOORD8;\n"

/* Broadcast validity before rasterization: an arbitrarily small/zero
 * interpolation weight must never conceal a missing previous vertex. */
#define GPU_MOTION_TRIANGLE_VALIDITY \
" float bf_triangle_history=min(v[0].bf_history_valid,min(v[1].bf_history_valid,v[2].bf_history_valid));\n" \
" v[0].bf_history_valid=bf_triangle_history;v[1].bf_history_valid=bf_triangle_history;v[2].bf_history_valid=bf_triangle_history;\n"

#define GPU_MOTION_VERTEX_BODY \
BF_MOTION_LOOKUP_HLSL \
"Buffer<uint> bf_original_ids:register(t1);\n" \
"StructuredBuffer<BFHistoryPosition> bf_previous_positions:register(t2);\n" \
"float4 bf_motion_vertex(float4 current,uint vertex,out float4 previous,out float valid,out uint original) {\n" \
" previous=0;valid=0;original=0;uint flags=bf_motion_info.x;\n" \
" if((flags&3)==3 && vertex<bf_motion_info.w){\n" \
"  original=bf_original_ids.Load(vertex);\n" \
"  if((flags&4)!=0 && bf_history_previous(bf_previous_positions,bf_motion_info.y,original,bf_motion_info.z,previous) && previous.w>0 && current.w>0)valid=1;\n" \
" }\n" \
" if((flags&9)==9){current.x-=2*bf_motion_view.z/bf_motion_view.x*current.w;current.y+=2*bf_motion_view.w/bf_motion_view.y*current.w;}\n" \
" return current;}\n"

#define GPU_MOTION_VERTEX_HLSL GPU_MOTION_CBUFFER GPU_MOTION_VERTEX_BODY

/* D3D PS SV_Position.w is interpolated clip W, unlike GL FragCoord.w.
 * The hardware fixture checks primary depth at an actual view-Z of two. */
#define GPU_MOTION_PIXEL_HLSL \
GPU_MOTION_CBUFFER \
"struct BFMotionPixel {float2 motion;float reactive;float canonical;float composition;};\n" \
"BFMotionPixel bf_motion_pixel(float4 position,float4 previous,float valid,float alpha){\n" \
" BFMotionPixel o;uint flags=bf_motion_info.x;bool tracked=(flags&3)==3;\n" \
" o.motion=tracked?float2(0,0):asfloat(uint2(0x7fc00000,0x7fc00000));\n" \
" bool history=tracked && (flags&4)!=0 && valid>.99999 && all(isfinite(previous)) && previous.w>0;\n" \
" if(history){float2 old=previous.xy/previous.w;old=(old*float2(.5,-.5)+.5)*bf_motion_view.xy;\n" \
"  float2 current=position.xy+(((flags&8)!=0)?bf_motion_view.zw:float2(0,0));o.motion=old-current;}\n" \
" o.reactive=(flags&32)!=0?min(saturate(alpha),.9):(history?0:1);o.composition=o.reactive;\n" \
" o.canonical=saturate(bf_motion_depth.x+bf_motion_depth.y/position.w);return o;}\n"
