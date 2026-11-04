#pragma once
#include <d3d11.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t frame;
    uint32_t color_va, depth_va, depth_draws;
    uint32_t width, height;
} BfSceneSnapshotInfo;
typedef struct {
    BfSceneSnapshotInfo info;
    ID3D11Texture2D *color;
    ID3D11Texture2D *device_depth; /* R32_FLOAT, actual D3D device [0,1] depth */
    ID3D11ShaderResourceView *color_view, *depth_view;
    /* No motion, jitter or projection-history data is manufactured here.
     * Device depth still requires validation against the game's projection. */
} BfSceneSnapshot;
typedef void (*BfSceneSnapshotConsumer)(const BfSceneSnapshot *, void *);
/* Render-thread-only API. Consumer runs synchronously before HUD commands.
 * Snapshot references remain valid until the next publish or shutdown; use
 * AddRef and copy if retaining data beyond that point. No SDK-ready claim. */
void bf_scene_snapshot_consumer(BfSceneSnapshotConsumer consumer,void *user);
int bf_scene_snapshot_enabled(void);
int bf_scene_snapshot_publish(ID3D11Device *,ID3D11DeviceContext *,
    ID3D11Texture2D *color,ID3D11Texture2D *depth,const BfSceneSnapshotInfo *);
void bf_scene_snapshot_shutdown(void);
/* Independent, bounded file diagnostic. It has no consumer and never replaces
 * the normal export's retained resources or its pre-UI provenance contract. */
typedef enum {
    BF_DEPTH_PROBE_BEFORE_CLEAR,
    BF_DEPTH_PROBE_SHARED_WRITE,
    BF_DEPTH_PROBE_RECREATE
} BfDepthProbeStage;
int bf_depth_probe_enabled(void);
int bf_depth_probe_publish(ID3D11Device *,ID3D11DeviceContext *,
    ID3D11Texture2D *color,ID3D11Texture2D *depth,const BfSceneSnapshotInfo *,BfDepthProbeStage);
void bf_depth_probe_shutdown(void);
#ifdef __cplusplus
}
#endif
