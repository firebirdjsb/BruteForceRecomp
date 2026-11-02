/* Presented-frame statistics; see nv2a_frame_stats.h. */
#include <string.h>

#include "nv2a_frame_stats.h"

#define FRAME_STATS_FLUSH_US 500000u

static uint64_t mix64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}

uint64_t nv2a_frame_hash(const uint8_t *base, uint32_t pitch, uint32_t x,
                         uint32_t y, uint32_t w, uint32_t h, uint32_t bpp)
{
    /* Four independent lanes so the multiplies overlap; rows are hashed as
     * 8-byte words with a byte tail, so the result depends only on the
     * visible bytes and their position. */
    uint64_t lane[4] = {
        0x9E3779B97F4A7C15ull, 0xC2B2AE3D27D4EB4Full,
        0x165667B19E3779F9ull, 0x27D4EB2F165667C5ull,
    };
    const uint64_t k = 0x9FB21C651E98DF25ull;
    uint32_t row, row_bytes = w * bpp;
    uint64_t result;

    for (row = 0; row < h; row++) {
        const uint8_t *p = base + (size_t)(y + row) * pitch + (size_t)x * bpp;
        uint32_t i = 0;
        for (; i + 32 <= row_bytes; i += 32) {
            uint64_t v[4];
            memcpy(v, p + i, sizeof v);
            lane[0] = (lane[0] ^ v[0]) * k;
            lane[1] = (lane[1] ^ v[1]) * k;
            lane[2] = (lane[2] ^ v[2]) * k;
            lane[3] = (lane[3] ^ v[3]) * k;
        }
        for (; i + 8 <= row_bytes; i += 8) {
            uint64_t v;
            memcpy(&v, p + i, sizeof v);
            lane[0] = (lane[0] ^ v) * k;
        }
        for (; i < row_bytes; i++)
            lane[1] = (lane[1] ^ p[i]) * k;
        /* The row boundary, so a picture shifted by a row does not collide. */
        lane[2] = (lane[2] ^ row) * k;
    }
    result = mix64(lane[0] ^ mix64(lane[1] ^ mix64(lane[2] ^ mix64(lane[3]
             ^ ((uint64_t)w << 32 | h)))));
    return result ? result : 1;
}

int nv2a_frame_stats_open(Nv2aFrameStats *s, const char *path)
{
    memset(s, 0, sizeof *s);
    s->file = fopen(path, "w");
    if (!s->file)
        return 0;
    fputs("flip,t_us,draws,program_vertices,hash,distinct,constants\n", s->file);
    fflush(s->file);
    return 1;
}

int nv2a_frame_stats_flip(Nv2aFrameStats *s, uint64_t now_us, uint32_t draws,
                          uint32_t program_vertices, uint64_t hash,
                          uint64_t constants)
{
    int distinct = -1;

    if (hash) {
        distinct = !s->have_last || hash != s->last_hash;
        s->last_hash = hash;
        s->have_last = 1;
    } else {
        s->have_last = 0;
    }
    s->flips++;
    s->report_flips++;
    if (distinct > 0)
        s->report_distinct++;
    else if (distinct < 0)
        s->report_unknown++;
    if (s->file) {
        fprintf(s->file, "%u,%llu,%u,%u,%016llx,%d,%016llx\n", s->flips,
                (unsigned long long)now_us, draws, program_vertices,
                (unsigned long long)hash, distinct, (unsigned long long)constants);
        if (now_us - s->last_flush_us >= FRAME_STATS_FLUSH_US) {
            fflush(s->file);
            s->last_flush_us = now_us;
        }
    }
    return distinct;
}

void nv2a_frame_stats_take_report(Nv2aFrameStats *s, uint32_t *flips,
                                  uint32_t *distinct, uint32_t *unknown)
{
    *flips = s->report_flips;
    *distinct = s->report_distinct;
    *unknown = s->report_unknown;
    s->report_flips = s->report_distinct = s->report_unknown = 0;
    if (s->file)
        fflush(s->file);
}

void nv2a_frame_stats_close(Nv2aFrameStats *s)
{
    if (s->file)
        fclose(s->file);
    s->file = NULL;
}