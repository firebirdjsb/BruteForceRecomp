#pragma once
#include <d3d11.h>
#ifdef __cplusplus
extern "C" {
#endif
/* NV2A consumer thread only. Smooth color already drawn, restoring every
 * changed pipeline binding before the original UI draw resumes. */
int bf_scene_antialias(ID3D11Device *,ID3D11DeviceContext *,ID3D11Texture2D *);
#ifdef __cplusplus
}
#endif
