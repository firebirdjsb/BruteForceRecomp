#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Window-thread messages are queued; guest state is changed only by its thread. */
int bf_text_active(void);
int bf_text_key(uint32_t key);
int bf_text_character(uint32_t codepoint);
int bf_text_utf8(const char *text);
void bf_text_reset(void);
void bf_text_seen(uint32_t keyboard);
typedef struct BfTextEvent {
    uint32_t codepoint, column, row;
    int click;
} BfTextEvent;
int bf_text_click(uint32_t keyboard, uint32_t column, uint32_t row);
int bf_text_take(uint32_t keyboard, BfTextEvent *event);
#ifdef __cplusplus
}
#endif
