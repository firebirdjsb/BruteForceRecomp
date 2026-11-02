#pragma once
#include <d3d11.h>
#ifdef __cplusplus
extern "C" {
#endif
// The bridge owns its D3D12 device, direct queue and completion fence. The
// caller owns the returned D3D11 interfaces and uses them on its GPU thread.
HRESULT bf_scene_d3d12_create(int warp,ID3D11Device **device,ID3D11DeviceContext **context,D3D_FEATURE_LEVEL *level);
HRESULT bf_scene_d3d12_wait(ID3D11DeviceContext *context);
void bf_scene_d3d12_shutdown(void);
#ifdef __cplusplus
}
#endif
