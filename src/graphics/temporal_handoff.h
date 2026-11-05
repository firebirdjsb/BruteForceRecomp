#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Executor/producer thread: mark the exact completed GPU generation before
 * publishing its CPU fallback. Another thread has no such implicit source. */
void bf_temporal_handoff_source(uint64_t sequence,uint32_t color_va);
/* A source rendered with temporal jitter must not fall back to an uncorrected
 * CPU image if reconstruction was rejected. Retain the preceding presentation
 * for this source only; the renderer disables jitter for its native fallback. */
void bf_temporal_handoff_reject(uint64_t sequence,uint32_t color_va);
int bf_temporal_handoff_publishable(uint32_t color_va);
void *bf_temporal_ticket_acquire(uint32_t color_va);
void bf_temporal_ticket_release(void *ticket);
/* Window thread: called under the existing completed-frame SRW lock, while
 * selecting the matching CPU pixels, native aspect and prompt batch. */
void bf_temporal_ticket_select(const void *ticket);
void bf_temporal_ticket_clear(void);
#ifdef __cplusplus
}
#include "game_temporal.h"
namespace bf::graphics {
std::shared_ptr<const GameTemporalFrame> selected_temporal_frame();
}
#endif
