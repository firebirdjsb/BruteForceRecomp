#pragma once
#include <stdint.h>
/* Private native-port NO_OPERATION value, intercepted before original XDK
 * software methods. It is never sent to physical Xbox hardware. */
#define BF_UI_SCENE_BOUNDARY 0xBF414100u
#define BF_SCENE_WIDE_BEGIN 0xBF415701u
#define BF_SCENE_WIDE_END 0xBF415700u
#define BF_UI_ANCHOR_LEFT 0xBF414110u
#define BF_UI_ANCHOR_CENTER 0xBF414111u
#define BF_UI_ANCHOR_RIGHT 0xBF414112u
#ifdef __cplusplus
extern "C" {
#endif
int bf_ui_try_scene_marker(uint32_t device);
/* Exact same validated two-DWORD pushbuffer reservation, restricted to the
 * private scene/UI protocol values above. */
int bf_ui_try_marker(uint32_t device,uint32_t marker);
void bf_scene_aa_set_enabled(int enabled);
int bf_scene_aa_enabled(void);
void bf_scene_aa_cpu(uint32_t va,uint32_t w,uint32_t h,uint32_t pitch,uint32_t bpp,uint32_t frame);
#ifdef __cplusplus
}
#endif
