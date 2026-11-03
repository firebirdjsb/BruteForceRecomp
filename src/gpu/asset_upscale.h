#pragma once
#include <d3d11.h>
#ifdef __cplusplus
extern "C" {
#endif
// Creates a larger sampled asset and retains every original mip level below
// the reconstructed levels. No render target, depth buffer or guest data edit.
int bf_asset_upscale(ID3D11Device *,ID3D11DeviceContext *,ID3D11Texture2D *,unsigned,
    ID3D11Texture2D **,ID3D11ShaderResourceView **);
#ifdef __cplusplus
}
#endif
