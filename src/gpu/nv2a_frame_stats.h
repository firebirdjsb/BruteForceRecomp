/*
 * Presented-frame statistics (RECOMP_FRAME_STATS=<file>).
 *
 * The periodic report counts flips, and a flip can repeat the frame before
 * it: a title that misses a frame flips the same buffer again, and a
 * stationary scene can render identical frames. A frame rate measured in
 * flips therefore overstates what the player sees. This records, per flip,
 * a hash of the presented surface's visible pixels, so "distinct frames" is
 * a count of flips whose picture differs from the previous one.
 *
 * Each flip appends one CSV line to the file:
 *
 *   flip,t_us,draws,program_vertices,hash,distinct,constants
 *
 * t_us is host time in microseconds; draws and program_vertices count the
 * work since the previous flip; hash is 0 when no picture was available
 * (a registered back end owns the pixels); distinct is 1 when the hash
 * differs from the previous flip's, or -1 when unknown. constants is a hash
 * of the vertex-program constants uploaded since the previous flip: an
 * unchanged camera and scene upload the same constants, so it is a
 * renderer-independent hint that the world did or did not change, for runs
 * where the pixels are not available. It is a hint, not a picture: texture
 * or state changes alone do not move it. Lines are flushed at least twice a
 * second, because a watchdog exit does not flush stdio.
 *
 * Opt-in: hashing a 640x480 frame costs a fraction of a millisecond.
 */
#ifndef NV2A_FRAME_STATS_H
#define NV2A_FRAME_STATS_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *file;
    uint64_t last_hash;
    int have_last;
    uint32_t flips;
    uint64_t last_flush_us;
    /* Since the last report. */
    uint32_t report_flips, report_distinct, report_unknown;
} Nv2aFrameStats;

/* Hash the visible rectangle of a surface: rows of w pixels of bpp bytes,
 * starting at (x, y), pitch bytes apart. Padding between rows and pixels
 * outside the rectangle do not contribute. Never returns 0. */
uint64_t nv2a_frame_hash(const uint8_t *base, uint32_t pitch, uint32_t x,
                         uint32_t y, uint32_t w, uint32_t h, uint32_t bpp);

/* Open path for writing (truncating) and write the header. 0 on failure. */
int nv2a_frame_stats_open(Nv2aFrameStats *s, const char *path);

/* Record one flip. hash 0 means unknown. Returns 1 if the frame is distinct
 * from the previous one, 0 if it repeats it, -1 if unknown. */
int nv2a_frame_stats_flip(Nv2aFrameStats *s, uint64_t now_us, uint32_t draws,
                          uint32_t program_vertices, uint64_t hash,
                          uint64_t constants);

/* The counts since the previous call, for the periodic report. */
void nv2a_frame_stats_take_report(Nv2aFrameStats *s, uint32_t *flips,
                                  uint32_t *distinct, uint32_t *unknown);

void nv2a_frame_stats_close(Nv2aFrameStats *s);

#endif /* NV2A_FRAME_STATS_H */