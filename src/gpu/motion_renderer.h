#pragma once
#include <d3d11.h>
#include <stdint.h>
#include <stddef.h>
#include "motion_hlsl.h"
#include "temporal_trace.h"
#include "nv2a_backend_ext.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct BfMotionRenderer BfMotionRenderer;
typedef struct {
    uint32_t frame,color_va,depth_va,width,height,view,context;
    float near_plane,far_plane,jitter_x,jitter_y;
    uint32_t apply_jitter;
} BfMotionSource;
enum {BF_MOTION_BIND_VECTOR=1u,BF_MOTION_BIND_MASKS=2u,BF_MOTION_BIND_DEPTH=4u};
typedef struct {
    ID3D11Buffer *constants;
    ID3D11ShaderResourceView *original_ids,*previous;
    uint32_t outputs,tracked,previous_valid,projection_valid,jitter_applied;
} BfMotionDrawBinding;
typedef struct BfMotionSnapshot {
    BfMotionSource source;
    uint32_t draws,tracked_draws,previous_draws,new_history_draws,transparent_draws;
    uint32_t untracked_opaque,unsupported_projection,resource_failures;
    uint32_t clear_observed,jitter_applied,requires_reset,complete;
    ID3D11Texture2D *motion,*reactive,*composition,*canonical_depth;
} BfMotionSnapshot;
/* Snapshot texture pointers are borrowed on the producer thread, immutable
 * only until the next generation/reset/teardown. Submit any retaining copy or
 * bridge access before that mutation; never publish these as window tickets.
 * `complete` covers the draws observed so far. The caller must additionally
 * prove the actual source boundary and detach these RTVs before bridge access;
 * a mid-scene snapshot is never proof that the whole scene has completed. */
BfMotionRenderer *bf_motion_renderer_create(ID3D11Device *,ID3D11DeviceContext *);
void bf_motion_renderer_destroy(BfMotionRenderer *);
/* A real full far-depth clear initializes the known clear background. Later
 * post-effect clears never erase the retained world attachments. */
void bf_motion_renderer_clear(BfMotionRenderer *,const BfMotionSource *,int full_far_depth_clear);
/* Before each actual source draw. The resolved record must come from
 * bf_motion_draw_record(original BEGIN VA), never inferred draw/VB identity.
 * Unknown opaque work remains explicit and makes the source incomplete. */
int bf_motion_renderer_prepare(BfMotionRenderer *,const BfMotionSource *,const Nv2aRenderState *,
    const Nv2aXVsBatch *,const BFMotionRecord *,BfMotionDrawBinding *);
/* Borrowed RTVs at slots 1/2/4/5. Bind only for this source's current draw;
 * original RT0, DSV and UI slot3 are always owned by the original backend. */
void bf_motion_renderer_targets(BfMotionRenderer *,uint32_t color_va,uint32_t frame,
    ID3D11RenderTargetView **motion,ID3D11RenderTargetView **reactive,
    ID3D11RenderTargetView **depth,ID3D11RenderTargetView **composition);
/* Run the already-bound actual VS again as non-rasterized points to capture
 * its emitted unjittered native clip float4 and original ID entirely on GPU.
 * Existing IA streams/layout/VS/CB bindings remain the real consumed inputs. */
int bf_motion_renderer_capture(BfMotionRenderer *,const void *vs_bytecode,size_t bytes,uint32_t vertices);
void bf_motion_renderer_complete(BfMotionRenderer *,uint32_t frame);
void bf_motion_renderer_surface_reset(BfMotionRenderer *,uint32_t color_va);
void bf_motion_renderer_resource_failure(BfMotionRenderer *,uint32_t color_va,uint32_t frame);
int bf_motion_renderer_snapshot(BfMotionRenderer *,uint32_t color_va,uint32_t frame,BfMotionSnapshot *);
/* Dynamic constants are checked as well as an audited actual program body.
 * A positive result proves OP.w is primary view-Z, including large-far sky. */
int bf_motion_primary_projection(const Nv2aXVsBatch *,const BfMotionSource *);
#ifdef __cplusplus
}
#endif
