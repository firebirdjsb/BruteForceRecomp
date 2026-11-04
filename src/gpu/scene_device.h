#pragma once
#include <d3d11.h>
#include <d3d12.h>
#ifdef __cplusplus
extern "C" {
#endif
// The bridge owns its D3D12 device, direct queue and completion fence. The
// caller owns the returned D3D11 interfaces and uses them on its GPU thread.
HRESULT bf_scene_d3d12_create(int warp,ID3D11Device **device,ID3D11DeviceContext **context,D3D_FEATURE_LEVEL *level);
HRESULT bf_scene_d3d12_wait(ID3D11DeviceContext *context);
/* Synchronous, render-thread-only D3D11On12 -> native D3D12 handoff.
 * Callback only records commands. Every resource starts and must finish in
 * COMMON. No translation-layer access is allowed until this function returns.
 * Queue fences preserve real ownership; no CPU image copy occurs. A real GPU
 * completion wait currently bounds allocator lifetime (not a performance path).
 * References passed to the callback are borrowed for the duration of the call. */
typedef HRESULT (*BfSceneD3D12Work)(ID3D12Device *,ID3D12CommandQueue *,
    ID3D12GraphicsCommandList *,ID3D12Resource *const *,UINT,void *);
HRESULT bf_scene_d3d12_access(ID3D11DeviceContext *,ID3D11Resource *const *,UINT,
    BfSceneD3D12Work,void *);
void bf_scene_d3d12_shutdown(void);
#ifdef __cplusplus
}
#endif
