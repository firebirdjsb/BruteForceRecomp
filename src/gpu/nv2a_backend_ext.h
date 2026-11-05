/*
 * Extended render back-end interface: the full pixel pipeline.
 *
 * Adapted from Blood Wake's bw_backend_ext.h (bloodwake-recomp at
 * e631edddfe56d22344e9f696923aeb6d78a9e7ba, MIT licence).
 *
 * The base interface (nv2a_backend.h) hands a back end texture stage 0 and
 * the diffuse colour, so a hardware back end cannot reproduce the register
 * combiners the CPU rasteriser evaluates (nv2a_combiner.c). For transformed
 * batches -- vertex programs and the fixed-function pipeline -- this hands
 * over everything the CPU path's per-pixel loop reads instead:
 *   - per vertex: position (surface pixels, 1/w), both colours, the fog
 *     factor after the fog unit, and all four texture coordinate sets;
 *   - per batch: all four texture stages, the stage programs, the combiner
 *     registers, the fog colour, the clip-plane mode and the near plane.
 * Screen-space (pre-transformed) batches still use the base draw().
 *
 * Optionally (draw_vsh), vertex-program batches go over untransformed: the
 * program, the constants and the raw vertex streams, so the back end runs
 * the program on the GPU (nv2a_vsh_interp.c semantics). It returns 0 to have
 * the executor transform the batch on the CPU instead.
 *
 * Everything is called on the NV2A thread, one call at a time, like the
 * base interface. Registering an extended back end requires a base back end
 * (nv2a_backend_register) for clears, screen-space draws and flips.
 */
#ifndef NV2A_BACKEND_EXT_H
#define NV2A_BACKEND_EXT_H

#include <stdint.h>
#include "nv2a_backend.h"
#include "nv2a_combiner.h"

typedef struct {
    float x, y, z, rhw;         /* surface pixels, z as produced, 1/w */
    float d0[4], d1[4];         /* diffuse, specular: r, g, b, a in [0,1] */
    float fog;                  /* fog factor (1 = no fog) */
    float pad[3];
    float tc[4][4];             /* texture coordinates, stages 0-3 (s, t, r, q) */
} Nv2aXVertex;                  /* 32 floats */

typedef struct {
    const Nv2aXVertex *vertices;    /* triangle list */
    uint32_t count;
    const Nv2aRenderState *state;
    const Nv2aCombiner *rc;         /* NULL: the title never programmed the
                                       combiners; stage 0 times diffuse */
    Nv2aTexture tex[4];             /* valid only where tex_valid[i] */
    uint8_t tex_valid[4];
    uint8_t tex_normalized[4];      /* coordinates are 0..1 (swizzled, DXT); else texels */
    uint8_t tex_cube[4];
    uint8_t cull;                   /* draw_vsh: cull by state as the CPU path would
                                       (RECOMP_CULL_FACE); CPU-transformed batches
                                       arrive already culled */
    float fogc[4];                  /* fog colour r, g, b (a unused) */
    uint32_t clip_plane_mode;       /* SET_SHADER_CLIP_PLANE_MODE */
    uint32_t alpha_func, alpha_ref; /* NV2A values; also in state */
    float near_z;                   /* the screen-space near plane the CPU path clips at */
    int near_clip;                  /* 0: no near-plane clip (RECOMP_CLIP_NEAR_Z=0) */
} Nv2aXBatch;

/* Vertex programs on the GPU (draw_vsh). Each vertex stream's span over the
 * batch's index range goes over as the title's own bytes; the back end's
 * input layout and shader prologue convert each format exactly as fetch_attr
 * does. Inputs fetch_attr reads differently arrive gathered as floats. */
#define NV2A_XVS_MAX_STREAMS 16
enum {                              /* Nv2aXVsInput.conv >> 4; the low nibble is the size */
    NV2A_XVS_GATHERED = 0,          /* `inputs` (no streams) */
    NV2A_XVS_FLOAT = 1,             /* type 2, 1-4 floats */
    NV2A_XVS_COLOR = 2,             /* type 0, D3DCOLOR: bytes B,G,R,A -> r,g,b,a / 255 */
    NV2A_XVS_UBYTE = 3,             /* type 4, 1-4 bytes / 255 */
    NV2A_XVS_SHORT = 4,             /* type 5, 1-4 shorts as they are */
    NV2A_XVS_CMP = 5,               /* type 6, packed 11:11:10 normal */
    NV2A_XVS_CONST = 6              /* no array: the current value (4 floats, stride 0) */
};
typedef struct {
    const void *data;               /* the span: vertex `first` of the stream's lowest attribute */
    uint32_t bytes;
    uint32_t stride;                /* 0: one value for every vertex */
} Nv2aXVsStream;
typedef struct {
    uint8_t stream;                 /* index into streams */
    uint8_t conv;                   /* NV2A_XVS_* << 4 | components */
    uint16_t offset;                /* the attribute's byte offset in a vertex of its stream */
} Nv2aXVsInput;

typedef struct {
    Nv2aXBatch xb;                  /* everything except vertices and count */
    const uint32_t (*program)[4];   /* from the start slot ... */
    uint32_t ninstr;                /* ... to the FINAL instruction, inclusive */
    const float (*consts)[4];       /* c[0..191] */
    uint32_t consts_gen;            /* bumped by every constant write; equal means unchanged */
    const float *inputs;            /* nverts x ninputs float4s; NULL with streams */
    uint32_t input_mask;            /* bit i: v<i> is read; ascending order */
    uint32_t ninputs, nverts;
    const uint32_t *indices;        /* triangle list into the vertices */
    uint32_t nindices;
    float aa[2];                    /* surface pixels per logical pixel */
    uint32_t fog_enable, fog_mode;
    float fog_param[2];
    uint32_t nstreams;              /* > 0: vertices are the streams' spans */
    Nv2aXVsStream streams[NV2A_XVS_MAX_STREAMS];
    Nv2aXVsInput in[16];
    /* Original NV2A vertex index for each dense/raw-span vertex. Synchronous
     * executor storage; no VB-address/hash or draw-order identity inference. */
    const uint32_t *vertex_ids;
    uint32_t command_va;            /* exact original BEGIN parameter VA */
} Nv2aXVsBatch;

/* What the back end's vertex shader produced for one vertex, in the
 * executor's terms (pos as transform_vertex leaves it), for verification. */
typedef struct {
    float pos[4];
    float d0[4], d1[4];
    float fog, clip[2], pad;        /* fog factor; clip distances (w, near plane) */
    float tc[4][4];
} Nv2aXVsOut;                       /* 32 floats */

typedef struct {
    void (*draw)(const Nv2aSurface *s, const Nv2aXBatch *b);
    /* Optional. Returns 1 if it drew the batch, 0 to have the executor
     * transform it on the CPU and call draw() instead. */
    int (*draw_vsh)(const Nv2aSurface *s, const Nv2aXVsBatch *b);
    /* Optional, for verification: re-run the last draw_vsh batch's vertices
     * through its shader and read the results back (slow). 1 on success. */
    int (*vsh_readback)(Nv2aXVsOut *out, uint32_t nverts);
    /* Optional: 1 if the back end takes the next batch as raw streams. */
    int (*vsh_raw_ok)(void);
    /* Optional: at a flip, before the frame is presented or hashed, copy the
     * colour surface at guest address color_va from the GPU into guest
     * memory. Returns 1 if guest memory now holds the frame. Without it, or
     * when it returns 0, the presenter and RECOMP_FRAME_STATS see whatever
     * guest memory held. */
    int (*readback)(uint32_t color_va);
    /* Optional: visibility tests (D3D's Begin/EndVisibilityTest). The title
     * counts the pixels that pass the depth test between
     * SET_ZPASS_PIXEL_COUNT_ENABLE 1 and 0 and reads the total with
     * GET_REPORT; titles cull geometry by it. zpass(ZPASS_BEGIN/END) brackets
     * counting, zpass(ZPASS_CLEAR) is CLEAR_REPORT_VALUE, and zpass_count()
     * at GET_REPORT returns the pixels counted since the last clear (it may
     * wait for the GPU). Without them a back end's pixels count as none, and
     * a title that culls by visibility draws nothing behind its tests. */
    void (*zpass)(int op);
    uint32_t (*zpass_count)(void);
    /* Optional, asynchronous reports. At GET_REPORT the executor offers the
     * report to zpass_report(report_va, stamp) first; a back end returning 1
     * writes the 16-byte report itself (stamp, count, status 0) once the
     * count is known, as the GPU writes a report after the draws before it.
     * Until then the slot keeps what the title left there, and D3D's
     * GetVisibilityTestResult keeps reporting the test incomplete.
     * poll() runs on every pass of the NV2A thread, also while it is idle,
     * so pending reports complete while the title waits for them. */
    int (*zpass_report)(uint32_t report_va, uint64_t stamp);
    void (*poll)(void);
    uint32_t capabilities;
    void (*before_ui)(const Nv2aSurface *surface);
    /* Optional completed-source hook before framebuffer publication. The
     * CPU executor does not acquire a dependency on a particular renderer. */
    void (*prepare_present)(uint32_t color_va);
} Nv2aBackendExt;

enum { NV2A_BACKEND_CAP_WINDOW_CLIP = 1u };

enum { NV2A_ZPASS_END = 0, NV2A_ZPASS_BEGIN = 1, NV2A_ZPASS_CLEAR = 2 };

/* One vertex-program instruction field, (dword, lowest bit, width), as
 * nv2a_vsh_interp.c reads it. */
static inline uint32_t nv2a_vsh_field(const uint32_t *ins, int dw, int lo, int width)
{
    return (ins[dw] >> lo) & ((1u << width) - 1u);
}

/* The inputs a program reads (bit i for v<i>), counting only the sources
 * each op uses: A for any MAC op, B for mul, mad, dp3/dph/dp4, dst, min, max,
 * slt, sge; C for add, mad and every ILU op. The executor fetches exactly
 * these and the shader reads them in this order. */
static inline uint32_t nv2a_vsh_inputs_used(const uint32_t (*p)[4], uint32_t n)
{
    uint32_t m = 0, i;
    for (i = 0; i < n; i++) {
        const uint32_t *ins = p[i];
        uint32_t mac = nv2a_vsh_field(ins, 1, 21, 4), ilu = nv2a_vsh_field(ins, 1, 25, 3);
        int ua = mac != 0;
        int ub = mac == 2 || (mac >= 4 && mac <= 12);
        int uc = mac == 3 || mac == 4 || ilu != 0;
        if ((ua && nv2a_vsh_field(ins, 2, 26, 2) == 2)
         || (ub && nv2a_vsh_field(ins, 2, 11, 2) == 2)
         || (uc && nv2a_vsh_field(ins, 3, 28, 2) == 2))
            m |= 1u << nv2a_vsh_field(ins, 1, 9, 4);
    }
    return m;
}

/* Register (or, with NULL, remove) the extended entry. */
void nv2a_backend_ext_register(const Nv2aBackendExt *backend);

/* During clear(): the title's clear rectangle (SET_CLEAR_RECT_HORIZONTAL /
 * _VERTICAL) as x0, y0, x1, y1 with x1/y1 exclusive, in the title's logical
 * pixels. Returns 0 if it never sent both. */
int nv2a_backend_clear_rect(uint32_t r[4]);

/* The last value the title wrote to NV097 method `method` (the executor's
 * register mirror). */
uint32_t nv2a_backend_reg(uint32_t method);

#endif /* NV2A_BACKEND_EXT_H */
