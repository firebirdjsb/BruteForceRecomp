/* HLSL pixel shaders from NV2A register-combiner state; see rc_hlsl.c.
 * Adapted from Blood Wake's bw_rc_hlsl.h (MIT; THIRD_PARTY_NOTICES.md). */
#ifndef GPU_RC_HLSL_H
#define GPU_RC_HLSL_H

#include <stddef.h>
#include <stdint.h>
#include "motion_hlsl.h"

/* Everything that changes the shader's code. Constants (c0/c1, the final
 * combiner's constants, fog colour, texture sizes, alpha test) live in the
 * constant buffer instead, so a material colour change does not compile. */
typedef struct {
    uint32_t color_icw[8], alpha_icw[8], color_ocw[8], alpha_ocw[8];
    uint32_t final0, final1, control, stage_program, clip_plane_mode;
    uint32_t shadow_func;
    uint8_t tex_valid[4], tex_normalized[4], tex_cube[4], tex_depth[4];
} GpuRcKey;

#define GPU_WINDOW_CLIP_HLSL \
    "cbuffer WindowClip : register(b1) { uint4 clip_rect[8]; uint4 clip_info; };\n" \
    "bool window_allows(float2 pos) {\n" \
    "  if (clip_info.x == 0) return true;\n" \
    "  uint2 p = (uint2)pos; p.x >>= clip_info.z; p.y >>= clip_info.w;\n" \
    "  bool inside = false;\n" \
    "  [loop] for (uint j = 0; j < clip_info.x; ++j) {\n" \
    "    uint4 r = clip_rect[j];\n" \
    "    inside = inside || (p.x >= r.x && p.x <= r.y && p.y >= r.z && p.y <= r.w);\n" \
    "  }\n" \
    "  return clip_info.y != 0 ? !inside : inside;\n" \
    "}\n"

#define GPU_SHADOW_HLSL \
    "float shadow_compare(float depth, float ref, uint f) {\n" \
    "  bool ok = f == 7 || (f == 1 && depth < ref) || (f == 2 && depth == ref)\n" \
    "    || (f == 3 && depth <= ref) || (f == 4 && depth > ref)\n" \
    "    || (f == 5 && depth != ref) || (f == 6 && depth >= ref);\n" \
    "  return ok ? 1.0 : 0.0;\n" \
    "}\n"

/* 0/1 retain identity; legacy .75 is centered. Ordered widget markers pack
 * 2/3/4 as left/center/right, one affine transform for the entire draw. */
#define GPU_UI_ANCHOR_HLSL \
    "float ui_x(float x,float mode) {\n" \
    "  return mode>=2.0 && mode<=4.0 ? x*.75+(mode-3.0)*.25 : x*(mode>0.0?mode:1.0);\n" \
    "}\n"

/* A separate MRT preserves the original RT0 value and the same fragment
 * coverage/alpha test/depth/stencil/scissor result. PS b3 is independent of
 * original base/combiner constants and programmable VS constants. */
#define GPU_UI_CAPTURE_HLSL \
GPU_MOTION_PIXEL_HLSL \
    "cbuffer UiCapture : register(b3) { uint4 ui_capture; };\n" \
    "struct UiPixelOutput { float4 original:SV_Target0;float2 motion:SV_Target1;float reactive:SV_Target2;float4 overlay:SV_Target3;float canonical:SV_Target4;float composition:SV_Target5; };\n" \
    "UiPixelOutput ui_pixel_output(float4 c,float4 position,float4 previous,float valid) {\n" \
    "  UiPixelOutput o; o.original=c; float4 v=saturate(c);\n" \
    "  BFMotionPixel m=bf_motion_pixel(position,previous,valid,v.a);o.motion=m.motion;o.reactive=m.reactive;o.canonical=m.canonical;o.composition=m.composition;\n" \
    "  o.overlay=ui_capture.x==1 ? float4(v.rgb,1) :\n" \
    "    ui_capture.x==2 ? float4(v.rgb*v.a,v.a) : float4(0,0,0,0); return o;\n" \
    "}\n"

/* The constant buffer both shaders read (register b0):
 *   surf     surface width, height, 1/width, 1/height (real pixels)
 *   depth    z offset, z scale to 0..1, clamp (1/0), -
 *   alpha    alpha function (1 LESS .. 7 ALWAYS), reference 0..255, enabled,
 *            UI transform (0/1 identity; .75 legacy center; 2/3/4 anchors)
 *   fogc     fog colour
 *   C0, C1   per-stage combiner constants; FC0, FC1 the final combiner's
 *   texinfo  per stage: 1/width, 1/height for texel-space coordinates */
#define GPU_RC_CBUFFER \
    "cbuffer C : register(b0) {\n" \
    "  float4 surf; float4 depth; float4 alpha; float4 fogc;\n" \
    "  float4 C0[8]; float4 C1[8]; float4 FC0; float4 FC1;\n" \
    "  float4 texinfo[4];\n" \
    "};\n"

typedef struct {
    float surf[4], depth[4], alpha[4], fogc[4];
    float c0[8][4], c1[8][4], fc0[4], fc1[4];
    float texinfo[4][4];
} GpuRcConsts;

/* Write the pixel shader (entry point ps_main) for `k` into out. Returns the
 * length, or 0 if it did not fit. */
size_t gpu_rc_hlsl(const GpuRcKey *k, char *out, size_t cap);

#endif
