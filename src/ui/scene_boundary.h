#pragma once
#include <stdint.h>
/* Private native-port NO_OPERATION value, intercepted before original XDK
 * software methods. It is never sent to physical Xbox hardware. */
#define BF_UI_SCENE_BOUNDARY 0xBF414100u
#ifdef __cplusplus
extern "C" {
#endif
int bf_ui_try_scene_marker(uint32_t device);
void bf_scene_aa_set_enabled(int enabled);
int bf_scene_aa_enabled(void);
void bf_scene_aa_cpu(uint32_t va,uint32_t w,uint32_t h,uint32_t pitch,uint32_t bpp,uint32_t frame);
#ifdef __cplusplus
}
#endif
