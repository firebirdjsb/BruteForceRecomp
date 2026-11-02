#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float x,y,w,h; } BfUiRect;
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
void bf_ui_controller_activity(void);
void bf_ui_menu_seen(void);
int bf_ui_pointer(BfUiPointer *event);
void bf_ui_pad_press(unsigned control);
int bf_ui_pad(uint16_t *digital,uint8_t analog[8]);
void bf_ui_prompt(BfUiRect rect,const char *key);
void bf_ui_paint(uint32_t *pixels,unsigned width,unsigned height);
int bf_ui_fsr(void);
void bf_ui_fsr_initial(int enabled);
void bf_ui_fsr_supported(int supported);
void bf_ui_settings_initial(int gpu_available);
unsigned bf_ui_scale(void);
int bf_ui_take_size(unsigned *width,unsigned *height);
float bf_ui_audio_gain(void);
int bf_ui_point(int x,int y,int width,int height,float *gx,float *gy);
int bf_ui_contains(BfUiRect rect,float x,float y);
#ifdef __cplusplus
}
#endif
