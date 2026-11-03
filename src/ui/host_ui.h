#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float x,y,w,h; } BfUiRect;
typedef struct { uint32_t tag,vtable,flags;BfUiRect rect; } BfUiTutorialNode;
typedef struct { float x,y; int action; uint64_t serial; } BfUiPointer;
/* action: 0=motion, 1=left click, 2=right click. Coordinates are client pixels. */
void bf_ui_mouse(int x,int y,int width,int height,int action);
int bf_ui_key(unsigned key,int repeat);
int bf_ui_menu_key(unsigned key,int repeat);
void bf_ui_focus_lost(void);
int bf_ui_menu_active(void);
int bf_ui_capture(void);
int bf_ui_open(void);
int bf_ui_keyboard_mode(void);
int bf_ui_keyboard_active(void);
void bf_ui_controller_activity(void);
void bf_ui_mouse_activity(void);
void bf_ui_menu_seen(void);
int bf_ui_pointer(BfUiPointer *event);
void bf_ui_pad_press(unsigned control);
int bf_ui_pad(uint16_t *digital,uint8_t analog[8]);
void bf_ui_prompt(BfUiRect rect,const char *key);
/* Called only for an authenticated, active managed guest view. Nodes use
 * dynamically resolved positions; no guest pointer is retained by the UI. */
int bf_ui_tutorial_prepare(uint32_t root_tag,BfUiRect root,const BfUiTutorialNode *nodes,unsigned count);
int bf_ui_tutorial_hide(uint32_t tag,uint32_t vtable);
void bf_ui_tutorial_clear(void);
void bf_ui_paint(uint32_t *pixels,unsigned width,unsigned height);
int bf_ui_antialiasing(void);
int bf_ui_fsr(void);
void bf_ui_fsr_initial(int enabled);
void bf_ui_fsr_supported(int supported);
void bf_ui_settings_initial(int gpu_available);
unsigned bf_ui_scale(void);
int bf_ui_take_size(unsigned *width,unsigned *height);
float bf_ui_audio_gain(void);
float bf_ui_mouse_sensitivity(void);
unsigned bf_ui_interpolation(void);
unsigned bf_ui_texture_scale(void);
int bf_ui_interpolation_allowed(void);
int bf_ui_point(int x,int y,int width,int height,float *gx,float *gy);
int bf_ui_contains(BfUiRect rect,float x,float y);
#ifdef __cplusplus
}
#endif
