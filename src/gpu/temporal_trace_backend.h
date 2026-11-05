#pragma once
#include "temporal_trace.h"
#include "nv2a_backend_ext.h"
#ifdef __cplusplus
extern "C" {
#endif
void bf_motion_trace_draw(const Nv2aSurface *surface,const Nv2aRenderState *state,
                          unsigned path,const Nv2aXVsBatch *vertices,uint32_t frame,int surface_ui);
#ifdef __cplusplus
}
#endif
