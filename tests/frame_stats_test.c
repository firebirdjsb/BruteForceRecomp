/* Synthetic CPU frame-statistics regression: no game assets. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nv2a_frame_stats.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Frame stats failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    uint8_t a[60], b[60];
    memset(a, 0, sizeof a);
    for (unsigned i = 0; i < sizeof a; i++)
        a[i] = (uint8_t)(i * 19u + 1u);
    memcpy(b, a, sizeof b);
    const uint64_t h1 = nv2a_frame_hash(a, 20, 0, 0, 3, 3, 4);
    CHECK(h1 != 0);
    b[12] ^= 1;             /* first scanline's row padding */
    CHECK(h1 == nv2a_frame_hash(b, 20, 0, 0, 3, 3, 4));
    b[20 + 3] ^= 1;         /* visible pixel on second row */
    const uint64_t h2 = nv2a_frame_hash(b, 20, 0, 0, 3, 3, 4);
    CHECK(h1 != h2);

    Nv2aFrameStats stats = {0};
    CHECK(nv2a_frame_stats_flip(&stats, 1000, 10, 20, h1, 41) == 1);
    CHECK(nv2a_frame_stats_flip(&stats, 1600, 11, 21, h1, 41) == 0);
    CHECK(nv2a_frame_stats_flip(&stats, 2000, 12, 22, 0, 41) == -1);
    CHECK(nv2a_frame_stats_flip(&stats, 2600, 13, 23, h2, 42) == 1);
    uint32_t flips = 0, distinct = 0, unknown = 0;
    nv2a_frame_stats_take_report(&stats, &flips, &distinct, &unknown);
    CHECK(flips == 4 && distinct == 2 && unknown == 1);
    nv2a_frame_stats_take_report(&stats, &flips, &distinct, &unknown);
    CHECK(flips == 0 && distinct == 0 && unknown == 0);
    nv2a_frame_stats_close(&stats);
    puts("NV2A frame visible-pixel hash and distinct/unknown flip tests passed");
    return 0;
}
