#pragma once
#include <d3d11.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    uint32_t render_width,render_height,output_width,output_height;
    uint64_t provider; /* Zero selects the SDK default; never relabel a fallback. */
    const wchar_t *dll_directory; /* Explicit absolute directory of signed SDK DLLs. */
    int inverted_depth,nonlinear_color;
} BfGameTemporalConfig;

enum {
    BF_TEMPORAL_COLOR_BEFORE_UI=1u<<0,
    BF_TEMPORAL_DEPTH_COHERENT=1u<<1,
    BF_TEMPORAL_MOTION_COMPLETE=1u<<2,
    BF_TEMPORAL_PROJECTION_VERIFIED=1u<<3,
    BF_TEMPORAL_JITTER_APPLIED=1u<<4,
    BF_TEMPORAL_SOURCE_COMPLETE=1u<<5,
    BF_TEMPORAL_MASKS_COHERENT=1u<<6,
    /* The renderer has proved this source has no transparent/reactive effects.
     * This is the only way to omit both masks; it is not inferred from pixels. */
    BF_TEMPORAL_OPAQUE_SCENE=1u<<7
};
typedef struct {
    ID3D11Texture2D *color,*depth,*motion,*reactive,*composition;
    uint64_t sequence,scene;
    uint32_t color_va,provenance;
    float jitter_x,jitter_y,elapsed_ms,near_plane,far_plane,vertical_fov;
    int reset;
    /* Optional complete native HUD, from exactly the same source surface and
     * generation. A GPU copy makes each published ticket immutable. */
    ID3D11Texture2D *hud;
    uint64_t hud_sequence;
    uint32_t hud_color_va;
} BfTemporalInputs;

/* Render-thread-only lifecycle. The context must be an immediate D3D11On12
 * context created by the scene bridge. No SDK build => unavailable/fail closed.
 * Configuration, rejection or missing provenance never enables another path.
 * Before submit, unbind the supplied resources from D3D11 OM/SRV/UAV use;
 * the synchronous native bridge checks them out and returns them in COMMON. */
int bf_game_temporal_available(void);
int bf_game_temporal_configure(const BfGameTemporalConfig *);
int bf_game_temporal_ready(void);
/* Call before drawing this actual source. Submit must carry this exact SDK
 * jitter after the renderer applied it; no motion/depth/jitter is synthesized. */
int bf_game_temporal_next_jitter(uint64_t sequence,float *x,float *y);
int bf_game_temporal_submit(const BfTemporalInputs *);
const char *bf_game_temporal_provider(void);
/* After configuring auto, enumerate actual SDK IDs/labels on the render thread
 * before reconfiguring an explicitly requested family. No hardcoded IDs. */
uint32_t bf_game_temporal_provider_count(void);
int bf_game_temporal_provider_at(uint32_t index,uint64_t *id,char *label,uint32_t capacity);
const char *bf_game_temporal_status(void);
/* After stopping the render thread, shutdown may run on the joining thread. */
void bf_game_temporal_shutdown(void);
#ifdef __cplusplus
}

#include "dx12_presenter.h"
#include "dx12_device.h"
#include <memory>
#include <string>
#include <wrl/client.h>
namespace bf::graphics {
/* A retained output lease. Keep the ticket until dx12_frame_texture has
 * submitted its sampling on the shared queue. It then retains the resources.
 * Do not keep a raw view, release this lease, and present that view later:
 * releasing the last lease permits pool reuse on the same ordered queue. */
struct GameTemporalFrame {
    uint64_t sequence{},scene{};
    uint32_t color_va{},width{},height{};
    Microsoft::WRL::ComPtr<ID3D12Resource> color,hud;
    std::shared_ptr<Dx12DeviceContext> owner;
    std::string provider; // Immutable actual SDK provider for this output.
    Dx12TextureFrame presentation() const;
};
/* Producer/render-thread lookup only. Pass a retained ticket through the
 * completed framebuffer handoff; the window thread must not ask for "latest". */
std::shared_ptr<const GameTemporalFrame> game_temporal_take_frame(uint64_t sequence,uint32_t color_va);
std::shared_ptr<const GameTemporalFrame> game_temporal_latest_frame(uint32_t color_va);
}
#endif
