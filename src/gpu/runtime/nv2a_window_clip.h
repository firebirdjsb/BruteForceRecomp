#ifndef NV2A_WINDOW_CLIP_H
#define NV2A_WINDOW_CLIP_H

#include <stdint.h>

#define NV2A_WINDOW_CLIP_COUNT 8

typedef struct {
    uint32_t horizontal[NV2A_WINDOW_CLIP_COUNT];
    uint32_t vertical[NV2A_WINDOW_CLIP_COUNT];
    unsigned int axes, exclusive, uniform;
} Nv2aWindowClip;

static inline void nv2a_window_clip_write(Nv2aWindowClip *clip,
                                           int vertical, unsigned int slot,
                                           uint32_t value)
{
    uint32_t *axis = vertical ? clip->vertical : clip->horizontal;
    unsigned int i;

    for (i = slot; i < NV2A_WINDOW_CLIP_COUNT; ++i)
        axis[i] = value;
    clip->axes |= vertical ? 2u : 1u;
    clip->uniform = 1;
    for (i = 1; i < NV2A_WINDOW_CLIP_COUNT; ++i) {
        if (clip->horizontal[i] != clip->horizontal[0] ||
            clip->vertical[i] != clip->vertical[0]) {
            clip->uniform = 0;
            break;
        }
    }
}

static inline int nv2a_window_clip_contains(uint32_t horizontal,
                                             uint32_t vertical,
                                             uint32_t x, uint32_t y)
{
    return x >= (horizontal & 0xFFFu) && x <= ((horizontal >> 16) & 0xFFFu)
        && y >= (vertical & 0xFFFu) && y <= ((vertical >> 16) & 0xFFFu);
}

static inline int nv2a_window_clip_allows(const Nv2aWindowClip *clip,
                                          uint32_t x, uint32_t y,
                                          unsigned int x_shift,
                                          unsigned int y_shift)
{
    unsigned int i, count;
    int contained = 0;

    if (clip->axes != 3)
        return 1;
    x >>= x_shift;
    y >>= y_shift;
    count = clip->uniform ? 1u : NV2A_WINDOW_CLIP_COUNT;
    for (i = 0; i < count; ++i) {
        if (nv2a_window_clip_contains(clip->horizontal[i], clip->vertical[i],
                                      x, y)) {
            contained = 1;
            break;
        }
    }
    return clip->exclusive ? !contained : contained;
}

#endif
