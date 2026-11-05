#pragma once
#include <d3d11.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Overlay RGB is premultiplied scene-independent colour; alpha is coverage.
 * The final RGB is overlay.rgb + scene.rgb * (1-overlay.a). */
typedef enum {
    BF_UI_BLEND_UNSUPPORTED = -1,
    BF_UI_BLEND_NO_RGB = 0,
    BF_UI_BLEND_OPAQUE = 1,
    BF_UI_BLEND_SOURCE_ALPHA = 2
} BfUiBlendMode;
/* NV2A blend enums and byte-spaced A/R/G/B write mask. Destination-dependent
 * blends, partial RGB masks, additive and unproven premultiplied inputs reject
 * the entire capture, because their intermediate UNORM clipping is not affine. */
BfUiBlendMode bf_ui_blend_mode(uint32_t enabled,uint32_t source,uint32_t destination,
    uint32_t equation,uint32_t color_mask);

typedef enum {
    BF_UI_CAPTURE_VALID = 0,
    BF_UI_CAPTURE_UNSUPPORTED_BLEND,
    BF_UI_CAPTURE_SOURCE_CHANGED,
    BF_UI_CAPTURE_CLEAR_AFTER_BOUNDARY,
    BF_UI_CAPTURE_UNSUPPORTED_TARGET
} BfUiCaptureStatus;
typedef enum {
    BF_UI_SOURCE_CHANGE_NONE = 0,
    BF_UI_SOURCE_CHANGE_CPU_SYNC,
    BF_UI_SOURCE_CHANGE_TEXTURE_FEEDBACK,
    BF_UI_SOURCE_CHANGE_WORLD_SCOPE
} BfUiSourceChangeReason;
typedef struct {
    uint64_t generation;
    uint32_t color_va,width,height,draws;
    BfUiCaptureStatus status;
    BfUiSourceChangeReason source_change_reason;
} BfUiCompositionInfo;
typedef struct {
    BfUiCompositionInfo info;
    ID3D11Texture2D *texture;
    ID3D11ShaderResourceView *view;
} BfUiCompositionSnapshot;
typedef struct BfUiComposition BfUiComposition;
typedef void (*BfUiCompositionConsumer)(ID3D11DeviceContext *,const BfUiCompositionSnapshot *,void *);
/* Render-thread-only registration and capture API; disabled by default.
 * A consumer or an explicit request enables subsequent complete boundaries.
 * No SDK-ready claim is implied by capture being enabled. */
void bf_ui_composition_consumer(BfUiCompositionConsumer,void *);
void bf_ui_composition_request(int enabled);
int bf_ui_composition_enabled(void);
/* Begin clears only the separate FP16 target. A reused object retains storage
 * when device/dimensions match. Original colour/depth resources are untouched. */
HRESULT bf_ui_composition_begin(BfUiComposition **,ID3D11Device *,ID3D11DeviceContext *,
    uint64_t generation,uint32_t color_va,uint32_t width,uint32_t height);
void bf_ui_composition_destroy(BfUiComposition *);
ID3D11RenderTargetView *bf_ui_composition_target(BfUiComposition *,uint64_t generation);
void bf_ui_composition_invalidate(BfUiComposition *,BfUiCaptureStatus);
void bf_ui_composition_source_changed(BfUiComposition *,BfUiSourceChangeReason);
void bf_ui_composition_draw(BfUiComposition *,uint64_t generation);
/* Executor-thread borrowed view; invalid capture status is preserved so a
 * caller cannot accidentally turn unsupported blending into valid coverage. */
int bf_ui_composition_snapshot(BfUiComposition *,uint64_t generation,BfUiCompositionSnapshot *);
/* Publish after the complete source generation. Borrowed references are valid
 * through the synchronous callback; consumers must retain and fence GPU work.
 * Invalid captures are reported explicitly and must use the original image. */
void bf_ui_composition_publish(BfUiComposition *,ID3D11DeviceContext *,uint64_t generation);
#ifdef __cplusplus
}
#endif
