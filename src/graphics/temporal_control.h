#pragma once
#include "game_temporal.h"
#include "gpu/temporal_trace.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t sequence;
    uint32_t color_va,view,context;
    float jitter_x,jitter_y,near_plane,far_plane,vertical_fov;
} BfTemporalView;
int bf_temporal_requested(void);
/* Probe actual provider contexts on the owning scene thread. Capability
 * publication never labels a requested mode as an active reconstruction. */
void bf_temporal_probe_device(ID3D11Device *,ID3D11DeviceContext *,uint32_t width,uint32_t height);
/* Window thread publishes the real reconstruction target after aspect/size
 * fitting. A changed target is configured at the next source-frame boundary. */
void bf_temporal_output_size(uint32_t width,uint32_t height);
/* Producer thread, BEFORE any eligible scene geometry. This issues actual SDK
 * jitter and recovers camera scalar metadata from the ordered view record.
 * Success does not assert that the title shader's depth projection is valid. */
int bf_temporal_prepare_view(ID3D11Device *,ID3D11DeviceContext *,uint64_t sequence,
    uint32_t color_va,uint32_t width,uint32_t height,const BFMotionRecord *,BfTemporalView *);
#ifdef __cplusplus
}
#endif
