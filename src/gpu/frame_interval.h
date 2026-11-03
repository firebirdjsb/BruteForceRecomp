#pragma once
#include <stdint.h>

/* Host presentation pacing only. The software-method number occupies bits
 * 0..4; the interval occupies payload bits 0..2 (parameter bits 5..7).
 * Preserve the framebuffer, IMMEDIATE flag and every non-swap method. Only
 * an explicit 60-FPS mode may shorten the original TWO interval to ONE.
 * This does not touch guest clocks, physics fields or the 60-Hz vblank. */
static inline uint32_t bf_swap_interval(uint32_t parameter, unsigned frame_limit)
{
    if (frame_limit != 60 || (parameter & 31u) != 1u ||
        ((parameter >> 5) & 7u) != 2u)
        return parameter;
    return (parameter & ~(7u << 5)) | (1u << 5);
}
