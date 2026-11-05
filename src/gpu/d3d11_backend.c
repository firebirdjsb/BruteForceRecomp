/*
 * Direct3D 11 render back end (RECOMP_GPU=d3d11).
 *
 * Generic parts adapted from Blood Wake's bw_d3d11.c (bloodwake-recomp at
 * e631edddfe56d22344e9f696923aeb6d78a9e7ba, MIT licence; see
 * THIRD_PARTY_NOTICES.md): surfaces, depth buffers, the texture cache, state
 * objects, the vertex ring and the combiner pixel shaders. Its edition,
 * widescreen, visual-effects, texture-restoration and title-specific code is
 * not used.
 *
 * The executor (nv2a_pb_exec.c) decodes the pushbuffer and hands this back
 * end clears, screen-space batches (nv2a_backend.h) and transformed batches
 * with the full pixel-pipeline state (nv2a_backend_ext.h). Rendering is
 * GPU-authoritative:
 *   - one render target per guest colour surface and one D24S8 buffer per
 *     zeta surface; colour and eligible experimental depth buffers are
 *     seeded once from guest memory;
 *   - compatible linear colour aliases sample the render target directly;
 *     feedback uses a separate GPU snapshot and other aliases resolve guest
 *     bytes before the ordinary format-specific texture decoder reads them;
 *   - other textures use exact source/palette snapshots checked at every
 *     use, including repeated writes within one frame;
 *   - blend, depth-stencil, rasteriser and sampler states are cached;
 *   - vertices go through one dynamic ring buffer mapped NO_OVERWRITE.
 *
 * Correctness reference: the CPU rasteriser. The defaults reproduce its
 * rules where they are a choice: texture level 0 only, colours and fog
 * interpolated affinely, the surface clip as the only scissor, depth as the
 * title's screen z over the zeta format's range (SET_CLIP_MIN/MAX are not
 * applied). Known differences: depth is D24 rather than the CPU path's float
 * buffer, and stencil state is honoured here while the CPU path ignores it.
 *
 * Vertex programs run on the GPU (draw_vsh): each program becomes a vertex
 * shader (vsh_hlsl.c) compiled on a worker thread; until it is ready the
 * executor transforms the batch on the CPU. Vertex streams arrive as the
 * title's bytes and are converted by the input layout and the shader.
 *
 * At each flip the presented surface is read
 * back into guest memory (the executor's readback hook), so the framework's
 * presenter, frame dumps and frame statistics keep working unchanged.
 *
 * RECOMP_D3D11_WARP=1 uses the WARP software device (tests, no GPU needed).
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d/d3d8_swizzle.h"
#include "kernel.h"
#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"
#include "d3d11_backend.h"
#include "rc_hlsl.h"
#include "texture_decode.h"
#include "asset_upscale.h"
#include "scene_antialias.h"
#include "ui/scene_boundary.h"
extern unsigned bf_ui_texture_scale(void);
#include "vsh_hlsl.h"
#include "shader_cache.h"
#include "xbox_memory_layout.h"
#include "scene_device.h"
#include "scene_snapshot.h"
#include "graphics/temporal_handoff.h"
#include "graphics/native_aspect_state.h"
#include "temporal_trace_backend.h"
#include "graphics/ui_composition.h"
#include "graphics/temporal_control.h"
#include "motion_renderer.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define GUEST(va) ((uint8_t *)((uintptr_t)(va) + xbox_GetMemoryOffset()))

static ID3D11Device *s_dev;
static ID3D11DeviceContext *s_ctx;
static int s_failed;
static int s_warp;
static int s_d3d12;
static int s_depth_texture;
static volatile LONG s_device_thread;
static BfMotionRenderer *s_motion_renderer;
static ID3D11Buffer *s_motion_zero_cb;
static BfMotionDrawBinding s_motion_binding;
static uint32_t s_motion_draw_color,s_motion_jitter_frame;
static int s_motion_next_jitter,s_motion_latched_jitter;

/* ---- statistics ---------------------------------------------------------- */

static struct {
    unsigned draws, xdraws, tris, uploads, rtt, clears, readbacks, ps_compiles, flips;
    unsigned overlap_rtt;       /* a texture inside a surface but not at its start */
    unsigned vdraws, vraw, vfall, kup, kskip;  /* draw_vsh: drawn, raw streams, CPU, constants */
    unsigned tex_gpu, tex_cpu, tex_bc, tex_direct, tex_pitch;
    unsigned depth_seeds, depth_samples, depth_snapshots, depth_rejected;
    uint64_t tex_compared_bytes;
    unsigned tex_hint_hits, tex_searches;
    double readback_ms;
} s_st;

static unsigned s_q_reads;
static double s_q_wait_ms;
static unsigned s_rep_written, s_rep_flushes;
static struct { double ms; uint64_t calls, ticks; } s_texture_profile;
static struct { double ms, max_ms; uint64_t calls; } s_completion_profile;
enum { TEX_PROFILE_ALIAS, TEX_PROFILE_DESCRIPTOR, TEX_PROFILE_VALIDATE,
       TEX_PROFILE_PREPARE, TEX_PROFILE_PHASES };
static struct { uint64_t ticks, calls; } s_texture_phases[TEX_PROFILE_PHASES];
typedef struct { unsigned phase; uint64_t started, last; } TextureProfileSample;

static uint64_t texture_profile_ticks(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)counter.QuadPart;
}

/* Nanoseconds express QPC elapsed time, not nanosecond clock precision.
 * Convert bounded interval totals in floating point to avoid ticks*1e9
 * overflowing an integer on a high-frequency performance counter. */
static double texture_profile_ns(uint64_t ticks)
{
    static LARGE_INTEGER frequency;
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    return (double)ticks*1000000000.0/(double)frequency.QuadPart;
}

static void texture_profile_next(TextureProfileSample *sample,unsigned phase)
{
    uint64_t now;
    if(!sample)return; /* Profile-off lookups never read the clock here. */
    now=texture_profile_ticks();
    s_texture_phases[sample->phase].ticks+=now-sample->last;
    sample->phase=phase;sample->last=now;
    ++s_texture_phases[phase].calls;
}

static int profile_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("BF_PROFILE_RENDER") != NULL;
    return enabled;
}

static double qpc_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart)
        QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static void stats_flip(void)
{
    static double last;
    double now = qpc_ms();
    s_st.flips++;
    if (!last)
        last = now;
    if (now - last >= 10000.0) {
        fprintf(stderr, "[GPU-D3D11] %u flips: %u draws (%u extended, %u vertex programs"
                        " [%u raw], %u back to the CPU), %u triangles, %u uploads,"
                        " %u render-to-texture, %u overlapping, %u clears, %u pixel shaders"
                        " compiled, constants %u uploaded %u skipped, readback %.2f ms/flip,"
                        " %u visibility reads waiting %.1f ms, %u reports written"
                        " asynchronously (%u polls found one pending),"
                        " textures GPU %u CPU %u BC %u direct %u, %u stale pitches,"
                        " depth seeds %u samples %u snapshots %u rejected %u,"
                        " %llu source bytes compared\n",
                s_st.flips, s_st.draws, s_st.xdraws, s_st.vdraws, s_st.vraw, s_st.vfall,
                s_st.tris, s_st.uploads, s_st.rtt, s_st.overlap_rtt, s_st.clears,
                s_st.ps_compiles, s_st.kup, s_st.kskip,
                s_st.readbacks ? s_st.readback_ms / s_st.readbacks : 0.0,
                s_q_reads, s_q_wait_ms, s_rep_written, s_rep_flushes,
                s_st.tex_gpu, s_st.tex_cpu, s_st.tex_bc, s_st.tex_direct, s_st.tex_pitch,
                s_st.depth_seeds, s_st.depth_samples, s_st.depth_snapshots, s_st.depth_rejected,
                (unsigned long long)s_st.tex_compared_bytes);
        if (profile_enabled()) {
            static const char *names[TEX_PROFILE_PHASES]={"alias_sync","descriptor_search","exact_validation","decode_upload_prepare"};
            unsigned phase;
            fprintf(stderr, "[GPU-PERF] texture lookup/validation/upload %.2f ms/s across %llu calls; interval %.3f s; lookup hints %u, searches %u; total %.0f ns; requested source spans %llu bytes\n",
                s_texture_profile.ms * 1000.0 / (now - last),
                (unsigned long long)s_texture_profile.calls, (now - last) / 1000.0,
                s_st.tex_hint_hits, s_st.tex_searches,texture_profile_ns(s_texture_profile.ticks),
                (unsigned long long)s_st.tex_compared_bytes);
            for(phase=0;phase<TEX_PROFILE_PHASES;phase++) {
                const double ns=texture_profile_ns(s_texture_phases[phase].ticks);
                fprintf(stderr,"[GPU-PERF] texture phase %s: %.0f ns across %llu calls; %.2f ns/call; %.2f ms/s\n",
                    names[phase],ns,(unsigned long long)s_texture_phases[phase].calls,
                    s_texture_phases[phase].calls?ns/(double)s_texture_phases[phase].calls:0.0,
                    ns/1000.0/(now-last));
            }
        }
        if (profile_enabled())
            fprintf(stderr, "[GPU-PERF] ordered completion waits %.2f ms/s across %llu calls; max %.3f ms\n",
                s_completion_profile.ms * 1000.0 / (now - last),
                (unsigned long long)s_completion_profile.calls, s_completion_profile.max_ms);
        memset(&s_texture_profile, 0, sizeof s_texture_profile);
        memset(s_texture_phases,0,sizeof s_texture_phases);
        memset(&s_completion_profile, 0, sizeof s_completion_profile);
        s_q_reads = 0;
        s_q_wait_ms = 0.0;
        s_rep_written = s_rep_flushes = 0;
        memset(&s_st, 0, sizeof s_st);
        last = now;
    }
}

/* ---- shaders ------------------------------------------------------------- */

static int compile(const char *src, size_t len, const char *name, const char *entry,
                   const char *target, ID3DBlob **out)
{
    ID3DBlob *err = NULL;
    void *cached = NULL; size_t cached_size = 0;
    if (gpu_pscache_load(src,len,name,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,&cached,&cached_size)) {
        HRESULT loaded = D3DCreateBlob(cached_size,out);
        if (SUCCEEDED(loaded)) memcpy(ID3D10Blob_GetBufferPointer(*out),cached,cached_size);
        free(cached);
        if (SUCCEEDED(loaded)) return 1;
    }
    HRESULT hr = D3DCompile(src, len, name, NULL, NULL, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] shader %s/%s failed: %s\n", name, entry,
                err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err)
            ID3D10Blob_Release(err);
        return 0;
    }
    if (err)
        ID3D10Blob_Release(err);
    gpu_pscache_store(src,len,name,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,
        ID3D10Blob_GetBufferPointer(*out),ID3D10Blob_GetBufferSize(*out));
    return 1;
}

/* Screen-space batches (base interface): stage 0 times the diffuse colour,
 * as the CPU path's raster_indexed. */
static const char s_base_hlsl[] =
GPU_WINDOW_CLIP_HLSL
GPU_SHADOW_HLSL
GPU_UI_ANCHOR_HLSL
GPU_UI_CAPTURE_HLSL
GPU_MOTION_VERTEX_BODY
"cbuffer B : register(b0) { float4 surf; float4 texinfo; float4 alpha; float4 depth; };\n"
"struct VSIn { float4 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD;uint vid:SV_VertexID; };\n"
"struct PSIn { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD;\n" GPU_MOTION_VARYINGS "};\n"
"PSIn vs_main(VSIn i) {\n"
"  PSIn o;\n"
"  float w = (i.p.w > 0.0) ? 1.0 / i.p.w : 1.0;\n"
"  float x = i.p.x * surf.z * 2.0 - 1.0;\n"
"  x = ui_x(x,alpha.w);\n"
"  float y = 1.0 - i.p.y * surf.w * 2.0;\n"
"  float z = (i.p.z - depth.x) * depth.y;\n"
"  o.p = float4(x * w, y * w, z * w, w);\n"
"  o.bf_capture_clip=o.p;o.p=bf_motion_vertex(o.p,i.vid,o.bf_previous_clip,o.bf_history_valid,o.bf_original_id);\n"
"  o.c = i.c;\n"
"  o.uv = i.uv * texinfo.xy;\n"
"  return o;\n"
"}\n"
"Texture2D t0 : register(t0);\n"
"SamplerState s0 : register(s0);\n"
"UiPixelOutput ps_main(PSIn i) {\n"
"  if (!window_allows(i.p.xy)) discard;\n"
"  float4 c = i.c;\n"
"  if (texinfo.z > 0.5) {\n"
"    float4 t = t0.Sample(s0, i.uv);\n"
"    if (texinfo.w > 0.5) t = shadow_compare(t.r * 16777215.0, 0.0, (uint)texinfo.w - 1).xxxx;\n"
"    c *= t;\n"
"  }\n"
"  if (alpha.z > 0.5) { int v = (int)(c.a * 255.0 + 0.5), r = (int)alpha.y; int f = (int)alpha.x;\n"
"    bool ok = (f == 7) || (f == 1 && v < r) || (f == 2 && v == r) || (f == 3 && v <= r)\n"
"      || (f == 4 && v > r) || (f == 5 && v != r) || (f == 6 && v >= r);\n"
"    if (!ok) discard; }\n"
"  return ui_pixel_output(c,i.p,i.bf_previous_clip,i.bf_history_valid);\n"
"}\n";

typedef struct {
    float surf[4], tex[4], alpha[4], depth[4];
} BaseConsts;

/* Transformed batches (extended interface): the executor's vertices, in
 * surface pixels with 1/w, mapped to an affine image of clip space so the
 * texture coordinates interpolate perspective-correctly. */
static const char s_xvs_hlsl[] =
GPU_UI_ANCHOR_HLSL
GPU_RC_CBUFFER
GPU_MOTION_VERTEX_HLSL
"struct VSIn { float4 p : POSITION; float4 d0 : COLOR0; float4 d1 : COLOR1;\n"
"  float4 fog : TEXCOORD4; float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1;\n"
"  float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3;uint vid:SV_VertexID; };\n"
"struct VSOut { float4 p : SV_Position; noperspective float4 d0 : COLOR0;\n"
"  noperspective float4 d1 : COLOR1; noperspective float fog : FOG;\n"
"  float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1;\n"
"  float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3; float fogp : FOG1;\n" GPU_MOTION_VARYINGS "};\n"
"VSOut vs_main(VSIn i) {\n"
"  VSOut o;\n"
"  float w = (i.p.w > 0.0) ? 1.0 / i.p.w : 1.0;\n"
"  float x = i.p.x * surf.z * 2.0 - 1.0;\n"
"  x = ui_x(x,alpha.w);\n"
"  float y = 1.0 - i.p.y * surf.w * 2.0;\n"
"  float zr = (i.p.z - depth.x) * depth.y;\n"
"  float z = depth.z > 0.5 ? saturate(zr) : zr;\n"
"  o.p = float4(x * w, y * w, z * w, w);\n"
"  o.bf_capture_clip=o.p;o.p=bf_motion_vertex(o.p,i.vid,o.bf_previous_clip,o.bf_history_valid,o.bf_original_id);\n"
"  o.d0 = i.d0; o.d1 = i.d1; o.fog = i.fog.x; o.fogp = i.fog.x;\n"
"  o.tc0 = i.tc0; o.tc1 = i.tc1; o.tc2 = i.tc2; o.tc3 = i.tc3;\n"
"  return o;\n"
"}\n";

static ID3D11VertexShader *s_base_vs, *s_xvs;
static ID3D11PixelShader *s_base_ps;
static ID3D11InputLayout *s_base_layout, *s_xlayout;
static ID3D11Buffer *s_base_cb, *s_xcb, *s_window_cb;
static ID3D11RasterizerState *s_rs;
static ID3D11RasterizerState *s_rs_cull[2][2];  /* [cull back][front counter-clockwise] */

#define MAX_PS 1024
typedef struct { GpuRcKey key; ID3D11PixelShader *ps; } PsEntry;
static PsEntry *s_ps;
static int s_nps;

static ID3D11PixelShader *get_rc_shader(const GpuRcKey *k)
{
    static char src[64 * 1024];
    size_t len;
    ID3DBlob *blob = NULL;
    ID3D11PixelShader *ps = NULL;
    int i;

    for (i = 0; i < s_nps; i++)
        if (!memcmp(&s_ps[i].key, k, sizeof *k))
            return s_ps[i].ps;
    if (s_nps == MAX_PS)
        return NULL;
    len = gpu_rc_hlsl(k, src, sizeof src);
    if (!len || !compile(src, len, "rc", "ps_main", "ps_5_0", &blob))
        return NULL;
    ID3D11Device_CreatePixelShader(s_dev, ID3D10Blob_GetBufferPointer(blob),
                                   ID3D10Blob_GetBufferSize(blob), NULL, &ps);
    ID3D10Blob_Release(blob);
    if (!ps)
        return NULL;
    s_ps[s_nps].key = *k;
    s_ps[s_nps].ps = ps;
    s_nps++;
    s_st.ps_compiles++;
    return ps;
}

/* ---- device -------------------------------------------------------------- */

static int ensure_device(void)
{
    static const D3D11_INPUT_ELEMENT_DESC base_il[3] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_B8G8R8A8_UNORM,     0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    /* Nv2aXVertex: x y z rhw | d0 | d1 | fog pad[3] | tc[4]. */
    static const D3D11_INPUT_ELEMENT_DESC x_il[8] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    ID3DBlob *vsb = NULL, *psb = NULL, *xvsb = NULL;
    D3D11_BUFFER_DESC bd;
    D3D11_RASTERIZER_DESC rd;
    HRESULT hr;

    if (s_failed)
        return 0;
    if (s_dev)
        return 1;
    {
        const char *capture=getenv("BF_TEMPORAL_UI_CAPTURE");
        if(capture&&!strcmp(capture,"1"))bf_ui_composition_request(1);
    }
    s_failed = 1;               /* until everything below has worked */
    hr = s_d3d12 ? bf_scene_d3d12_create(s_warp,&s_dev,&s_ctx,&got) : D3D11CreateDevice(NULL, s_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                           NULL, D3D11_CREATE_DEVICE_SINGLETHREADED, &fl, 1,
                           D3D11_SDK_VERSION, &s_dev, &got, &s_ctx);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU] %s scene device creation failed 0x%08lX; select another scene renderer in the launcher\n",s_d3d12?"D3D12/11On12":"D3D11", (unsigned long)hr);
        abort();
    }
    InterlockedExchange(&s_device_thread, (LONG)GetCurrentThreadId());
    if (!compile(s_base_hlsl, sizeof s_base_hlsl - 1, "base", "vs_main", "vs_5_0", &vsb)
        || !compile(s_base_hlsl, sizeof s_base_hlsl - 1, "base", "ps_main", "ps_5_0", &psb)
        || !compile(s_xvs_hlsl, sizeof s_xvs_hlsl - 1, "xvs", "vs_main", "vs_5_0", &xvsb)) {
        if (vsb) ID3D10Blob_Release(vsb);
        if (psb) ID3D10Blob_Release(psb);
        return 0;
    }
    ID3D11Device_CreateVertexShader(s_dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &s_base_vs);
    ID3D11Device_CreatePixelShader(s_dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &s_base_ps);
    ID3D11Device_CreateInputLayout(s_dev, base_il, 3, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &s_base_layout);
    ID3D11Device_CreateVertexShader(s_dev, ID3D10Blob_GetBufferPointer(xvsb),
                                    ID3D10Blob_GetBufferSize(xvsb), NULL, &s_xvs);
    ID3D11Device_CreateInputLayout(s_dev, x_il, 8, ID3D10Blob_GetBufferPointer(xvsb),
                                   ID3D10Blob_GetBufferSize(xvsb), &s_xlayout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);
    ID3D10Blob_Release(xvsb);

    memset(&bd, 0, sizeof bd);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = sizeof(BaseConsts);
    ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_base_cb);
    bd.ByteWidth = (sizeof(GpuRcConsts) + 15) & ~15u;
    ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_xcb);
    bd.ByteWidth = 9 * 16;
    ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_window_cb);
    {
        const BfMotionDrawConstants zero={0};D3D11_SUBRESOURCE_DATA data={0};
        bd.ByteWidth=sizeof zero;bd.Usage=D3D11_USAGE_IMMUTABLE;bd.CPUAccessFlags=0;data.pSysMem=&zero;
        if(FAILED(ID3D11Device_CreateBuffer(s_dev,&bd,&data,&s_motion_zero_cb)))return 0;
    }

    memset(&rd, 0, sizeof rd);
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;      /* the executor culls, as the CPU path */
    rd.DepthClipEnable = FALSE;         /* the executor clips at the near plane */
    rd.ScissorEnable = TRUE;
    ID3D11Device_CreateRasterizerState(s_dev, &rd, &s_rs);
    {
        int back, ccw;
        for (back = 0; back < 2; back++)
            for (ccw = 0; ccw < 2; ccw++) {
                rd.CullMode = back ? D3D11_CULL_BACK : D3D11_CULL_FRONT;
                rd.FrontCounterClockwise = ccw ? TRUE : FALSE;
                ID3D11Device_CreateRasterizerState(s_dev, &rd, &s_rs_cull[back][ccw]);
            }
        rd.CullMode = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = FALSE;
    }

    s_ps = (PsEntry *)calloc(MAX_PS, sizeof *s_ps);
    if (!s_base_vs || !s_base_ps || !s_base_layout || !s_xvs || !s_xlayout || !s_base_cb
        || !s_xcb || !s_window_cb || !s_rs || !s_ps)
        return 0;
    s_failed = 0;
    bf_aspect_scene_supported(1);
    fprintf(stderr, "[GPU-D3D11] %s device ready (feature level 0x%x)\n",
            s_warp ? "WARP" : "hardware", (unsigned)got);
    return 1;
}

/* ---- surfaces ------------------------------------------------------------ */

#define MAX_SURF 32
typedef struct {
    uint32_t va, pitch, bpp, w, h;
    uint32_t aa_generation;
    uint32_t scene_depth_va, scene_generation, scene_depth_draws, scene_tainted;
    uint32_t wide_generation, ui_generation,ui_published_generation;
    BfUiComposition *ui_overlay;
    BfSceneDepthHold *temporal_depth;
    BfSceneDepthHoldInfo temporal_identity;
    uint64_t temporal_geometry_serial;
    uint32_t temporal_frozen;
    ID3D11Texture2D *tex, *snapshot;
    ID3D11RenderTargetView *rtv;
    ID3D11ShaderResourceView *srv, *snapshot_srv;
    uint8_t *guest_shadow;
    size_t guest_shadow_bytes;
    unsigned guest_check_gen;
    int gpu_dirty;
} Surf;

typedef struct {
    uint32_t va, w, h, pitch, format, surface_type, initialized, recreated;
    uint32_t stencil_initialized, gpu_dirty;
    ID3D11Texture2D *tex, *snapshot;
    ID3D11DepthStencilView *dsv;
    ID3D11ShaderResourceView *srv, *snapshot_srv;
} Depth;

static Surf s_surf[MAX_SURF];
static int s_nsurf;
static Depth s_depth[MAX_SURF];
static int s_ndepth;
static Surf *s_cur;
static Depth *s_cur_depth;
static uint32_t s_gen = 1;          /* completed-flip generation */
static unsigned s_wide_scope;
static unsigned s_ui_anchor=1;
static BfUiBlendMode s_ui_capture_mode;
static ID3D11Buffer *s_ui_capture_cb;

static int ui_capture_trace_diagnostics(void)
{
    const char *enabled=getenv("BF_TEMPORAL_UI_TRACE");
    return enabled&&!strcmp(enabled,"1")&&bf_motion_trace_configured();
}

static const char *ui_source_change_name(BfUiSourceChangeReason reason)
{
    switch(reason) {
    case BF_UI_SOURCE_CHANGE_CPU_SYNC:return "CPU_SYNC";
    case BF_UI_SOURCE_CHANGE_TEXTURE_FEEDBACK:return "TEXTURE_FEEDBACK";
    case BF_UI_SOURCE_CHANGE_WORLD_SCOPE:return "WORLD_SCOPE";
    default:return "NONE";
    }
}
static void bind_output_views(Surf *,Depth *);
static ID3D11RenderTargetView *ui_capture_target(Surf *s)
{
    return s&&s->ui_generation==s_gen?bf_ui_composition_target(s->ui_overlay,s_gen):NULL;
}
static int trace_aspect_queue(void)
{
    static int enabled=-1;
    if(enabled<0){const char *value=getenv("BF_TRACE_ASPECT_QUEUE");enabled=value&&strcmp(value,"1")==0;}
    return enabled;
}
static int readable_guest_range(uint32_t va, uint64_t bytes);
static int d3d_readback(uint32_t color_va);
static int resolve_depth_color_alias(const Nv2aTexture *t,int cube);
static ID3D11Texture2D *s_depth_color_staging;
static uint32_t s_depth_color_w,s_depth_color_h;
static DXGI_FORMAT s_depth_color_format;

static DXGI_FORMAT surf_format(uint32_t bpp)
{
    return bpp == 2 ? DXGI_FORMAT_B5G6R5_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
}

static void release_surf(Surf *s)
{
    bf_motion_renderer_surface_reset(s_motion_renderer,s->va);
    bf_scene_depth_hold_destroy(s->temporal_depth);s->temporal_depth=NULL;
    memset(&s->temporal_identity,0,sizeof s->temporal_identity);
    s->temporal_geometry_serial=0;s->temporal_frozen=0;
    bf_ui_composition_destroy(s->ui_overlay);s->ui_overlay=NULL;
    if (s->snapshot_srv) ID3D11ShaderResourceView_Release(s->snapshot_srv);
    if (s->snapshot) ID3D11Texture2D_Release(s->snapshot);
    s->snapshot_srv = NULL; s->snapshot = NULL;
    if (s->srv) ID3D11ShaderResourceView_Release(s->srv);
    if (s->rtv) ID3D11RenderTargetView_Release(s->rtv);
    if (s->tex) ID3D11Texture2D_Release(s->tex);
    s->srv = NULL; s->rtv = NULL; s->tex = NULL;
    free(s->guest_shadow);s->guest_shadow=NULL;s->guest_shadow_bytes=0;
    s->guest_check_gen=0;s->gpu_dirty=0;
    s->scene_depth_va=0;s->scene_generation=0;s->scene_depth_draws=0;
    s->scene_tainted=1;
    s->wide_generation=s->ui_generation=s->ui_published_generation=0;
}

/* Executor-thread-only, after preceding queued geometry has been flushed. */
void bruteforce_d3d11_wide_scope(int begin_scope)
{
    int i;
    if(begin_scope) {
        ++s_wide_scope;
        s_ui_anchor=1;
        for(i=0;i<s_nsurf;i++) {
            if(s_surf[i].ui_generation==s_gen)
                bf_ui_composition_source_changed(s_surf[i].ui_overlay,BF_UI_SOURCE_CHANGE_WORLD_SCOPE);
            s_surf[i].ui_generation=0;
        }
    } else if(s_wide_scope)--s_wide_scope;
    if(trace_aspect_queue()) {
        static unsigned lines;
        if(lines++<16)fprintf(stderr,"[ASPECT-QUEUE] frame=%u %s scope=%u\n",s_gen,begin_scope?"begin":"end",s_wide_scope);
    }
}
void bruteforce_d3d11_ui_anchor(unsigned anchor) {if(anchor<=2)s_ui_anchor=anchor;}

static int completed_surface_wide(uint32_t va)
{
    int i;
    for(i=0;i<s_nsurf;i++) {
        const Surf *s=&s_surf[i];
        if(s->va==va&&s->tex&&s->w==640&&s->h==480&&s->bpp==4) {
            const int wide=s->wide_generation==s_gen;
            if(wide&&trace_aspect_queue()) {
                static unsigned lines;
                if(lines++<8)fprintf(stderr,"[ASPECT-QUEUE] frame=%u publish=%08X wide=1 ui=%d\n",s_gen,va,s->ui_generation==s_gen);
            }
            return wide;
        }
    }
    return 0;
}

/* Call only while scene export is enabled. A shared/replaced depth attachment
 * invalidates every dependent color, including dependencies from older flips.
 * One later draw cannot prove that the retained color was fully rebuilt. */
static void invalidate_scene_depth(uint32_t va,const Surf *owner)
{
    int i;
    for(i=0;i<s_nsurf;i++)if(&s_surf[i]!=owner&&s_surf[i].scene_depth_va&&
        (s_surf[i].scene_depth_va&0x0FFFFFFFu)==(va&0x0FFFFFFFu)) {
        s_surf[i].scene_tainted=1;s_surf[i].scene_depth_draws=0;
    }
}

static int temporal_depth_enabled(void)
{
    static int enabled=-1;
    if(enabled<0) {const char *p=getenv("BF_TEMPORAL_DEPTH_HOLD");enabled=p&&!strcmp(p,"1");}
    return enabled;
}

static int scene_tracking_enabled(void)
{
    return bf_scene_snapshot_enabled()||bf_depth_probe_enabled()||temporal_depth_enabled();
}

/* Diagnostic copies precede destructive depth commands. They use a separate
 * bounded publisher and never satisfy the validated pre-UI export contract. */
static void probe_scene_depth(Depth *depth,const Surf *owner,BfDepthProbeStage stage)
{
    int i;
    if((!bf_depth_probe_enabled()&&!temporal_depth_enabled())||!depth||!depth->tex||!depth->initialized||
       depth->format!=2||depth->surface_type!=1)return;
    for(i=0;i<s_nsurf;i++) {
        Surf *sf=&s_surf[i];
        if(sf!=owner&&sf->tex&&sf->bpp==4&&!sf->scene_tainted&&sf->scene_generation==s_gen&&
           sf->scene_depth_draws&&sf->scene_depth_va==depth->va&&sf->w==depth->w&&sf->h==depth->h) {
            BfSceneSnapshotInfo info={(uint64_t)s_gen,sf->va,depth->va,sf->scene_depth_draws,sf->w,sf->h};
            if(bf_depth_probe_enabled())bf_depth_probe_publish(s_dev,s_ctx,sf->tex,depth->tex,&info,stage);
            if(temporal_depth_enabled()&&sf->temporal_identity.view&&sf->temporal_identity.context) {
                sf->temporal_identity.source=info;
                sf->temporal_identity.geometry_serial=sf->temporal_geometry_serial;
                sf->temporal_frozen=bf_scene_depth_hold_freeze(&sf->temporal_depth,s_dev,s_ctx,
                    sf->tex,depth->tex,&sf->temporal_identity);
            }
        }
    }
}

static void probe_depth_write(const Nv2aRenderState *rs)
{
    if(rs&&rs->depth_write)probe_scene_depth(s_cur_depth,rs->depth_test_enable?s_cur:NULL,BF_DEPTH_PROBE_SHARED_WRITE);
}

/* The backend's device/cache lifetime is process-wide. Release the snapshot
 * references we own at normal process shutdown as well as on surface reuse.
 * Do not touch the single-threaded immediate context from an exit thread. */
static void release_asset_cache(void);
static void release_color_snapshots(void)
{
    int i;
    bf_motion_renderer_destroy(s_motion_renderer);s_motion_renderer=NULL;
    if(s_motion_zero_cb)ID3D11Buffer_Release(s_motion_zero_cb);s_motion_zero_cb=NULL;
    bf_scene_snapshot_shutdown();
    bf_depth_probe_shutdown();
    release_asset_cache();
    for(i=0;i<s_nsurf;i++) {
        bf_ui_composition_destroy(s_surf[i].ui_overlay);s_surf[i].ui_overlay=NULL;
        if(s_surf[i].snapshot_srv)ID3D11ShaderResourceView_Release(s_surf[i].snapshot_srv);
        if(s_surf[i].snapshot)ID3D11Texture2D_Release(s_surf[i].snapshot);
        s_surf[i].snapshot_srv=NULL;s_surf[i].snapshot=NULL;
    }
    if(s_depth_color_staging)ID3D11Texture2D_Release(s_depth_color_staging);
    s_depth_color_staging=NULL;
    if(s_ui_capture_cb)ID3D11Buffer_Release(s_ui_capture_cb);s_ui_capture_cb=NULL;
}

/* CPU movie decoding writes color surfaces directly. Compare those bytes
 * with the last shared snapshot before uploading or publishing GPU pixels.
 * Generic ownership protocol adapted from upstream 24d2e58 (MIT). */
static int snapshot_guest_surface(Surf *s)
{
    size_t row=(size_t)s->w*s->bpp,bytes;
    uint32_t y;
    uint8_t *shadow;
    if(!row||!s->h||row>s->pitch||s->h>SIZE_MAX/row||
       !readable_guest_range(s->va,(uint64_t)(s->h-1)*s->pitch+row))return 0;
    bytes=row*s->h;
    if(s->guest_shadow_bytes!=bytes) {
        shadow=(uint8_t *)realloc(s->guest_shadow,bytes);
        if(!shadow)return 0;
        s->guest_shadow=shadow;s->guest_shadow_bytes=bytes;
    }
    for(y=0;y<s->h;++y)memcpy(s->guest_shadow+y*row,GUEST(s->va)+(size_t)y*s->pitch,row);
    return 1;
}

static int sync_guest_surface(Surf *s,int force)
{
    size_t row=(size_t)s->w*s->bpp;
    uint32_t y;
    if(!force&&s->guest_check_gen==s_gen)return 1;
    s->guest_check_gen=s_gen;
    if(!s->guest_shadow)return 0;
    for(y=0;y<s->h;++y)
        if(memcmp(s->guest_shadow+y*row,GUEST(s->va)+(size_t)y*s->pitch,row))break;
    if(y==s->h)return 1;
    if(s->gpu_dirty) {
        static int warned;
        if(!warned++)fprintf(stderr,"[GPU-D3D11] CPU writes overlap pending GPU color work at %08X; preserving GPU work\n",s->va);
        return 1;
    }
    if(s->ui_generation==s_gen)bf_ui_composition_source_changed(s->ui_overlay,BF_UI_SOURCE_CHANGE_CPU_SYNC);
    ID3D11DeviceContext_UpdateSubresource(s_ctx,(ID3D11Resource *)s->tex,0,NULL,GUEST(s->va),s->pitch,0);
    return snapshot_guest_surface(s);
}

static int mirror_full_color_clear(Surf *s,uint32_t argb)
{
    uint32_t x,y;
    for(y=0;y<s->h;++y) {
        if(s->bpp==4) {
            uint32_t *row=(uint32_t *)(GUEST(s->va)+(size_t)y*s->pitch);
            for(x=0;x<s->w;++x)row[x]=argb;
        } else if(s->bpp==2) {
            uint16_t value=(uint16_t)(((argb>>8)&0xF800u)|((argb>>5)&0x07E0u)|((argb>>3)&0x001Fu));
            uint16_t *row=(uint16_t *)(GUEST(s->va)+(size_t)y*s->pitch);
            for(x=0;x<s->w;++x)row[x]=value;
        } else return 0;
    }
    if(!snapshot_guest_surface(s))return 0;
    s->gpu_dirty=0;s->guest_check_gen=0;return 1;
}

/* Seeded from guest memory: whatever the title (or the boot) left there.
 * After this the GPU copy is authoritative. */
static int create_surf(Surf *s)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init, *pinit = NULL;
    if(!snapshot_guest_surface(s))return 0;
    s->scene_tainted=1; // Export needs a known combined color/depth reset.

    memset(&td, 0, sizeof td);
    td.Width = s->w;
    td.Height = s->h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = surf_format(s->bpp);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (s->pitch >= s->w * s->bpp) {
        init.pSysMem = GUEST(s->va);
        init.SysMemPitch = s->pitch;
        init.SysMemSlicePitch = 0;
        pinit = &init;
    }
    if (FAILED(ID3D11Device_CreateTexture2D(s_dev, &td, pinit, &s->tex)))
        return 0;
    ID3D11Device_CreateRenderTargetView(s_dev, (ID3D11Resource *)s->tex, NULL, &s->rtv);
    ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)s->tex, NULL, &s->srv);
    return s->rtv && s->srv;
}

static Surf *find_surf(uint32_t va)
{
    int i;
    for (i = 0; i < s_nsurf; i++)
        if (s_surf[i].va == va && s_surf[i].tex)
            return &s_surf[i];
    return NULL;
}

/* A surface's width is its pitch in pixels where that is at least the clip
 * width, so a texture reading the same memory with that pitch sees the same
 * layout. */
static Surf *get_surf(const Nv2aSurface *in)
{
    uint32_t bpp = in->bytes_per_pixel ? in->bytes_per_pixel : 4;
    uint32_t w = in->pitch && in->pitch / bpp >= in->width ? in->pitch / bpp : in->width;
    uint32_t h = in->height;
    Surf *s = NULL;
    int i;

    for (i = 0; i < s_nsurf; i++)
        if (s_surf[i].va == in->color_va) { s = &s_surf[i]; break; }
    if (s && (s->bpp != bpp || s->w < w || s->h < h || !s->tex)) {
        if (s_cur == s)
            s_cur = NULL;
        release_surf(s);
        s->bpp = bpp;
        s->w = w > s->w ? w : s->w;
        s->h = h > s->h ? h : s->h;
        s->pitch = in->pitch;
        if (!create_surf(s))
            return NULL;
    }
    if (!s) {
        if (s_nsurf == MAX_SURF)
            return NULL;
        s = &s_surf[s_nsurf++];
        memset(s, 0, sizeof *s);
        s->va = in->color_va;
        s->pitch = in->pitch;
        s->bpp = bpp;
        s->w = w;
        s->h = h;
        if (!create_surf(s)) {
            s_nsurf--;
            return NULL;
        }
    }
    return s;
}

static int readable_guest_range(uint32_t va, uint64_t bytes)
{
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    uint64_t guest_end = (uint64_t)va + bytes;
    uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset(), p, end;

    if (!va || !bytes || bytes > XBOX_CONTIG_SIZE ||
        !(guest_end <= XBOX_CONTIG_SIZE ||
          (va >= XBOX_CONTIG_BASE && guest_end <= (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE)) ||
        offset > UINTPTR_MAX - va)
        return 0;
    p = offset + va;
    if (bytes > UINTPTR_MAX - p)
        return 0;
    end = p + (uintptr_t)bytes;
    while (p < end) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t base, available;
        if (VirtualQuery((const void *)p, &info, sizeof info) != sizeof info ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & readable))
            return 0;
        base = (uintptr_t)info.BaseAddress;
        if (p < base || p - base >= info.RegionSize)
            return 0;
        available = info.RegionSize - (p - base);
        p += available < end - p ? available : end - p;
    }
    return 1;
}

/* Xbox Z24S8 stores depth in the high 24 bits; D3D11 R24G8 stores it in
 * the low 24. Seed before any GPU depth writes, not from stale guest bytes
 * when an existing GPU surface must be recreated. */
static uint32_t *depth_seed(uint32_t va, uint32_t w, uint32_t h, uint32_t pitch,
                            uint32_t format, uint32_t surface_type)
{
    uint64_t row = (uint64_t)w * 4, span;
    uint32_t *seed, x, y;
    if (!w || !h || format != 2 || surface_type != 1 || pitch < row)
        return NULL;
    span = (uint64_t)(h - 1) * pitch + row;
    if (!readable_guest_range(va, span))
        return NULL;
    seed = (uint32_t *)malloc((size_t)row * h);
    if (!seed) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate depth seed for surface %08X\n", va);
        return NULL;
    }
    for (y = 0; y < h; y++) {
        const uint8_t *src = GUEST(va) + (size_t)y * pitch;
        for (x = 0; x < w; x++) {
            uint32_t value;
            memcpy(&value, src + (size_t)x * 4, sizeof value);
            seed[(size_t)y * w + x] = (value >> 8) | (value << 24);
        }
    }
    return seed;
}

static Depth *get_depth(uint32_t va, uint32_t w, uint32_t h)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    Depth *d = NULL;
    uint32_t *seed = NULL;
    uint32_t pitch = nv2a_backend_reg(0x020C) >> 16;
    uint32_t surface_format = nv2a_backend_reg(0x0208);
    uint32_t format = (surface_format >> 4) & 0xF;
    uint32_t surface_type = (surface_format >> 8) & 0xF;
    int existing;
    HRESULT hr;
    int i;

    if (!va)
        return NULL;
    for (i = 0; i < s_ndepth; i++)
        if (s_depth[i].va == va) { d = &s_depth[i]; break; }
    existing = d != NULL;
    if (d && d->w >= w && d->h >= h && d->pitch == pitch &&
        d->format == format && d->surface_type == surface_type)
        return d;
    if (d) {
        if(scene_tracking_enabled()) {
            probe_scene_depth(d,NULL,BF_DEPTH_PROBE_RECREATE);
            invalidate_scene_depth(d->va,NULL);
        }
        if (s_cur_depth == d)
            s_cur_depth = NULL;
        if (d->pitch == pitch && d->format == format && d->surface_type == surface_type) {
            w = w > d->w ? w : d->w;
            h = h > d->h ? h : d->h;
        }
        if (d->snapshot_srv) ID3D11ShaderResourceView_Release(d->snapshot_srv);
        if (d->snapshot) ID3D11Texture2D_Release(d->snapshot);
        if (d->srv) ID3D11ShaderResourceView_Release(d->srv);
        if (d->dsv) ID3D11DepthStencilView_Release(d->dsv);
        if (d->tex) ID3D11Texture2D_Release(d->tex);
    } else {
        if (s_ndepth == MAX_SURF)
            return NULL;
        d = &s_depth[s_ndepth++];
    }
    memset(d, 0, sizeof *d);
    d->va = va; d->w = w; d->h = h; d->pitch = pitch; d->format = format;
    d->surface_type = surface_type; d->recreated = existing;
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = s_depth_texture ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | (s_depth_texture ? D3D11_BIND_SHADER_RESOURCE : 0);
    if (s_depth_texture && !existing)
        seed = depth_seed(va, w, h, pitch, format, surface_type);
    memset(&init, 0, sizeof init);
    init.pSysMem = seed;
    init.SysMemPitch = w * 4;
    hr = ID3D11Device_CreateTexture2D(s_dev, &td, seed ? &init : NULL, &d->tex);
    if (FAILED(hr) && seed) {
        fprintf(stderr, "[GPU-D3D11] depth seed upload for %08X failed 0x%08lX;"
                        " sampling will require a GPU depth clear\n", va, (unsigned long)hr);
        free(seed);
        seed = NULL;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &d->tex);
    }
    if (FAILED(hr))
        goto failed;
    if (seed) {
        d->initialized = 1;
        d->stencil_initialized = 1;
        s_st.depth_seeds++;
        free(seed);
        seed = NULL;
    }
    memset(&dv, 0, sizeof dv);
    dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = ID3D11Device_CreateDepthStencilView(s_dev, (ID3D11Resource *)d->tex, &dv, &d->dsv);
    if (FAILED(hr))
        goto failed;
    if (s_depth_texture) {
        memset(&sv, 0, sizeof sv);
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)d->tex, &sv, &d->srv);
        if (FAILED(hr))
            goto failed;
    }
    return d;

failed:
    free(seed);
    fprintf(stderr, "[GPU-D3D11] depth surface creation failed 0x%08lX\n", (unsigned long)hr);
    if (d->srv) ID3D11ShaderResourceView_Release(d->srv);
    if (d->dsv) ID3D11DepthStencilView_Release(d->dsv);
    if (d->tex) ID3D11Texture2D_Release(d->tex);
    memset(d, 0, sizeof *d);
    return NULL;
}

static ID3D11ShaderResourceView *get_depth_texture(const Nv2aTexture *t, int cube,
                                                    unsigned levels)
{
    static int warned;
    D3D11_TEXTURE2D_DESC td;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    Depth *d = NULL;
    HRESULT hr;
    int i;

    for (i = 0; i < s_ndepth; i++)
        if (s_depth[i].va == t->offset) { d = &s_depth[i]; break; }
    if (!d || !d->srv || cube || levels > 1 || d->format != 2 || d->surface_type != 1 ||
        t->width != d->w || t->height != d->h || t->pitch != d->pitch ||
        t->pitch < t->width * 4) {
        s_st.depth_rejected++;
        if (!warned++) {
            fprintf(stderr, "[GPU-D3D11] depth texture alias rejected: va %08X %ux%u"
                            " pitch %u levels %u cube %d; surface %s %ux%u pitch %u"
                            " format %u type %u\n",
                    t->offset, t->width, t->height, t->pitch, levels, cube,
                    d ? "tracked" : "missing", d ? d->w : 0, d ? d->h : 0,
                    d ? d->pitch : 0, d ? d->format : 0, d ? d->surface_type : 0);
        }
        return NULL;
    }
    if (!d->initialized) {
        static int warned_uninitialized;
        s_st.depth_rejected++;
        if (!warned_uninitialized++)
            fprintf(stderr, "[GPU-D3D11] depth texture alias rejected: surface %08X"
                            " has not been initialized from guest memory or a GPU depth clear"
                            " (%ux%u pitch %u, type %u, recreated %u)\n",
                    d->va, d->w, d->h, d->pitch, d->surface_type, d->recreated);
        return NULL;
    }
    s_st.depth_samples++;
    if (d != s_cur_depth)
        return d->srv;
    if (!d->snapshot) {
        ID3D11Texture2D_GetDesc(d->tex, &td);
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &d->snapshot);
        if (FAILED(hr))
            goto failed;
        memset(&sv, 0, sizeof sv);
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)d->snapshot,
                                                   &sv, &d->snapshot_srv);
        if (FAILED(hr))
            goto failed;
    }
    bind_output_views(s_cur,NULL);
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)d->snapshot,
                                     (ID3D11Resource *)d->tex);
    bind_output_views(s_cur,d);
    s_st.depth_snapshots++;
    return d->snapshot_srv;

failed:
    fprintf(stderr, "[GPU-D3D11] depth snapshot creation failed 0x%08lX\n", (unsigned long)hr);
    if (d->snapshot_srv) ID3D11ShaderResourceView_Release(d->snapshot_srv);
    if (d->snapshot) ID3D11Texture2D_Release(d->snapshot);
    d->snapshot = NULL;
    d->snapshot_srv = NULL;
    return NULL;
}

static void reject_depth_mode(uint32_t mode, int cube, int base)
{
    static uint32_t warned_base, warned_extended;
    static int warned_cube;
    uint32_t *warned = base ? &warned_base : &warned_extended;
    s_st.depth_rejected++;
    if (cube) {
        if (!warned_cube++)
            fprintf(stderr, "[GPU-D3D11] depth cube textures are unsupported\n");
    } else if (!(*warned & (1u << mode))) {
        *warned |= 1u << mode;
        fprintf(stderr, "[GPU-D3D11] depth texture stage mode %u is unsupported on %s draws\n",
                mode, base ? "base" : "extended");
    }
}

static void motion_disable_draw(void)
{
    memset(&s_motion_binding,0,sizeof s_motion_binding);s_motion_draw_color=0;
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx,2,1,&s_motion_zero_cb);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx,2,1,&s_motion_zero_cb);
}
static int motion_source(Surf *surface,const Nv2aRenderState *rs,BfMotionSource *source)
{
    BFMotionRecord record;float near_z,far_z;
    if(!bf_motion_observer_enabled()||!surface||surface->w!=640||surface->h!=480||surface->bpp!=4||surface->ui_generation==s_gen)return 0;
    if(!s_motion_renderer)s_motion_renderer=bf_motion_renderer_create(s_dev,s_ctx);
    if(!s_motion_renderer)return 0;
    memset(source,0,sizeof *source);source->frame=s_gen;source->color_va=surface->va;source->depth_va=rs?rs->zeta_va:0;
    source->width=surface->w;source->height=surface->h;
    /* Missing scopes remain observed unknown work; silently skipping such a
     * draw could incorrectly bless an otherwise tracked subset of the frame. */
    if(!bf_motion_current_record(&record)||!record.words[BF_MO_VIEW_VALID])return 1;
    memcpy(&near_z,record.words+BF_MO_PROJECTION_WORDS+2,4);memcpy(&far_z,record.words+BF_MO_PROJECTION_WORDS+3,4);
    source->view=record.words[BF_MO_VIEW];source->context=record.words[BF_MO_CONTEXT];
    source->near_plane=near_z;source->far_plane=far_z;
    if(s_motion_jitter_frame!=s_gen){s_motion_jitter_frame=s_gen;s_motion_latched_jitter=s_motion_next_jitter;}
    if(s_motion_latched_jitter) {
        BfTemporalView view;
        if(bf_temporal_prepare_view(s_dev,s_ctx,s_gen,surface->va,surface->w,surface->h,&record,&view)) {
            source->jitter_x=view.jitter_x;source->jitter_y=view.jitter_y;source->apply_jitter=1;
        }
    }
    return 1;
}
static void motion_prepare_draw(Surf *surface,const Nv2aRenderState *rs,const Nv2aXVsBatch *batch)
{
    BfMotionSource source;BFMotionRecord identity;ID3D11ShaderResourceView *views[2];
    motion_disable_draw();
    if(!motion_source(surface,rs,&source))return;
    memset(&identity,0,sizeof identity);
    if(batch)bf_motion_draw_record(batch->command_va,&identity);
    if(!bf_motion_renderer_prepare(s_motion_renderer,&source,rs,batch,&identity,&s_motion_binding))return;
    s_motion_draw_color=surface->va;
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx,2,1,&s_motion_binding.constants);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx,2,1,&s_motion_binding.constants);
    views[0]=s_motion_binding.original_ids;views[1]=s_motion_binding.previous;
    ID3D11DeviceContext_VSSetShaderResources(s_ctx,1,2,views);
}
void bruteforce_d3d11_temporal_jitter(int enabled){s_motion_next_jitter=enabled!=0;}
int bruteforce_d3d11_motion_snapshot(uint32_t color,uint64_t frame,BfMotionSnapshot *snapshot)
{
    return frame<=UINT32_MAX&&bf_motion_renderer_snapshot(s_motion_renderer,color,(uint32_t)frame,snapshot);
}

/* Slot 0 remains the game's original target. Slots 1/2 are reserved for
 * motion/reactivity, and slot 3 contains only post-boundary UI coverage. */
static void bind_output_views(Surf *s,Depth *d)
{
    ID3D11RenderTargetView *targets[6]={s->rtv,NULL,NULL,NULL,NULL,NULL};
    targets[3]=ui_capture_target(s);
    if(s_motion_draw_color==s->va&&s_motion_binding.outputs) {
        bf_motion_renderer_targets(s_motion_renderer,s->va,s_gen,&targets[1],&targets[2],&targets[4],&targets[5]);
        if(!(s_motion_binding.outputs&BF_MOTION_BIND_VECTOR))targets[1]=NULL;
        if(!(s_motion_binding.outputs&BF_MOTION_BIND_MASKS))targets[2]=targets[5]=NULL;
        if(!(s_motion_binding.outputs&BF_MOTION_BIND_DEPTH))targets[4]=NULL;
    }
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx,targets[1]||targets[2]||targets[4]||targets[5]?6:targets[3]?4:1,targets,d?d->dsv:NULL);
}

static void ui_capture_prepare(Surf *s,const Nv2aRenderState *rs)
{
    D3D11_BUFFER_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    uint32_t values[4]={0,0,0,0};
    s_ui_capture_mode=BF_UI_BLEND_NO_RGB;
    if(!ui_capture_target(s))return;
    if(!rs) {bf_ui_composition_invalidate(s->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_BLEND);return;}
    s_ui_capture_mode=bf_ui_blend_mode(rs->blend_enable,rs->blend_src,rs->blend_dst,rs->blend_eq,rs->color_mask);
    if(s_ui_capture_mode==BF_UI_BLEND_UNSUPPORTED) {
        bf_ui_composition_invalidate(s->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_BLEND);return;
    }
    if(!s_ui_capture_cb) {
        memset(&desc,0,sizeof desc);desc.ByteWidth=sizeof values;desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(ID3D11Device_CreateBuffer(s_dev,&desc,NULL,&s_ui_capture_cb))) {
            bf_ui_composition_invalidate(s->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_TARGET);return;
        }
    }
    if(FAILED(ID3D11DeviceContext_Map(s_ctx,(ID3D11Resource *)s_ui_capture_cb,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {
        bf_ui_composition_invalidate(s->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_TARGET);return;
    }
    values[0]=(uint32_t)s_ui_capture_mode;memcpy(mapped.pData,values,sizeof values);
    ID3D11DeviceContext_Unmap(s_ctx,(ID3D11Resource *)s_ui_capture_cb,0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx,3,1,&s_ui_capture_cb);
}

static void bind_targets(Surf *s, Depth *d)
{
    D3D11_VIEWPORT vp;
    ID3D11ShaderResourceView *none[4] = { NULL, NULL, NULL, NULL };

    /* A surface about to be drawn into must not stay bound as a texture. */
    ID3D11DeviceContext_PSSetShaderResources(s_ctx, 0, 4, none);
    bind_output_views(s,d);
    vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width = (float)s->w; vp.Height = (float)s->h;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(s_ctx, 1, &vp);
    s_cur = s;
    s_cur_depth = d;
}

/* Reading and writing one D3D resource in the same draw is forbidden. Copy
 * prior colour work into a distinct SRV before feedback, with both previous
 * PS inputs and OM outputs unbound. The immediate-context command order also
 * orders the copy on D3D11On12; it needs no CPU wait or visibility-query reset. */
static ID3D11ShaderResourceView *color_snapshot(Surf *s)
{
    if(s->ui_generation==s_gen)bf_ui_composition_source_changed(s->ui_overlay,BF_UI_SOURCE_CHANGE_TEXTURE_FEEDBACK);
    D3D11_TEXTURE2D_DESC td;
    ID3D11ShaderResourceView *none[4]={NULL,NULL,NULL,NULL};
    HRESULT hr;
    if(!s->snapshot) {
        ID3D11Texture2D_GetDesc(s->tex,&td);
        td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        hr=ID3D11Device_CreateTexture2D(s_dev,&td,NULL,&s->snapshot);
        if(FAILED(hr))goto failed;
        hr=ID3D11Device_CreateShaderResourceView(s_dev,(ID3D11Resource *)s->snapshot,NULL,&s->snapshot_srv);
        if(FAILED(hr))goto failed;
    }
    ID3D11DeviceContext_PSSetShaderResources(s_ctx,0,4,none);
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx,0,NULL,NULL);
    ID3D11DeviceContext_CopyResource(s_ctx,(ID3D11Resource *)s->snapshot,(ID3D11Resource *)s->tex);
    bind_output_views(s_cur,s_cur_depth);
    return s->snapshot_srv;
failed:
    fprintf(stderr,"[GPU-D3D11] colour feedback snapshot creation failed 0x%08lX\n",(unsigned long)hr);
    if(s->snapshot_srv)ID3D11ShaderResourceView_Release(s->snapshot_srv);
    if(s->snapshot)ID3D11Texture2D_Release(s->snapshot);
    s->snapshot_srv=NULL;s->snapshot=NULL;
    return NULL;
}

/* The surface clip, as the CPU rasteriser bounds its triangles. */
static void apply_scissor(const Nv2aSurface *s)
{
    D3D11_RECT r;
    r.left = 0; r.top = 0;
    r.right = (LONG)(s->width < s_cur->w ? s->width : s_cur->w);
    r.bottom = (LONG)(s->height < s_cur->h ? s->height : s_cur->h);
    ID3D11DeviceContext_RSSetScissorRects(s_ctx, 1, &r);
}

static int begin(const Nv2aSurface *s, const Nv2aRenderState *rs)
{
    Surf *surf;
    Depth *d;
    int motion_was_bound;

    if (!ensure_device() || !s || !s->color_va)
        return 0;
    surf = get_surf(s);
    if (!surf)
        return 0;
    if(!sync_guest_surface(surf,0))return 0;
    motion_was_bound=s_motion_binding.outputs!=0;
    motion_disable_draw();
    if(surf->w==640&&surf->h==480&&surf->bpp==4)bf_temporal_probe_device(s_dev,s_ctx,surf->w,surf->h);
    d = rs ? get_depth(rs->zeta_va, surf->w, surf->h) : NULL;
    if(bf_ui_composition_enabled())ui_capture_prepare(surf,rs);
    if (surf != s_cur || d != s_cur_depth || motion_was_bound || bf_ui_composition_enabled() || bf_motion_observer_enabled())
        bind_targets(surf, d);
    apply_scissor(s);
    return 1;
}

static void mark_depth_written(const Nv2aRenderState *rs)
{
    if(temporal_depth_enabled()&&s_cur&&rs&&rs->depth_test_enable&&
       (rs->depth_write||(rs->color_mask&0x00010101u))&&s_cur->ui_generation!=s_gen) {
        BFMotionRecord record;
        if(bf_motion_current_record(&record)&&record.words[BF_MO_VIEW_VALID]) {
            /* Serial changes on every real world submission, including
             * depth-read-only geometry. A frozen depth may only be joined
             * to later color if no such submission intervened. */
            if(s_cur->temporal_geometry_serial!=UINT64_MAX)++s_cur->temporal_geometry_serial;
            s_cur->temporal_identity.view=record.words[BF_MO_VIEW];
            s_cur->temporal_identity.context=record.words[BF_MO_CONTEXT];
        } else {
            s_cur->temporal_frozen=0;
            s_cur->temporal_identity.view=s_cur->temporal_identity.context=0;
        }
    }
    if(s_wide_scope&&s_cur&&s_cur->w==640&&s_cur->h==480)
        s_cur->wide_generation=s_gen;
    if(s_cur_depth&&rs&&(rs->depth_write||rs->stencil_enable))s_cur_depth->gpu_dirty=1;
    /* Metadata only: potential writes through another color, or unconditional
     * depth writes without a world depth test, invalidate prior provenance.
     * Disabled export does no surface iteration. Stencil-only work is safe. */
    if(s_cur&&s_cur_depth&&rs&&rs->depth_write&&scene_tracking_enabled()) {
        invalidate_scene_depth(s_cur_depth->va,rs->depth_test_enable?s_cur:NULL);
        if(s_cur->scene_depth_va!=s_cur_depth->va)s_cur->scene_tainted=1;
        if(!rs->depth_test_enable||s_cur->scene_tainted)return;
        if(s_cur->scene_generation!=s_gen)
            s_cur->scene_depth_draws=0;
        s_cur->scene_generation=s_gen;s_cur->scene_depth_va=s_cur_depth->va;
        ++s_cur->scene_depth_draws;
    }
}

/* ---- textures ------------------------------------------------------------ */

#define MAX_TEX 2048
typedef struct {
    uint32_t offset, w, h, pitch, color, levels, palette;
    uint8_t *source_copy;
    size_t source_bytes;
    uint8_t palette_copy[1024];
    unsigned palette_entries;
    uint32_t gen, stable, last_use;
    uint8_t cube;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    ID3D11Texture2D *enhanced;
    ID3D11ShaderResourceView *enhanced_srv;
    uint64_t enhanced_bytes;
    unsigned enhancement_factor;
} Tex;

static Tex *s_tex;
static int s_ntex;
/* Direct-mapped hints store slot+1, never an authority on texture identity or
 * content. Slots are fixed for this device's lifetime. An LRU replacement or
 * bucket collision must pass the full key check or use the original search. */
#define TEX_HINT_COUNT 4096u
static uint16_t s_tex_hint[TEX_HINT_COUNT];
static unsigned texture_hint(const Nv2aTexture *t, int cube, unsigned palette_entries)
{
    uint32_t key=(t->offset>>7)^(t->offset>>19)^t->palette^t->pitch^
        (t->width<<3)^(t->height<<15)^t->color^(t->levels<<8)^
        (palette_entries<<20)^(uint32_t)(uint8_t)cube;
    key^=key>>16;key*=0x9E3779B1u;key^=key>>16;
    return key&(TEX_HINT_COUNT-1u);
}
static int texture_key_matches(const Tex *e,const Nv2aTexture *t,int cube,unsigned palette_entries)
{
    return e->offset==t->offset&&e->w==t->width&&e->h==t->height&&
        e->color==t->color&&e->pitch==t->pitch&&e->cube==(uint8_t)cube&&
        e->palette==t->palette&&e->palette_entries==palette_entries&&e->levels==t->levels;
}
static uint32_t *s_decode;
static size_t s_decode_cap;
static GpuTextureDecoder s_texture_decoder;
static int s_tex_gpu = 1, s_tex_log;
static uint64_t s_asset_bytes;
static void release_enhancement(Tex *e) {
    if(e->enhanced_srv)ID3D11ShaderResourceView_Release(e->enhanced_srv);
    if(e->enhanced)ID3D11Texture2D_Release(e->enhanced);
    s_asset_bytes-=e->enhanced_bytes;e->enhanced_bytes=0;
    e->enhanced=NULL;e->enhanced_srv=NULL;e->enhancement_factor=0;
}
static void release_asset_cache(void){int i;for(i=0;i<s_ntex;i++)release_enhancement(&s_tex[i]);}
static ID3D11ShaderResourceView *asset_view(Tex *e,int eligible) {
    static unsigned previous_factor=1;
    const unsigned factor=bf_ui_texture_scale();
    if(previous_factor!=factor){release_asset_cache();previous_factor=factor;}
    if(!eligible){if(e->enhanced_srv)release_enhancement(e);return e->srv;}
    if(e->enhancement_factor!=factor) {
        D3D11_TEXTURE2D_DESC desc;uint64_t bytes=0;unsigned w,h,mip,levels;
        release_enhancement(e);e->enhancement_factor=factor;
        if(factor!=2&&factor!=4)return e->srv;
        ID3D11Texture2D_GetDesc(e->tex,&desc);
        w=desc.Width*factor;h=desc.Height*factor;levels=desc.MipLevels+(factor==4?2:1);
        if(w>4096||h>4096)return e->srv;
        for(mip=0;mip<levels;mip++){bytes+=(uint64_t)w*h*desc.ArraySize*4;w=w>1?w/2:1;h=h>1?h/2:1;}
        /* A bounded 256-MiB optional cache. Original sampled images remain
         * authoritative; allocation/budget failure simply retains them. */
        if(bytes<=256ull*1024*1024-s_asset_bytes&&bf_asset_upscale(s_dev,s_ctx,e->tex,factor,&e->enhanced,&e->enhanced_srv)) {
            e->enhanced_bytes=bytes;s_asset_bytes+=bytes;
            if(getenv("BF_TRACE_ASSETS"))fprintf(stderr,"[ASSET] %ux %ux%u faces=%u mips=%u cache=%llu bytes\n",factor,desc.Width,desc.Height,desc.ArraySize,desc.MipLevels,(unsigned long long)s_asset_bytes);
        }
    }
    return e->enhanced_srv?e->enhanced_srv:e->srv;
}

/* The bytes level 0 (all faces, for a cube) occupies in guest memory. */
static size_t tex_bytes(const Nv2aTexture *t, int faces)
{
    return gpu_texture_span(t, faces);
}

static void release_tex(Tex *e)
{
    release_enhancement(e);
    if (e->srv) ID3D11ShaderResourceView_Release(e->srv);
    if (e->tex) ID3D11Texture2D_Release(e->tex);
    e->srv = NULL;
    e->tex = NULL;
    free(e->source_copy); e->source_copy = NULL; e->source_bytes = 0;
}

/* A slot for a texture: its old entry, a free one, or the least recently
 * used. */
static Tex *tex_slot(Tex *e)
{
    int i, lru = 0;
    if (e) {
        release_tex(e);
        return e;
    }
    if (s_ntex < MAX_TEX)
        return &s_tex[s_ntex++];
    for (i = 1; i < s_ntex; i++)
        if (s_tex[i].last_use < s_tex[lru].last_use)
            lru = i;
    release_tex(&s_tex[lru]);
    return &s_tex[lru];
}

static int ensure_decode(size_t texels)
{
    if (texels <= s_decode_cap)
        return 1;
    free(s_decode);
    s_decode = (uint32_t *)malloc(texels * 4);
    s_decode_cap = s_decode ? texels : 0;
    return s_decode != NULL;
}

/* Upload the title's original mip chain, never a generated/blurred substitute.
 * Each cube face has its own complete chain and 128-byte-aligned stride.
 * Decode each level independently so swizzle masks follow that level's size. */
static int upload_mips(Tex *e, const Nv2aTexture *t, int cube, unsigned palette_entries)
{
    D3D11_TEXTURE2D_DESC td = {0};
    D3D11_SUBRESOURCE_DATA init[6*13] = {{0}};
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {0};
    uint32_t *decoded[6*13] = {0};
    unsigned levels = gpu_texture_mip_levels(t), faces = cube ? 6 : 1, face, level, i;
    unsigned block = d3d8_format_dxt_block_bytes(t->color);
    size_t stride = cube ? gpu_texture_face_stride(t) : 0;
    HRESULT hr;
    int ok = 0;
    if (!levels || levels > 13) return 0;
    td.Width=t->width;td.Height=t->height;td.MipLevels=levels;td.ArraySize=faces;
    td.Format=block?(t->color==0x0C?DXGI_FORMAT_BC1_UNORM:t->color==0x0E?DXGI_FORMAT_BC2_UNORM:DXGI_FORMAT_BC3_UNORM):DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags=cube?D3D11_RESOURCE_MISC_TEXTURECUBE:0;
    for(face=0;face<faces;face++)for(level=0;level<levels;level++) {
        Nv2aTexture mip;
        size_t offset, bytes;
        i=face*levels+level;
        if(!gpu_texture_mip(t,level,&mip,&offset,&bytes))goto done;
        /* get_texture checked the entire chain against mapped guest RAM. */
        mip.offset=t->offset+(uint32_t)(face*stride+offset);
        if(block) {
            init[i].pSysMem=GUEST(mip.offset);
            init[i].SysMemPitch=((mip.width+3)/4)*block;
        } else {
            decoded[i]=(uint32_t *)malloc((size_t)mip.width*mip.height*4);
            if(!decoded[i])goto done;
            if(mip.color==0x0B) {
                if(!gpu_texture_decode_p8_cpu(&mip,1,GUEST(mip.offset),GUEST(mip.palette),palette_entries,decoded[i]))goto done;
            } else if(!nv2a_backend_decode_texture(&mip,decoded[i]))goto done;
            init[i].pSysMem=decoded[i];init[i].SysMemPitch=mip.width*4;
        }
    }
    hr=ID3D11Device_CreateTexture2D(s_dev,&td,init,&e->tex);
    if(FAILED(hr))goto done;
    sv.Format=td.Format;
    if(cube){sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;sv.TextureCube.MipLevels=levels;}
    else{sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=levels;}
    hr=ID3D11Device_CreateShaderResourceView(s_dev,(ID3D11Resource *)e->tex,&sv,&e->srv);
    if(FAILED(hr))goto done;
    s_st.uploads++;ok=1;
done:
    for(i=0;i<6*13;i++)free(decoded[i]);
    if(!ok){fprintf(stderr,"[GPU-D3D11] original mip-chain upload rejected\n");release_tex(e);}
    return ok;
}

/* Level 0 of a 2D texture (or all six faces of a cube) to the GPU. DXT
 * blocks go as they are (BC1-3 are the same S3TC blocks); everything else
 * through the framework's decoder, the same one the CPU path samples with. */
static int upload(Tex *e, const Nv2aTexture *t, int cube, unsigned palette_entries, const char **path)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init[6];
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    int dxt = (int)d3d8_format_dxt_block_bytes(t->color), faces = cube ? 6 : 1, f;
    size_t face_bytes = cube ? gpu_texture_face_stride(t) : tex_bytes(t, 1);
    HRESULT hr;
    int decoded_gpu = 0;

    if(gpu_texture_mip_levels(t)>1) {
        *path="original-mips";
        return upload_mips(e,t,cube,palette_entries);
    }

    memset(&td, 0, sizeof td);
    td.Width = t->width;
    td.Height = t->height;
    td.MipLevels = 1;
    td.ArraySize = (UINT)faces;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
    memset(init, 0, sizeof init);
    if (dxt && !t->pitch && (t->width & 3) == 0 && (t->height & 3) == 0) {
        td.Format = t->color == 0x0C ? DXGI_FORMAT_BC1_UNORM
                  : t->color == 0x0E ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_BC3_UNORM;
        for (f = 0; f < faces; f++) {
            init[f].pSysMem = GUEST(t->offset) + f * face_bytes;
            init[f].SysMemPitch = (t->width / 4) * (UINT)dxt;
        }
        *path = "BC";
        s_st.tex_bc++;
    } else if (t->color == 0x12 && t->pitch >= t->width * 4) {
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        for (f = 0; f < faces; f++) {
            init[f].pSysMem = GUEST(t->offset) + f * face_bytes;
            init[f].SysMemPitch = t->pitch;
        }
        *path = "direct";
        s_st.tex_direct++;
    } else {
        if (s_tex_gpu && gpu_texture_decode_supported(t)) {
            decoded_gpu = gpu_texture_decode(&s_texture_decoder, s_dev, s_ctx, t, faces,
                                              GUEST(t->offset),
                                              t->palette ? GUEST(t->palette) : NULL, palette_entries, &e->tex);
            if (!decoded_gpu)
                fprintf(stderr, "[GPU-D3D11] falling back to CPU texture decoding\n");
        }
        if (decoded_gpu) {
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            *path = "GPU";
            s_st.tex_gpu++;
        } else {
            size_t texels = (size_t)t->width * t->height;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            if (!ensure_decode(texels * faces)) {
                fprintf(stderr, "[GPU-D3D11] cannot allocate CPU texture decode buffer\n");
                return 0;
            }
            for (f = 0; f < faces; f++) {
                Nv2aTexture face = *t;
                face.offset = t->offset + (uint32_t)(f * face_bytes);
                if (!nv2a_backend_decode_texture(&face, s_decode + texels * f)) {
                    static unsigned failures;
                    if (failures++ < 10)
                        fprintf(stderr, "[GPU-D3D11] cannot decode texture format 0x%02X"
                                        " at 0x%08X%s\n", t->color, t->offset,
                                failures == 10 ? " (further failures suppressed)" : "");
                    return 0;
                }
                init[f].pSysMem = s_decode + texels * f;
                init[f].SysMemPitch = t->width * 4;
            }
            *path = "CPU";
            s_st.tex_cpu++;
        }
    }
    if (!decoded_gpu) {
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, init, &e->tex);
        if (FAILED(hr)) {
            fprintf(stderr, "[GPU-D3D11] texture upload failed 0x%08lX\n", (unsigned long)hr);
            return 0;
        }
    }
    memset(&sv, 0, sizeof sv);
    sv.Format = td.Format;
    if (cube) {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = 1;
    } else {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
    }
    hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)e->tex, &sv, &e->srv);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] texture view creation failed 0x%08lX\n", (unsigned long)hr);
        release_tex(e);
        return 0;
    }
    s_st.uploads++;
    return 1;
}

/* The view a stage samples, and the size its texel-space coordinates are
 * divided by (*tw, *th). */
static ID3D11ShaderResourceView *lookup_texture(const Nv2aTexture *t, int cube, unsigned stage,
                                             float *tw, float *th,TextureProfileSample *profile)
{
    Tex *e = NULL;
    Surf *rt;
    Nv2aTexture normalized;
    const char *path = NULL;
    int rewritten,asset_eligible=1;
    unsigned palette_entries = t && t->color == 0x0B ? 256u >> ((nv2a_backend_reg(0x1B20 + stage * 0x40) >> 2) & 3u) : 0;
    int i, faces = cube ? 6 : 1;
    unsigned hint;
    size_t source_bytes;

    if (!t || !t->width || !t->height || t->width > 4096 || t->height > 4096)
        return NULL;
    if (t->pitch && (d3d8_format_is_swizzled(t->color)
                     || d3d8_format_dxt_block_bytes(t->color))) {
        normalized = *t;
        normalized.pitch = 0;
        t = &normalized;
        s_st.tex_pitch++;
    }
    *tw = (float)t->width;
    *th = (float)t->height;
    if (s_depth_texture && t->color == 0x2E)
        return get_depth_texture(t, cube, (nv2a_backend_reg(0x1B04 + stage * 0x40) >> 16) & 0xF);
    if (!cube) {
        /* Direct SRVs are valid only for identical linear interpretations.
         * Other formats, pitches, dimensions or mip chains must decode the
         * actual shared bytes; matching the base address alone is insufficient. */
        rt = find_surf(t->offset);
        if (rt && t->width==rt->w && t->height==rt->h && t->pitch==rt->pitch &&
            gpu_texture_mip_levels(t)==1 &&
            ((rt->bpp==4 && t->color==0x12) || (rt->bpp==2 && t->color==0x11))) {
            /* CPU decoders can rewrite an offscreen source after readback,
             * including twice in one flip generation. Always compare it. */
            if(!sync_guest_surface(rt,1))return NULL;
            s_st.rtt++;
            *tw = (float)rt->w;
            *th = (float)rt->h;
            if(rt!=s_cur)return rt->srv;
            {
                ID3D11ShaderResourceView *snapshot=color_snapshot(rt);
                if(snapshot)return snapshot;
                /* Allocation failure may still use the coherent guest path. */
            }
        }
    }
    /* The normalized host descriptor and face count stay fixed in this
     * synchronous lookup. Guest byte rewrites cannot change this pure span;
     * retain its original first evaluation point after direct-alias returns. */
    source_bytes=tex_bytes(t,faces);
    if (!source_bytes) {
        fprintf(stderr, "[GPU-D3D11] invalid texture span for format 0x%02X, %ux%u\n",
                t->color, t->width, t->height);
        return NULL;
    }
    {
        uint64_t offset = t->offset & 0x0FFFFFFFu;
        if (offset + source_bytes > g_xbox_total_ram ||
            (palette_entries && (!t->palette || (uint64_t)(t->palette & 0x0FFFFFFFu) + palette_entries * 4 > g_xbox_total_ram))) {
            fprintf(stderr, "[GPU-D3D11] texture outside mapped guest RAM\n"); abort();
        }
        { int alias=resolve_depth_color_alias(t,cube);if(alias<0)return NULL;if(alias)asset_eligible=0; }
        /* This includes interior aliases, exact format reinterpretations,
         * swizzled/mip textures and cube faces. Resolve every overlapping
         * GPU-owned span before cache comparison/hash/upload, leaving all
         * layout and format handling to the existing texture decoder. Spans
         * use 64-bit arithmetic and the guest physical-address mask. */
        for(i=0;i<s_nsurf;i++) {
            Surf *source=&s_surf[i];
            uint64_t start=source->va&0x0FFFFFFFu;
            uint64_t end=start+(uint64_t)source->pitch*source->h;
            if(source->tex && offset<end && start<offset+source_bytes) {
                asset_eligible=0;
                s_st.overlap_rtt++;
                if(source->gpu_dirty && !d3d_readback(source->va))return NULL;
            }
        }
    }
    texture_profile_next(profile,TEX_PROFILE_DESCRIPTOR);
    hint=texture_hint(t,cube,palette_entries);
    i=(int)s_tex_hint[hint]-1;
    if(i>=0&&i<s_ntex&&texture_key_matches(&s_tex[i],t,cube,palette_entries)) {
        e=&s_tex[i];s_st.tex_hint_hits++;
    } else {
        s_st.tex_searches++;
        for(i=0;i<s_ntex;i++)if(texture_key_matches(&s_tex[i],t,cube,palette_entries)) {
            e=&s_tex[i];s_tex_hint[hint]=(uint16_t)(i+1);break;
        }
    }
    texture_profile_next(profile,TEX_PROFILE_VALIDATE);
    /* Exact source snapshots detect rewrites even within one frame. The old
     * full/sparse hashes were write-only metadata; do not scan these bytes a
     * second time. Count the requested comparison span, including early exits. */
    if (e && e->source_bytes == source_bytes && e->source_copy)
        s_st.tex_compared_bytes += e->source_bytes;
    if (e && e->source_bytes == source_bytes && e->source_copy
        && !memcmp(e->source_copy, GUEST(t->offset), e->source_bytes)
        && (t->color != 0x0B || !memcmp(e->palette_copy, GUEST(t->palette), palette_entries * 4))) {
        texture_profile_next(profile,TEX_PROFILE_PREPARE);
        e->last_use = s_gen;
        return asset_view(e,asset_eligible);
    }
    texture_profile_next(profile,TEX_PROFILE_PREPARE);
    rewritten = e != NULL;
    e = tex_slot(e);
    memset(e, 0, sizeof *e);
    if (palette_entries) {
        unsigned invalid_index;
        if (!gpu_texture_p8_indices_valid(t, faces, GUEST(t->offset), palette_entries, &invalid_index)) {
            fprintf(stderr,"[GPU-D3D11] P8 index %u exceeds palette of %u entries\n",invalid_index,palette_entries); abort();
        }
    }
    if (!upload(e, t, cube, palette_entries, &path))
        return NULL;
    e->offset = t->offset; e->w = t->width; e->h = t->height;
    e->color = t->color; e->pitch = t->pitch; e->cube = (uint8_t)cube;
    e->levels = t->levels; e->palette = t->palette;
    e->source_bytes = source_bytes;
    e->source_copy = (uint8_t *)malloc(e->source_bytes);
    if (!e->source_copy) { fprintf(stderr,"[GPU-D3D11] texture snapshot allocation failed\n"); abort(); }
    memcpy(e->source_copy,GUEST(t->offset),e->source_bytes);
    e->palette_entries = palette_entries;
    if (palette_entries) memcpy(e->palette_copy,GUEST(t->palette),palette_entries * 4);
    e->gen = s_gen; e->last_use = s_gen;
    s_tex_hint[hint]=(uint16_t)(e-s_tex+1);
    if (s_tex_log)
        fprintf(stderr, "[GPU-TEX] %s %s va=0x%08X format=0x%02X %ux%u pitch=%u"
                        " faces=%d levels=%u palette=0x%08X\n",
                rewritten ? "changed" : "new", path, t->offset, t->color, t->width,
                t->height, t->pitch, faces, t->levels, t->palette);
    return asset_view(e,asset_eligible);
}

static ID3D11ShaderResourceView *get_texture(const Nv2aTexture *t, int cube, unsigned stage,
                                             float *tw, float *th)
{
    ID3D11ShaderResourceView *view;
    TextureProfileSample sample;
    uint64_t finished,elapsed;
    if (!profile_enabled()) return lookup_texture(t, cube, stage, tw, th,NULL);
    sample.phase=TEX_PROFILE_ALIAS;sample.started=sample.last=texture_profile_ticks();
    ++s_texture_phases[TEX_PROFILE_ALIAS].calls;
    view = lookup_texture(t, cube, stage, tw, th,&sample);
    /* The wrapper closes the last phase even for invalid/unsupported or
     * direct-RTT early returns. Four exclusive phases partition this exact
     * lookup's wall time; nested GPU waits belong to alias_sync. */
    finished=texture_profile_ticks();elapsed=finished-sample.started;
    s_texture_phases[sample.phase].ticks+=finished-sample.last;
    s_texture_profile.ticks+=elapsed;
    s_texture_profile.ms += texture_profile_ns(elapsed)/1000000.0;
    ++s_texture_profile.calls;
    return view;
}

/* ---- state objects --------------------------------------------------------- */

static D3D11_BLEND map_blend(uint32_t f)
{
    switch (f) {
    case 0: return D3D11_BLEND_ZERO;
    case 1: return D3D11_BLEND_ONE;
    case 0x300: return D3D11_BLEND_SRC_COLOR;
    case 0x301: return D3D11_BLEND_INV_SRC_COLOR;
    case 0x302: return D3D11_BLEND_SRC_ALPHA;
    case 0x303: return D3D11_BLEND_INV_SRC_ALPHA;
    case 0x304: return D3D11_BLEND_DEST_ALPHA;
    case 0x305: return D3D11_BLEND_INV_DEST_ALPHA;
    case 0x306: return D3D11_BLEND_DEST_COLOR;
    case 0x307: return D3D11_BLEND_INV_DEST_COLOR;
    case 0x308: return D3D11_BLEND_SRC_ALPHA_SAT;
    case 0x8001: case 0x8003: return D3D11_BLEND_BLEND_FACTOR;
    case 0x8002: case 0x8004: return D3D11_BLEND_INV_BLEND_FACTOR;
    default: return D3D11_BLEND_ONE;
    }
}

static D3D11_BLEND alpha_only(D3D11_BLEND b)
{
    switch (b) {
    case D3D11_BLEND_SRC_COLOR: return D3D11_BLEND_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_ALPHA;
    case D3D11_BLEND_DEST_COLOR: return D3D11_BLEND_DEST_ALPHA;
    case D3D11_BLEND_INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_ALPHA;
    default: return b;
    }
}

static D3D11_BLEND_OP map_blend_op(uint32_t e)
{
    switch (e) {
    case 0x8007: return D3D11_BLEND_OP_MIN;
    case 0x8008: return D3D11_BLEND_OP_MAX;
    case 0x800A: return D3D11_BLEND_OP_SUBTRACT;
    case 0x800B: return D3D11_BLEND_OP_REV_SUBTRACT;
    default: return D3D11_BLEND_OP_ADD;
    }
}

static D3D11_COMPARISON_FUNC map_cmp(uint32_t f)
{
    switch (f) {
    case 0x200: return D3D11_COMPARISON_NEVER;
    case 0x201: return D3D11_COMPARISON_LESS;
    case 0x202: return D3D11_COMPARISON_EQUAL;
    case 0x203: return D3D11_COMPARISON_LESS_EQUAL;
    case 0x204: return D3D11_COMPARISON_GREATER;
    case 0x205: return D3D11_COMPARISON_NOT_EQUAL;
    case 0x206: return D3D11_COMPARISON_GREATER_EQUAL;
    default: return D3D11_COMPARISON_ALWAYS;
    }
}

static D3D11_STENCIL_OP map_sop(uint32_t o)
{
    switch (o) {
    case 0: return D3D11_STENCIL_OP_ZERO;
    case 0x1E01: return D3D11_STENCIL_OP_REPLACE;
    case 0x1E02: return D3D11_STENCIL_OP_INCR_SAT;
    case 0x1E03: return D3D11_STENCIL_OP_DECR_SAT;
    case 0x150A: return D3D11_STENCIL_OP_INVERT;
    case 0x8507: return D3D11_STENCIL_OP_INCR;
    case 0x8508: return D3D11_STENCIL_OP_DECR;
    default: return D3D11_STENCIL_OP_KEEP;
    }
}

#define MAX_STATE 256
typedef struct { uint32_t key[7]; ID3D11BlendState *bs; } BlendEntry;
typedef struct { uint32_t key[10]; ID3D11DepthStencilState *ds; } DsEntry;
typedef struct { uint32_t key[4]; ID3D11SamplerState *ss; } SampEntry;
static BlendEntry s_blend[MAX_STATE];
static int s_nblend;
static DsEntry s_ds[MAX_STATE];
static int s_nds;
static SampEntry s_samp[MAX_STATE];
static int s_nsamp;

static ID3D11BlendState *get_blend(const Nv2aRenderState *rs)
{
    const int ui_active=ui_capture_target(s_cur)!=NULL;
    const uint32_t motion=s_cur&&s_motion_draw_color==s_cur->va?s_motion_binding.outputs:0;
    uint32_t key[7] = { rs->blend_enable, rs->blend_src, rs->blend_dst, rs->blend_eq,
                        rs->color_mask, ui_active?(uint32_t)s_ui_capture_mode+1:0,motion };
    D3D11_BLEND_DESC bd;
    D3D11_RENDER_TARGET_BLEND_DESC *t = &bd.RenderTarget[0];
    UINT8 mask = 0;
    int i;

    for (i = 0; i < s_nblend; i++)
        if (!memcmp(s_blend[i].key, key, sizeof key))
            return s_blend[i].bs;
    if (s_nblend == MAX_STATE)
        return NULL;
    memset(&bd, 0, sizeof bd);
    t->BlendEnable = rs->blend_enable ? TRUE : FALSE;
    t->SrcBlend = map_blend(rs->blend_src);
    t->DestBlend = map_blend(rs->blend_dst);
    t->BlendOp = map_blend_op(rs->blend_eq);
    t->SrcBlendAlpha = alpha_only(t->SrcBlend);
    t->DestBlendAlpha = alpha_only(t->DestBlend);
    t->BlendOpAlpha = t->BlendOp;
    if (rs->color_mask & 0x00010000u) mask |= D3D11_COLOR_WRITE_ENABLE_RED;
    if (rs->color_mask & 0x00000100u) mask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
    if (rs->color_mask & 0x00000001u) mask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
    if (rs->color_mask & 0x01000000u) mask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
    t->RenderTargetWriteMask = mask;
    if(ui_active||motion) {
        /* Preserve the complete original RT0 descriptor and factor mapping.
         * Only RT3 uses scene-independent premultiplied source-over. */
        D3D11_RENDER_TARGET_BLEND_DESC *overlay=&bd.RenderTarget[3];
        for(i=1;i<8;i++) {bd.RenderTarget[i]=*t;bd.RenderTarget[i].RenderTargetWriteMask=0;}
        bd.IndependentBlendEnable=TRUE;overlay->BlendEnable=TRUE;
        overlay->SrcBlend=overlay->SrcBlendAlpha=D3D11_BLEND_ONE;
        overlay->DestBlend=overlay->DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        overlay->BlendOp=overlay->BlendOpAlpha=D3D11_BLEND_OP_ADD;
        overlay->RenderTargetWriteMask=ui_active&&s_ui_capture_mode!=BF_UI_BLEND_NO_RGB?D3D11_COLOR_WRITE_ENABLE_ALL:0;
        for(i=1;i<=5;i++)if(i!=3) {
            D3D11_RENDER_TARGET_BLEND_DESC *aux=&bd.RenderTarget[i];
            aux->BlendEnable=FALSE;aux->SrcBlend=aux->SrcBlendAlpha=D3D11_BLEND_ONE;
            aux->DestBlend=aux->DestBlendAlpha=D3D11_BLEND_ZERO;aux->BlendOp=aux->BlendOpAlpha=D3D11_BLEND_OP_ADD;
            aux->RenderTargetWriteMask=i==1?(motion&BF_MOTION_BIND_VECTOR?3:0):i==4?(motion&BF_MOTION_BIND_DEPTH?1:0):(motion&BF_MOTION_BIND_MASKS?1:0);
            if(i==2||i==5){aux->BlendEnable=TRUE;aux->DestBlend=aux->DestBlendAlpha=D3D11_BLEND_ONE;aux->BlendOp=aux->BlendOpAlpha=D3D11_BLEND_OP_MAX;}
        }
    }
    memcpy(s_blend[s_nblend].key, key, sizeof key);
    if(FAILED(ID3D11Device_CreateBlendState(s_dev, &bd, &s_blend[s_nblend].bs))&&(ui_active||motion)) {
        bf_motion_renderer_resource_failure(s_motion_renderer,s_cur->va,s_gen);motion_disable_draw();
        bf_ui_composition_invalidate(s_cur->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_TARGET);
        bind_output_views(s_cur,s_cur_depth);
        return get_blend(rs); /* Retain original blending if auxiliary state fails. */
    }
    return s_blend[s_nblend++].bs;
}

static ID3D11DepthStencilState *get_ds(const Nv2aRenderState *rs, int has_depth)
{
    uint32_t key[10] = { has_depth && rs->depth_test_enable, rs->depth_func,
                         has_depth && rs->depth_write, has_depth && rs->stencil_enable,
                         rs->stencil_func, rs->stencil_func_mask, rs->stencil_write_mask,
                         rs->stencil_fail, rs->stencil_zfail, rs->stencil_zpass };
    D3D11_DEPTH_STENCIL_DESC dd;
    int i;

    for (i = 0; i < s_nds; i++)
        if (!memcmp(s_ds[i].key, key, sizeof key))
            return s_ds[i].ds;
    if (s_nds == MAX_STATE)
        return NULL;
    memset(&dd, 0, sizeof dd);
    dd.DepthEnable = key[0] || key[2] ? TRUE : FALSE;
    dd.DepthFunc = key[0] ? map_cmp(rs->depth_func) : D3D11_COMPARISON_ALWAYS;
    dd.DepthWriteMask = key[2] ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.StencilEnable = key[3] ? TRUE : FALSE;
    dd.StencilReadMask = (UINT8)rs->stencil_func_mask;
    dd.StencilWriteMask = (UINT8)rs->stencil_write_mask;
    dd.FrontFace.StencilFunc = map_cmp(rs->stencil_func);
    dd.FrontFace.StencilFailOp = map_sop(rs->stencil_fail);
    dd.FrontFace.StencilDepthFailOp = map_sop(rs->stencil_zfail);
    dd.FrontFace.StencilPassOp = map_sop(rs->stencil_zpass);
    dd.BackFace = dd.FrontFace;
    memcpy(s_ds[s_nds].key, key, sizeof key);
    ID3D11Device_CreateDepthStencilState(s_dev, &dd, &s_ds[s_nds].ds);
    return s_ds[s_nds++].ds;
}

/* NV2A MIN modes 1/2: point/linear without mips; 3/4: point/linear with
 * nearest mip; 5/6: point/linear with linear mip interpolation. MAG is
 * independent. Retain the signed 13-bit 8-fractional-bit LOD bias. */
static ID3D11SamplerState *get_sampler(uint32_t addr_u, uint32_t addr_v, uint32_t filter)
{
    uint32_t mag = (filter >> 24) & 0xF, min = (filter >> 16) & 0xFF;
    int min_linear = min == 2 || min == 4 || min == 6;
    const unsigned anisotropy=bf_ui_texture_scale()>1&&min_linear&&mag==2&&min>=3&&min<=6?16u:1u;
    uint32_t key[4] = { addr_u, addr_v, filter, anisotropy };
    D3D11_SAMPLER_DESC sd;
    int i;

    for (i = 0; i < s_nsamp; i++)
        if (!memcmp(s_samp[i].key, key, sizeof key))
            return s_samp[i].ss;
    if (s_nsamp == MAX_STATE)
        return NULL;
    memset(&sd, 0, sizeof sd);
    sd.Filter = D3D11_ENCODE_BASIC_FILTER(min_linear?D3D11_FILTER_TYPE_LINEAR:D3D11_FILTER_TYPE_POINT,
        mag==2?D3D11_FILTER_TYPE_LINEAR:D3D11_FILTER_TYPE_POINT,
        min>=5?D3D11_FILTER_TYPE_LINEAR:D3D11_FILTER_TYPE_POINT,D3D11_FILTER_REDUCTION_TYPE_STANDARD);
    if(anisotropy>1)sd.Filter=D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy=anisotropy;
    sd.AddressU = addr_u == 1 ? D3D11_TEXTURE_ADDRESS_WRAP : addr_u == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR
                : D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = addr_v == 1 ? D3D11_TEXTURE_ADDRESS_WRAP : addr_v == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR
                : D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = min>=3 && min<=6 ? D3D11_FLOAT32_MAX : 0.0f;
    { int bias=(int)(filter&0x1FFF);if(bias&0x1000)bias-=0x2000;sd.MipLODBias=bias/256.0f; }
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    memcpy(s_samp[s_nsamp].key, key, sizeof key);
    ID3D11Device_CreateSamplerState(s_dev, &sd, &s_samp[s_nsamp].ss);
    return s_samp[s_nsamp++].ss;
}

/* The zeta format's largest value (SURFACE_FORMAT bits 4-7: 1 Z16, 2
 * Z24S8). The CPU path compares the title's screen z with the raw clear
 * value in this range and ignores SET_CLIP_MIN/MAX, so depth is z / zmax. */
static float zeta_max(void)
{
    return ((nv2a_backend_reg(0x0208) >> 4) & 0xF) == 1 ? 65535.0f : 16777215.0f;
}

static void depth_consts(const Nv2aRenderState *rs, float out[4])
{
    (void)rs;
    out[0] = 0.0f;
    out[1] = 1.0f / zeta_max();
    out[2] = 0.0f;              /* no vertex clamp: D3D clamps per pixel to 0..1 */
    out[3] = 0.0f;
}

static void blend_factor(const Nv2aRenderState *rs, float bf[4])
{
    bf[0] = ((rs->blend_color >> 16) & 0xFF) / 255.0f;
    bf[1] = ((rs->blend_color >> 8) & 0xFF) / 255.0f;
    bf[2] = (rs->blend_color & 0xFF) / 255.0f;
    bf[3] = ((rs->blend_color >> 24) & 0xFF) / 255.0f;
    if (rs->blend_src == 0x8003 || rs->blend_src == 0x8004 ||
        rs->blend_dst == 0x8003 || rs->blend_dst == 0x8004)
        bf[0] = bf[1] = bf[2] = bf[3];
}

static void set_alpha_test(const Nv2aRenderState *rs, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (rs->alpha_test_enable) {
        out[0] = (float)(rs->alpha_func >= 0x200 && rs->alpha_func <= 0x207
                         ? rs->alpha_func - 0x200 : 7);
        out[1] = (float)(rs->alpha_ref & 0xFF);
        out[2] = 1.0f;
    }
    /* One anchor for the complete original widget/string, ordered in FIFO. */
    out[3]=s_cur&&s_cur->wide_generation==s_gen&&s_cur->ui_generation==s_gen?(float)(2+s_ui_anchor):1.0f;
}

/* ---- vertex ring ----------------------------------------------------------- */

static ID3D11Buffer *s_vring;
static UINT s_vring_size, s_vring_pos;

/* The vertex ring, at least `bytes` large. */
static int ring_ensure(UINT bytes)
{
    D3D11_BUFFER_DESC bd;
    UINT want;

    if (s_vring && bytes <= s_vring_size)
        return 1;
    want = bytes * 4 > (32u << 20) ? bytes * 4 : (32u << 20);
    if (s_vring) ID3D11Buffer_Release(s_vring);
    s_vring = NULL;
    s_vring_size = 0;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = want;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_vring)))
        return 0;
    s_vring_size = want;
    s_vring_pos = want;                 /* the first map discards */
    return 1;
}

static UINT ring_push(const void *data, UINT bytes, UINT align)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT off;

    if (!bytes || !ring_ensure(bytes))
        return ~0u;
    off = (s_vring_pos + align - 1) / align * align;
    if (off > s_vring_size || s_vring_size - off < bytes) {
        mode = D3D11_MAP_WRITE_DISCARD;
        off = 0;
    }
    if (FAILED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_vring, 0, mode, 0, &m)))
        return ~0u;
    memcpy((uint8_t *)m.pData + off, data, bytes);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_vring, 0);
    s_vring_pos = off + bytes;
    return off;
}

static void unpack_argb(uint32_t c, float o[4])
{
    o[0] = ((c >> 16) & 0xFF) / 255.0f;
    o[1] = ((c >> 8) & 0xFF) / 255.0f;
    o[2] = (c & 0xFF) / 255.0f;
    o[3] = ((c >> 24) & 0xFF) / 255.0f;
}

static const char s_clear_hlsl[] =
"cbuffer C : register(b2) { float4 color; float4 depth; };\n"
"float4 vs_main(uint id : SV_VertexID) : SV_Position {\n"
"  float2 p = float2((id << 1) & 2, id & 2);\n"
"  return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, depth.x, 1.0);\n"
"}\n"
"float4 ps_main() : SV_Target { return color; }\n";

typedef struct {
    float color[4], depth[4];
} ClearConsts;

static ID3D11VertexShader *s_clear_vs;
static ID3D11PixelShader *s_clear_ps;
static ID3D11Buffer *s_clear_cb;
static ID3D11BlendState *s_clear_blend[16];
static ID3D11DepthStencilState *s_clear_ds[4];

static int query_suspend_clear(void);
static void query_resume_clear(int counting);

static void clear_error(const char *operation, HRESULT hr)
{
    fprintf(stderr, "[GPU-D3D11] %s failed 0x%08lX (device removed reason 0x%08lX);"
                    " cannot safely clear the surface\n",
            operation, (unsigned long)hr,
            (unsigned long)ID3D11Device_GetDeviceRemovedReason(s_dev));
    fflush(stderr);
    abort();
}

static void clear_resources(void)
{
    D3D11_BUFFER_DESC bd;
    ID3DBlob *vs = NULL, *ps = NULL;
    HRESULT hr;

    if (s_clear_vs)
        return;
    if (!compile(s_clear_hlsl, sizeof s_clear_hlsl - 1, "clear", "vs_main", "vs_5_0", &vs) ||
        !compile(s_clear_hlsl, sizeof s_clear_hlsl - 1, "clear", "ps_main", "ps_5_0", &ps))
        clear_error("compile rectangular clear shaders", E_FAIL);
    hr = ID3D11Device_CreateVertexShader(s_dev, ID3D10Blob_GetBufferPointer(vs),
                                         ID3D10Blob_GetBufferSize(vs), NULL, &s_clear_vs);
    if (FAILED(hr))
        clear_error("CreateVertexShader(clear)", hr);
    hr = ID3D11Device_CreatePixelShader(s_dev, ID3D10Blob_GetBufferPointer(ps),
                                        ID3D10Blob_GetBufferSize(ps), NULL, &s_clear_ps);
    if (FAILED(hr))
        clear_error("CreatePixelShader(clear)", hr);
    ID3D10Blob_Release(vs);
    ID3D10Blob_Release(ps);

    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = sizeof(ClearConsts);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_clear_cb);
    if (FAILED(hr))
        clear_error("CreateBuffer(clear constants)", hr);
}

static ID3D11BlendState *clear_blend(unsigned mask)
{
    D3D11_BLEND_DESC bd;
    D3D11_RENDER_TARGET_BLEND_DESC *target;
    HRESULT hr;

    if (s_clear_blend[mask])
        return s_clear_blend[mask];
    memset(&bd, 0, sizeof bd);
    target = &bd.RenderTarget[0];
    target->SrcBlend = D3D11_BLEND_ONE;
    target->DestBlend = D3D11_BLEND_ZERO;
    target->BlendOp = D3D11_BLEND_OP_ADD;
    target->SrcBlendAlpha = D3D11_BLEND_ONE;
    target->DestBlendAlpha = D3D11_BLEND_ZERO;
    target->BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target->RenderTargetWriteMask = (UINT8)mask;
    hr = ID3D11Device_CreateBlendState(s_dev, &bd, &s_clear_blend[mask]);
    if (FAILED(hr))
        clear_error("CreateBlendState(clear)", hr);
    return s_clear_blend[mask];
}

static ID3D11DepthStencilState *clear_depth_stencil(unsigned flags)
{
    D3D11_DEPTH_STENCIL_DESC dd;
    HRESULT hr;

    if (s_clear_ds[flags])
        return s_clear_ds[flags];
    memset(&dd, 0, sizeof dd);
    dd.DepthEnable = (flags & 1) != 0;
    dd.DepthWriteMask = (flags & 1) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    dd.StencilEnable = (flags & 2) != 0;
    dd.StencilReadMask = dd.StencilWriteMask = 0xFF;
    dd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dd.BackFace = dd.FrontFace;
    hr = ID3D11Device_CreateDepthStencilState(s_dev, &dd, &s_clear_ds[flags]);
    if (FAILED(hr))
        clear_error("CreateDepthStencilState(clear)", hr);
    return s_clear_ds[flags];
}

static void clear_draw(const D3D11_RECT *rect, uint32_t flags, uint32_t argb,
                       uint32_t zstencil, float z)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    ClearConsts c;
    ID3D11BlendState *blend;
    ID3D11DepthStencilState *ds;
    HRESULT hr;
    int counting;

    clear_resources();
    blend = clear_blend((flags >> 4) & 0xF);
    ds = clear_depth_stencil(flags & 3);
    memset(&c, 0, sizeof c);
    unpack_argb(argb, c.color);
    c.depth[0] = z;
    hr = ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_clear_cb, 0,
                                D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
        clear_error("Map(clear constants)", hr);
    memcpy(mapped.pData, &c, sizeof c);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_clear_cb, 0);

    ID3D11DeviceContext_IASetInputLayout(s_ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(s_ctx, s_clear_vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 2, 1, &s_clear_cb);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(s_ctx, s_clear_ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 2, 1, &s_clear_cb);
    ID3D11DeviceContext_RSSetState(s_ctx, s_rs);
    ID3D11DeviceContext_RSSetScissorRects(s_ctx, 1, rect);
    ID3D11DeviceContext_OMSetBlendState(s_ctx, blend, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, ds, (UINT8)zstencil);
    counting = query_suspend_clear();
    ID3D11DeviceContext_Draw(s_ctx, 3, 0);
    query_resume_clear(counting);
}

/* ---- the back end ---------------------------------------------------------- */

static void d3d_clear(const Nv2aSurface *s, const Nv2aRenderState *rs, uint32_t flags,
                      uint32_t argb, uint32_t zstencil)
{
    uint32_t bounds[4];
    D3D11_RECT rect;
    float c[4], z;
    int full_color, full_depth;
    uint32_t color_flags=flags&0xF0u;

    if (!begin(s, rs))
        return;
    if(s_cur->ui_generation==s_gen&&s_cur->ui_overlay) {
        bf_ui_composition_invalidate(s_cur->ui_overlay,BF_UI_CAPTURE_CLEAR_AFTER_BOUNDARY);
        bind_output_views(s_cur,s_cur_depth);
    }
    if (nv2a_backend_clear_rect(bounds)) {
        uint32_t sx = s->aa_sx > 1 ? 2 : 1, sy = s->aa_sy > 1 ? 2 : 1;
        uint32_t x0 = bounds[0] * sx, y0 = bounds[1] * sy;
        uint32_t x1 = bounds[2] * sx, y1 = bounds[3] * sy;
        rect.left = (LONG)(x0 < s_cur->w ? x0 : s_cur->w);
        rect.top = (LONG)(y0 < s_cur->h ? y0 : s_cur->h);
        rect.right = (LONG)(x1 < s_cur->w ? x1 : s_cur->w);
        rect.bottom = (LONG)(y1 < s_cur->h ? y1 : s_cur->h);
    } else {
        rect.left = rect.top = 0;
        rect.right = (LONG)s_cur->w;
        rect.bottom = (LONG)s_cur->h;
    }
    s_st.clears++;
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return;
    full_color = rect.left == 0 && rect.top == 0 &&
                 (uint32_t)rect.right == s_cur->w && (uint32_t)rect.bottom == s_cur->h;
    full_depth = s_cur_depth && rect.left == 0 && rect.top == 0 &&
                 (uint32_t)rect.right == s_cur_depth->w &&
                 (uint32_t)rect.bottom == s_cur_depth->h;
    if(flags&1) {
        BfMotionSource source;
        if(motion_source(s_cur,rs,&source))bf_motion_renderer_clear(s_motion_renderer,&source,full_depth&&(zstencil>>8)==0xFFFFFFu);
    }
    if(scene_tracking_enabled()) {
        if(s_cur_depth&&(flags&1)) {
            probe_scene_depth(s_cur_depth,NULL,BF_DEPTH_PROBE_BEFORE_CLEAR);
            invalidate_scene_depth(s_cur_depth->va,NULL);
        }
        if(color_flags==0xF0u&&full_color) {
            s_cur->temporal_frozen=0;
            if(s_cur->temporal_geometry_serial!=UINT64_MAX)++s_cur->temporal_geometry_serial;
            s_cur->temporal_identity.view=s_cur->temporal_identity.context=0;
            s_cur->scene_generation=s_gen;s_cur->scene_depth_draws=0;
            s_cur->scene_depth_va=s_cur_depth?s_cur_depth->va:0;
            // A color-only clear, separate resets or opaque-looking draws
            // cannot establish matching depth. Accept only a combined full
            // color/depth reset, then require actual subsequent world draws.
            s_cur->scene_tainted=!(full_depth&&(flags&1));
        }
    }
    z = zeta_max() == 65535.0f ? (zstencil & 0xFFFFu) / 65535.0f
                               : (zstencil >> 8) / 16777215.0f;
    if ((flags & 0xF0) == 0xF0 && full_color) {
        unpack_argb(argb, c);
        ID3D11DeviceContext_ClearRenderTargetView(s_ctx, s_cur->rtv, c);
        flags &= ~0xF0u;
    }
    if ((flags & 3) && full_depth) {
        UINT f = ((flags & 1) ? D3D11_CLEAR_DEPTH : 0) | ((flags & 2) ? D3D11_CLEAR_STENCIL : 0);
        ID3D11DeviceContext_ClearDepthStencilView(s_ctx, s_cur_depth->dsv, f, z,
                                                  (UINT8)(zstencil & 0xFF));
        if (flags & 1)
            s_cur_depth->initialized = 1;
        if(flags&2)s_cur_depth->stencil_initialized=1;
        s_cur_depth->gpu_dirty=1;
        flags &= ~3u;
    }
    if (!s_cur_depth) {
        if ((flags & 3) && rs && rs->zeta_va)
            fprintf(stderr, "[GPU-D3D11] cannot clear unavailable depth surface %08X\n",
                    rs->zeta_va);
        flags &= ~3u;
    }
    if (flags & 0xF3)
        clear_draw(&rect, flags, argb, zstencil, z);
    if(s_cur_depth&&(flags&3))s_cur_depth->gpu_dirty=1;
    if(color_flags) {
        if(color_flags==0xF0u&&full_color) {
            if(!mirror_full_color_clear(s_cur,argb))s_cur->gpu_dirty=1;
        } else s_cur->gpu_dirty=1;
    }
}

static int set_window_clip(const Nv2aSurface *s)
{
    static struct {
        uint32_t rect[NV2A_WINDOW_CLIP_COUNT][4];
        uint32_t info[4];
    } last;
    static int have_last;
    struct {
        uint32_t rect[NV2A_WINDOW_CLIP_COUNT][4];
        uint32_t info[4];
    } c;
    D3D11_MAPPED_SUBRESOURCE m;
    unsigned i;

    memset(&c, 0, sizeof c);
    if (s->window_clip_enabled && s->window_clip.axes == 3) {
        c.info[0] = s->window_clip.uniform ? 1 : NV2A_WINDOW_CLIP_COUNT;
        c.info[1] = s->window_clip.exclusive;
        c.info[2] = s->aa_sx > 1;
        c.info[3] = s->aa_sy > 1;
        for (i = 0; i < c.info[0]; i++) {
            uint32_t h = s->window_clip.horizontal[i];
            uint32_t v = s->window_clip.vertical[i];
            c.rect[i][0] = h & 0xFFFu;
            c.rect[i][1] = (h >> 16) & 0xFFFu;
            c.rect[i][2] = v & 0xFFFu;
            c.rect[i][3] = (v >> 16) & 0xFFFu;
            if(s_cur->wide_generation==s_gen&&s_cur->ui_generation==s_gen) {
                const uint32_t origin=s_ui_anchor*80u;
                c.rect[i][0]=origin+(c.rect[i][0]*3u)/4u;
                c.rect[i][1]=origin+((c.rect[i][1]+1u)*3u+3u)/4u-1u;
            }
        }
    }
    if (!have_last || memcmp(&last, &c, sizeof c)) {
        HRESULT hr = ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_window_cb, 0,
                                              D3D11_MAP_WRITE_DISCARD, 0, &m);
        if (FAILED(hr)) {
            fprintf(stderr, "[GPU-D3D11] window clip constants map failed 0x%08lX\n",
                    (unsigned long)hr);
            return 0;
        }
        memcpy(m.pData, &c, sizeof c);
        ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_window_cb, 0);
        memcpy(&last, &c, sizeof c);
        have_last = 1;
    }
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 1, 1, &s_window_cb);
    return 1;
}

static void d3d_draw(const Nv2aSurface *s, const Nv2aBatch *b)
{
    const Nv2aRenderState *rs;
    D3D11_MAPPED_SUBRESOURCE m;
    BaseConsts c;
    UINT stride = sizeof(Nv2aVertex), offs;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11SamplerState *ss = NULL;
    float bf[4], tw = 1.0f, th = 1.0f;

    if (!b || !b->count || !b->state || !begin(s, b->state) || !set_window_clip(s))
        return;
    rs = b->state;
    if (b->texture) {
        if (s_depth_texture && b->texture->color == 0x2E) {
            uint32_t mode = nv2a_backend_reg(0x1E70) & 0x1F;
            int cube = (nv2a_backend_reg(0x1B04) & 4) != 0;
            if (cube || mode != 1) {
                reject_depth_mode(mode, cube, 1);
                return;
            }
        }
        srv = get_texture(b->texture, 0, 0, &tw, &th);
        if (!srv && s_depth_texture && b->texture->color == 0x2E)
            return;
        ss = get_sampler(b->texture->addr_u, b->texture->addr_v, b->texture->filter);
    }
    memset(&c, 0, sizeof c);
    c.surf[0] = (float)s_cur->w; c.surf[1] = (float)s_cur->h;
    c.surf[2] = 1.0f / s_cur->w; c.surf[3] = 1.0f / s_cur->h;
    if (srv) {
        c.tex[0] = 1.0f / tw;
        c.tex[1] = 1.0f / th;
        c.tex[2] = 1.0f;
        if (b->texture->color == 0x2E && s_depth_texture)
            c.tex[3] = (float)((nv2a_backend_reg(0x1E6C) & 7) + 1);
    }
    set_alpha_test(rs, c.alpha);
    depth_consts(rs, c.depth);
    if (SUCCEEDED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_base_cb, 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, &c, sizeof c);
        ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_base_cb, 0);
    }
    offs = ring_push(b->vertices, b->count * stride, stride);
    if (offs == ~0u)
        return;
    blend_factor(rs, bf);
    ID3D11DeviceContext_IASetInputLayout(s_ctx, s_base_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &stride, &offs);
    ID3D11DeviceContext_VSSetShader(s_ctx, s_base_vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 0, 1, &s_base_cb);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(s_ctx, s_base_ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 0, 1, &s_base_cb);
    ID3D11DeviceContext_PSSetShaderResources(s_ctx, 0, 1, &srv);
    ID3D11DeviceContext_PSSetSamplers(s_ctx, 0, 1, &ss);
    ID3D11DeviceContext_RSSetState(s_ctx, s_rs);
    motion_prepare_draw(s_cur,rs,NULL);bind_output_views(s_cur,s_cur_depth);
    ID3D11DeviceContext_OMSetBlendState(s_ctx, get_blend(rs), bf, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, get_ds(rs, s_cur_depth != NULL),
                                               rs->stencil_ref & 0xFF);
    probe_depth_write(rs);
    ID3D11DeviceContext_Draw(s_ctx, b->count, 0);
    bf_ui_composition_draw(s_cur->ui_overlay,s_gen);
    bf_motion_trace_draw(s,rs,0,NULL,s_gen,s_cur&&s_cur->ui_generation==s_gen);
    s_cur->gpu_dirty=1;
    mark_depth_written(rs);
    s_st.draws++;
    s_st.tris += b->count / 3;
}

/* Without programmed combiners the CPU path draws stage 0 times diffuse; a
 * one-stage combiner computing T0 * D0 (or D0 alone) is the same thing. */
static void simple_combiner(const Nv2aXBatch *b, Nv2aCombiner *rc)
{
    memset(rc, 0, sizeof *rc);
    rc->control = 1;
    rc->stage_program = b->tex_valid[0] ? 1 : 0;
    /* A = T0 (reg 8) or 1 (reg 0 inverted), B = D0 (reg 4); out to R12
     * (spare0), which the final combiner absent reads as the result. */
    rc->color_icw[0] = (b->tex_valid[0] ? 0x08u : 0x20u) << 24 | 0x04u << 16;
    rc->alpha_icw[0] = (b->tex_valid[0] ? 0x18u : 0x30u) << 24 | 0x14u << 16;
    rc->color_ocw[0] = 0x0Cu << 4;
    rc->alpha_ocw[0] = 0x0Cu << 4;
}

/* The rasteriser state for a batch: no culling, unless the batch asks the
 * back end to cull as the CPU path would (draw_vsh with RECOMP_CULL_FACE).
 * NULL when both faces are culled. The executor's surface space has y
 * down, where D3D judges winding too; a positive area is clockwise. */
static ID3D11RasterizerState *batch_rs(const Nv2aXBatch *b)
{
    const Nv2aRenderState *rs = b->state;
    if (!b->cull || !rs->cull_enable)
        return s_rs;
    if (rs->cull_face == 0x408u)
        return NULL;
    return s_rs_cull[rs->cull_face != 0x404u][rs->front_face == 0x901u];
}

static GpuRcConsts s_xlast;         /* the last batch's constants, for vsh_readback */

/* Everything a transformed batch needs but its vertices: targets, combiner
 * shader, textures, samplers and output-merger state. 1 ready, 0 failed, -1
 * nothing to draw (both faces culled). */
static int xdraw_prepare(const Nv2aSurface *s, const Nv2aXBatch *b)
{
    const Nv2aRenderState *rs;
    const Nv2aCombiner *rc;
    Nv2aCombiner simple;
    GpuRcKey key;
    GpuRcConsts c;
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11PixelShader *ps;
    ID3D11RasterizerState *rst;
    ID3D11ShaderResourceView *srv[4] = { NULL, NULL, NULL, NULL };
    ID3D11SamplerState *ss[4] = { NULL, NULL, NULL, NULL };
    float bf[4];
    int st;

    if (!b || !b->state || !begin(s, b->state) || !set_window_clip(s))
        return 0;
    rs = b->state;
    rst = batch_rs(b);
    if (!rst)
        return -1;
    rc = b->rc;
    if (!rc) {
        simple_combiner(b, &simple);
        rc = &simple;
    }
    memset(&key, 0, sizeof key);
    memcpy(key.color_icw, rc->color_icw, sizeof key.color_icw);
    memcpy(key.alpha_icw, rc->alpha_icw, sizeof key.alpha_icw);
    memcpy(key.color_ocw, rc->color_ocw, sizeof key.color_ocw);
    memcpy(key.alpha_ocw, rc->alpha_ocw, sizeof key.alpha_ocw);
    key.final0 = rc->final0;
    key.final1 = rc->final1;
    key.control = rc->control;
    key.stage_program = rc->stage_program;
    key.clip_plane_mode = b->clip_plane_mode;

    memset(&c, 0, sizeof c);
    for (st = 0; st < 4; st++) {
        uint32_t mode = (rc->stage_program >> (st * 5)) & 0x1F;
        int cube = b->tex_cube[st] && mode == 3;
        float tw = 1.0f, th = 1.0f;
        if (s_depth_texture && b->tex_valid[st] && b->tex[st].color == 0x2E &&
            mode != 0 && (b->tex_cube[st] || (mode != 1 && mode != 2))) {
            reject_depth_mode(mode, b->tex_cube[st], 0);
            return 0;
        }
        if (!b->tex_valid[st] || mode < 1 || mode > 3)
            continue;
        srv[st] = get_texture(&b->tex[st], cube, st, &tw, &th);
        if (!srv[st]) {
            if (s_depth_texture && b->tex[st].color == 0x2E)
                return 0;
            continue;
        }
        key.tex_valid[st] = 1;
        key.tex_normalized[st] = b->tex_normalized[st];
        key.tex_cube[st] = (uint8_t)cube;
        if (s_depth_texture && b->tex[st].color == 0x2E) {
            key.tex_depth[st] = 1;
            key.shadow_func = nv2a_backend_reg(0x1E6C) & 7;
        }
        ss[st] = get_sampler(b->tex[st].addr_u, b->tex[st].addr_v, b->tex[st].filter);
        c.texinfo[st][0] = 1.0f / tw;
        c.texinfo[st][1] = 1.0f / th;
    }
    ps = get_rc_shader(&key);
    if (!ps)
        return 0;
    c.surf[0] = (float)s_cur->w; c.surf[1] = (float)s_cur->h;
    c.surf[2] = 1.0f / s_cur->w; c.surf[3] = 1.0f / s_cur->h;
    depth_consts(rs, c.depth);
    set_alpha_test(rs, c.alpha);
    c.fogc[0] = b->fogc[0]; c.fogc[1] = b->fogc[1]; c.fogc[2] = b->fogc[2];
    for (st = 0; st < 8; st++) {
        unpack_argb(rc->factor0[st], c.c0[st]);
        unpack_argb(rc->factor1[st], c.c1[st]);
    }
    unpack_argb(rc->final_c0, c.fc0);
    unpack_argb(rc->final_c1, c.fc1);
    if (SUCCEEDED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_xcb, 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, &c, sizeof c);
        ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_xcb, 0);
    }
    s_xlast = c;
    blend_factor(rs, bf);
    ID3D11DeviceContext_PSSetShader(s_ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 0, 1, &s_xcb);
    ID3D11DeviceContext_PSSetShaderResources(s_ctx, 0, 4, srv);
    ID3D11DeviceContext_PSSetSamplers(s_ctx, 0, 4, ss);
    ID3D11DeviceContext_RSSetState(s_ctx, rst);
    ID3D11DeviceContext_OMSetBlendState(s_ctx, get_blend(rs), bf, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, get_ds(rs, s_cur_depth != NULL),
                                               rs->stencil_ref & 0xFF);
    return 1;
}

static void d3d_xdraw(const Nv2aSurface *s, const Nv2aXBatch *b)
{
    UINT stride = sizeof(Nv2aXVertex), offs;

    if (!b || !b->count || xdraw_prepare(s, b) <= 0)
        return;
    offs = ring_push(b->vertices, b->count * stride, stride);
    if (offs == ~0u)
        return;
    ID3D11DeviceContext_IASetInputLayout(s_ctx, s_xlayout);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &stride, &offs);
    ID3D11DeviceContext_VSSetShader(s_ctx, s_xvs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 0, 1, &s_xcb);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    {
        float bf[4];motion_prepare_draw(s_cur,b->state,NULL);bind_output_views(s_cur,s_cur_depth);
        blend_factor(b->state,bf);ID3D11DeviceContext_OMSetBlendState(s_ctx,get_blend(b->state),bf,0xFFFFFFFFu);
    }
    probe_depth_write(b->state);
    ID3D11DeviceContext_Draw(s_ctx, b->count, 0);
    bf_ui_composition_draw(s_cur->ui_overlay,s_gen);
    bf_motion_trace_draw(s,b->state,1,NULL,s_gen,s_cur&&s_cur->ui_generation==s_gen);
    s_cur->gpu_dirty=1;
    mark_depth_written(b->state);
    s_st.draws++;
    s_st.xdraws++;
    s_st.tris += b->count / 3;
}

/* ---- vertex programs on the GPU (draw_vsh) ----------------------------------- */

#define MAX_VSH 1024
#define VSH_MAX_INSTR 136
#define VSH_RAW_IL 8
typedef struct {
    uint64_t key;
    D3D11_INPUT_ELEMENT_DESC el[16];
    ID3D11InputLayout *il;
} RawIl;
typedef struct {
    uint64_t hash;
    uint32_t n, mask, nin;
    uint8_t conv[16];                   /* the shader's conversions; 0 = floats */
    RawIl raw[VSH_RAW_IL];              /* layouts for raw streams */
    uint32_t raw_next;
    uint32_t words[VSH_MAX_INSTR][4];
    char *src;                          /* worker input */
    size_t len;
    ID3DBlob *blob;                     /* worker output */
    volatile LONG state;                /* 0 queued, 1 compiled, 2 ready, -1 failed */
    ID3D11VertexShader *vs;
    ID3D11InputLayout *il;              /* ATTR0..nin-1 as float4s in one stream */
    void *code;                         /* bytecode, for layouts and readback */
    SIZE_T code_size;
    ID3D11GeometryShader *so;           /* stream-output twin, for readback */
} Vsh;

static Vsh *s_vsh;
static volatile LONG s_nvsh, s_vsh_compiles, s_vsh_failures;
static HANDLE s_vsh_wake, s_vsh_thread;
static ID3D11Buffer *s_kcb, *s_iring;
static UINT s_iring_size, s_iring_pos;
static GpuVshConsts s_kshadow;
static int s_kvalid;
static Vsh *s_vlast;                    /* the last draw_vsh, for vsh_readback */
static UINT s_vlast_voff, s_vlast_stride, s_vlast_nverts, s_vlast_ns;
static UINT s_vlast_strides[NV2A_XVS_MAX_STREAMS], s_vlast_offs[NV2A_XVS_MAX_STREAMS];
static int s_vlast_raw;
static ID3D11InputLayout *s_vlast_il;
static uint32_t s_vsh_flags;            /* GPU_VSH_* for every shader */
static int s_vsh_sync;                  /* RECOMP_GPU_VSH_SYNC */

static DWORD WINAPI vsh_worker(LPVOID arg)
{
    (void)arg;
    for (;;) {
        LONG i, n = s_nvsh;
        int did = 0;
        for (i = 0; i < n; i++) {
            Vsh *v = &s_vsh[i];
            ID3DBlob *blob = NULL, *err = NULL;
            if (v->state != 0)
                continue;
            if (FAILED(D3DCompile(v->src, v->len, "vsh", NULL, NULL, "vs_main", "vs_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS,
                                  0, &blob, &err))) {
                if (InterlockedIncrement(&s_vsh_failures) <= 3)
                    fprintf(stderr, "[GPU-D3D11] vertex shader %ld failed: %s\n", (long)i,
                            err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
                if (err)
                    ID3D10Blob_Release(err);
                InterlockedExchange(&v->state, -1);
            } else {
                if (err)
                    ID3D10Blob_Release(err);
                v->blob = blob;
                InterlockedIncrement(&s_vsh_compiles);
                InterlockedExchange(&v->state, 1);
            }
            did = 1;
        }
        if (!did)
            WaitForSingleObject(s_vsh_wake, INFINITE);
    }
}

static uint64_t prog_hash(const uint32_t (*p)[4], uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 4; j++)
            h = (h ^ p[i][j]) * 1099511628211ull;
    return h ^ n;
}

/* On the NV2A thread: the compiled shader to a vertex shader and its float
 * input layout. */
static void vsh_finish(Vsh *v)
{
    static const uint8_t no_conv[16];
    D3D11_INPUT_ELEMENT_DESC il[16];
    const void *code = ID3D10Blob_GetBufferPointer(v->blob);
    SIZE_T size = ID3D10Blob_GetBufferSize(v->blob);
    uint32_t k;
    int ok;

    ok = SUCCEEDED(ID3D11Device_CreateVertexShader(s_dev, code, size, NULL, &v->vs));
    if (ok && v->nin && !memcmp(v->conv, no_conv, sizeof no_conv)) {
        for (k = 0; k < v->nin; k++) {
            il[k].SemanticName = "ATTR";
            il[k].SemanticIndex = k;
            il[k].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            il[k].InputSlot = 0;
            il[k].AlignedByteOffset = 16 * k;
            il[k].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            il[k].InstanceDataStepRate = 0;
        }
        ok = SUCCEEDED(ID3D11Device_CreateInputLayout(s_dev, il, v->nin, code, size, &v->il));
    }
    if (ok && (v->code = malloc(size)) != NULL) {
        memcpy(v->code, code, size);
        v->code_size = size;
    }
    ID3D10Blob_Release(v->blob);
    v->blob = NULL;
    free(v->src);
    v->src = NULL;
    v->state = ok ? 2 : -1;
}

static void vsh_conv_key(const Nv2aXVsBatch *b, uint8_t conv[16])
{
    uint32_t k;
    memset(conv, 0, 16);
    if (!b->nstreams)
        return;
    for (k = 0; k < b->ninputs && k < 16; k++) {
        uint8_t c = b->in[k].conv;
        switch (c >> 4) {
        case NV2A_XVS_COLOR: case NV2A_XVS_UBYTE: case NV2A_XVS_SHORT: case NV2A_XVS_CMP:
            conv[k] = c;
            break;
        default:
            break;
        }
    }
}

/* The ready shader for a batch's program and input formats, or NULL while it
 * compiles (the executor then transforms the batch on the CPU). */
static Vsh *get_vsh(const Nv2aXVsBatch *b)
{
    static char src[96 * 1024];
    static LONG last = -1;
    uint8_t conv[16];
    uint64_t h = prog_hash(b->program, b->ninstr);
    LONG i, n = s_nvsh;
    Vsh *v;

    vsh_conv_key(b, conv);
    for (i = -1; i < n; i++) {
        LONG j = i < 0 ? last : i;
        if (j < 0 || (i >= 0 && i == last))
            continue;
        v = &s_vsh[j];
        if (v->hash != h || v->n != b->ninstr || memcmp(v->conv, conv, sizeof conv)
            || memcmp(v->words, b->program, b->ninstr * sizeof v->words[0]))
            continue;
        last = j;
        if (v->state == 1)
            vsh_finish(v);
        return v->state == 2 ? v : NULL;
    }
    if (n == MAX_VSH || b->ninstr > VSH_MAX_INSTR)
        return NULL;
    if (!s_vsh && !(s_vsh = (Vsh *)calloc(MAX_VSH, sizeof *s_vsh)))
        return NULL;
    if (!s_vsh_thread) {
        s_vsh_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
        s_vsh_thread = CreateThread(NULL, 0, vsh_worker, NULL, 0, NULL);
        if (!s_vsh_thread)
            return NULL;
    }
    v = &s_vsh[n];
    memset(v, 0, sizeof *v);
    v->hash = h;
    v->n = b->ninstr;
    v->mask = b->input_mask;
    v->nin = b->ninputs;
    memcpy(v->conv, conv, sizeof conv);
    memcpy(v->words, b->program, b->ninstr * sizeof v->words[0]);
    v->len = gpu_vsh_hlsl(b->program, b->ninstr, b->input_mask, conv, s_vsh_flags,
                          src, sizeof src);
    if (!v->len || !(v->src = (char *)malloc(v->len + 1))) {
        v->state = -1;
    } else {
        memcpy(v->src, src, v->len + 1);
    }
    InterlockedExchange(&s_nvsh, n + 1);
    SetEvent(s_vsh_wake);
    if (s_vsh_sync) {
        /* RECOMP_GPU_VSH_SYNC=1 (tests): wait for the compile rather than
         * handing this batch to the CPU. */
        while (v->state == 0)
            Sleep(1);
        if (v->state == 1)
            vsh_finish(v);
        return v->state == 2 ? v : NULL;
    }
    return NULL;
}

static ID3D11GeometryShader *vsh_filter_gs(void)
{
    static ID3D11GeometryShader *gs;
    static int tried;
    ID3DBlob *blob = NULL;

    if (tried)
        return gs;
    tried = 1;
    if (!compile(gpu_vsh_gs_hlsl, strlen(gpu_vsh_gs_hlsl), "vsh_gs", "gs_main", "gs_5_0", &blob))
        return NULL;
    if (FAILED(ID3D11Device_CreateGeometryShader(s_dev, ID3D10Blob_GetBufferPointer(blob),
                                                ID3D10Blob_GetBufferSize(blob), NULL, &gs)))
        gs = NULL;
    ID3D10Blob_Release(blob);
    return gs;
}

/* fetch_attr's integer conversions as a table: x/1023 at x + 1024 for x in
 * -1024..1023, z/511 at z + 2560 for z in -512..511, c/255 at c + 3072. */
static ID3D11ShaderResourceView *vfetch_table(void)
{
    static ID3D11ShaderResourceView *srv;
    static int tried;
    static float t[3328];
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA sd;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd;
    ID3D11Buffer *buf = NULL;
    int i;

    if (tried)
        return srv;
    tried = 1;
    for (i = -1024; i < 1024; i++)
        t[i + 1024] = (float)i / 1023.0f;
    for (i = -512; i < 512; i++)
        t[i + 2560] = (float)i / 511.0f;
    for (i = 0; i < 256; i++)
        t[i + 3072] = (float)i / 255.0f;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = sizeof t;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    memset(&sd, 0, sizeof sd);
    sd.pSysMem = t;
    if (FAILED(ID3D11Device_CreateBuffer(s_dev, &bd, &sd, &buf)))
        return NULL;
    memset(&vd, 0, sizeof vd);
    vd.Format = DXGI_FORMAT_R32_FLOAT;
    vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    vd.Buffer.FirstElement = 0;
    vd.Buffer.NumElements = 3328;
    if (FAILED(ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)buf, &vd, &srv)))
        srv = NULL;
    ID3D11Buffer_Release(buf);
    return srv;
}

static DXGI_FORMAT vfetch_format(uint8_t conv)
{
    uint32_t s = conv & 15u;
    switch (conv >> 4) {
    case NV2A_XVS_FLOAT:
        return s == 1 ? DXGI_FORMAT_R32_FLOAT : s == 2 ? DXGI_FORMAT_R32G32_FLOAT
             : s == 3 ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
    case NV2A_XVS_CONST: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case NV2A_XVS_COLOR: return DXGI_FORMAT_R8G8B8A8_UINT;
    case NV2A_XVS_UBYTE:
        return s == 1 ? DXGI_FORMAT_R8_UINT : s == 2 ? DXGI_FORMAT_R8G8_UINT : DXGI_FORMAT_R8G8B8A8_UINT;
    case NV2A_XVS_SHORT:
        return s == 1 ? DXGI_FORMAT_R16_SINT : s == 2 ? DXGI_FORMAT_R16G16_SINT
                                                      : DXGI_FORMAT_R16G16B16A16_SINT;
    case NV2A_XVS_CMP:   return DXGI_FORMAT_R32_UINT;
    default:             return DXGI_FORMAT_UNKNOWN;
    }
}

static ID3D11InputLayout *vfetch_layout(Vsh *v, const Nv2aXVsBatch *b)
{
    D3D11_INPUT_ELEMENT_DESC el[16];
    uint64_t key = 1469598103934665603ull;
    uint32_t k, i;
    RawIl *e;

    if (!v->code || !v->nin || v->nin > 16)
        return NULL;
    memset(el, 0, sizeof el);
    for (k = 0; k < v->nin; k++) {
        const Nv2aXVsInput *in = &b->in[k];
        el[k].SemanticName = "ATTR";
        el[k].SemanticIndex = k;
        el[k].Format = vfetch_format(in->conv);
        el[k].InputSlot = in->stream;
        el[k].AlignedByteOffset = in->offset;
        el[k].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        if (el[k].Format == DXGI_FORMAT_UNKNOWN)
            return NULL;
        key = (key ^ ((uint64_t)el[k].Format | (uint64_t)in->stream << 8
                      | (uint64_t)in->offset << 16)) * 1099511628211ull;
    }
    for (i = 0; i < VSH_RAW_IL; i++) {
        e = &v->raw[i];
        if (e->il && e->key == key && !memcmp(e->el, el, v->nin * sizeof el[0]))
            return e->il;
    }
    e = &v->raw[v->raw_next++ % VSH_RAW_IL];
    if (e->il)
        ID3D11InputLayout_Release(e->il);
    e->il = NULL;
    if (FAILED(ID3D11Device_CreateInputLayout(s_dev, el, v->nin, v->code, v->code_size, &e->il)))
        return NULL;
    e->key = key;
    memcpy(e->el, el, sizeof el);
    return e->il;
}

static int d3d_vsh_raw_ok(void)
{
    return 1;
}

/* Indices: a second dynamic ring, mapped as the vertex ring is. */
static UINT index_push(const uint32_t *data, UINT count)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT bytes = count * 4u, off;

    if (!bytes)
        return ~0u;
    if (!s_iring || bytes > s_iring_size) {
        D3D11_BUFFER_DESC bd;
        UINT want = bytes * 4 > (16u << 20) ? bytes * 4 : (16u << 20);
        if (s_iring) ID3D11Buffer_Release(s_iring);
        s_iring = NULL;
        s_iring_size = 0;
        memset(&bd, 0, sizeof bd);
        bd.ByteWidth = want;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_iring)))
            return ~0u;
        s_iring_size = want;
        s_iring_pos = want;
    }
    off = (s_iring_pos + 3u) & ~3u;
    if (off > s_iring_size || s_iring_size - off < bytes) {
        mode = D3D11_MAP_WRITE_DISCARD;
        off = 0;
    }
    if (FAILED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_iring, 0, mode, 0, &m)))
        return ~0u;
    memcpy((uint8_t *)m.pData + off, data, bytes);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_iring, 0);
    s_iring_pos = off + bytes;
    return off;
}

/* Several streams into the vertex ring at once, 16-byte aligned each. */
static int vertex_streams_push(const Nv2aXVsBatch *b)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT pos[NV2A_XVS_MAX_STREAMS], total = 0, base, k;

    for (k = 0; k < b->nstreams; k++) {
        total = (total + 15u) & ~15u;
        pos[k] = total;
        total += b->streams[k].bytes;
    }
    if (!ring_ensure(total))
        return 0;
    base = (s_vring_pos + 15u) & ~15u;
    if (base > s_vring_size || s_vring_size - base < total) {
        mode = D3D11_MAP_WRITE_DISCARD;
        base = 0;
    }
    if (FAILED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_vring, 0, mode, 0, &m)))
        return 0;
    for (k = 0; k < b->nstreams; k++) {
        memcpy((uint8_t *)m.pData + base + pos[k], b->streams[k].data, b->streams[k].bytes);
        s_vlast_strides[k] = b->streams[k].stride;
        s_vlast_offs[k] = base + pos[k];
    }
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_vring, 0);
    s_vring_pos = base + total;
    return 1;
}

static int d3d_xdraw_vsh(const Nv2aSurface *s, const Nv2aXVsBatch *b)
{
    Vsh *v;
    ID3D11Buffer *cbs[2];
    UINT voff = 0, ioff, stride;
    ID3D11InputLayout *il;
    int r;

    if (!b || !b->nindices || !ensure_device())
        return 0;
    if (!(v = get_vsh(b))) {
        s_st.vfall++;
        return 0;
    }
    r = xdraw_prepare(s, &b->xb);
    if (r < 0)
        return 1;               /* both faces culled: nothing to draw */
    if (r == 0)
        return 0;
    if (!s_kcb) {
        D3D11_BUFFER_DESC bd;
        memset(&bd, 0, sizeof bd);
        bd.ByteWidth = (sizeof(GpuVshConsts) + 15) & ~15u;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_kcb)))
            return 0;
    }
    {
        /* The constants: skipped when neither the constant file (by its
         * generation) nor the per-batch values changed. */
        static GpuVshConsts k;
        static uint32_t kgen;
        static int kgen_ok;
        float vsinfo[4], foginfo[4];
        vsinfo[0] = b->aa[0]; vsinfo[1] = b->aa[1];
        vsinfo[2] = b->xb.near_clip ? 1.0f : 0.0f; vsinfo[3] = b->xb.near_z;
        foginfo[0] = b->fog_enable ? 1.0f : 0.0f;
        foginfo[1] = (float)b->fog_mode;
        foginfo[2] = b->fog_param[0]; foginfo[3] = b->fog_param[1];
        if (s_kvalid && kgen_ok && b->consts_gen == kgen
                && !memcmp(vsinfo, s_kshadow.vsinfo, sizeof vsinfo)
                && !memcmp(foginfo, s_kshadow.foginfo, sizeof foginfo)) {
            s_st.kskip++;
        } else {
            memcpy(k.k, b->consts, sizeof k.k);
            memcpy(k.vsinfo, vsinfo, sizeof vsinfo);
            memcpy(k.foginfo, foginfo, sizeof foginfo);
            if (!s_kvalid || memcmp(&k, &s_kshadow, sizeof k)) {
                D3D11_MAPPED_SUBRESOURCE m;
                if (FAILED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_kcb, 0,
                                                   D3D11_MAP_WRITE_DISCARD, 0, &m)))
                    return 0;
                memcpy(m.pData, &k, sizeof k);
                ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_kcb, 0);
                s_kshadow = k;
                s_kvalid = 1;
                s_st.kup++;
            }
            kgen = b->consts_gen;
            kgen_ok = 1;
        }
    }
    stride = 16u * v->nin;
    il = v->il;
    if (b->nstreams && v->nin) {
        ID3D11Buffer *bufs[NV2A_XVS_MAX_STREAMS];
        static int conv_bound;
        UINT k;
        if (b->nstreams > NV2A_XVS_MAX_STREAMS || !(il = vfetch_layout(v, b)))
            return 0;
        if (!vertex_streams_push(b))
            return 0;
        for (k = 0; k < b->nstreams; k++)
            bufs[k] = s_vring;
        if (!conv_bound) {
            ID3D11ShaderResourceView *t = vfetch_table();
            if (!t)
                return 0;
            ID3D11DeviceContext_VSSetShaderResources(s_ctx, 0, 1, &t);
            conv_bound = 1;
        }
        ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, b->nstreams, bufs, s_vlast_strides,
                                               s_vlast_offs);
        s_vlast_ns = b->nstreams;
        s_vlast_il = il;
        s_vlast_raw = 1;
        s_st.vraw++;
    } else {
        if (v->nin) {
            voff = ring_push(b->inputs, b->nverts * stride, stride);
            if (voff == ~0u)
                return 0;
            ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &stride, &voff);
        }
        s_vlast_raw = 0;
    }
    ioff = index_push(b->indices, b->nindices);
    if (ioff == ~0u)
        return 0;
    cbs[0] = s_xcb;
    cbs[1] = s_kcb;
    ID3D11DeviceContext_IASetInputLayout(s_ctx, il);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetIndexBuffer(s_ctx, s_iring, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetShader(s_ctx, v->vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 0, 2, cbs);
    ID3D11DeviceContext_GSSetShader(s_ctx, vsh_filter_gs(), NULL, 0);
    {
        float bf[4];motion_prepare_draw(s_cur,b->xb.state,b);bind_output_views(s_cur,s_cur_depth);
        blend_factor(b->xb.state,bf);ID3D11DeviceContext_OMSetBlendState(s_ctx,get_blend(b->xb.state),bf,0xFFFFFFFFu);
    }
    probe_depth_write(b->xb.state);
    ID3D11DeviceContext_DrawIndexed(s_ctx, b->nindices, ioff / 4u, 0);
    if(s_motion_binding.tracked)bf_motion_renderer_capture(s_motion_renderer,v->code,v->code_size,b->nverts);
    bf_ui_composition_draw(s_cur->ui_overlay,s_gen);
    bf_motion_trace_draw(s,b->xb.state,2,b,s_gen,s_cur&&s_cur->ui_generation==s_gen);
    s_cur->gpu_dirty=1;
    mark_depth_written(b->xb.state);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    s_vlast = v;
    s_vlast_voff = voff;
    s_vlast_stride = stride;
    s_vlast_nverts = b->nverts;
    s_st.draws++;
    s_st.vdraws++;
    s_st.tris += b->nindices / 3;
    return 1;
}

/* RECOMP_GPU_VSH_VERIFY: the last draw_vsh batch's vertices through its
 * shader again, captured with stream output, mapped back to the executor's
 * terms (surface pixels and the title's z). */
static int d3d_vsh_readback(Nv2aXVsOut *out, uint32_t n)
{
    static ID3D11Buffer *sob, *stg;
    static UINT so_cap;
    static const D3D11_SO_DECLARATION_ENTRY decl[] = {
        { 0, "TEXCOORD", 7, 0, 4, 0 }, /* actual unjittered emitted clip, before pixel sampling offset */
        { 0, "COLOR", 0, 0, 4, 0 },
        { 0, "COLOR", 1, 0, 4, 0 },
        { 0, "FOG", 0, 0, 1, 0 },
        { 0, "SV_ClipDistance", 0, 0, 2, 0 },
        { 0, NULL, 0, 0, 1, 0 },                  /* pad */
        { 0, "TEXCOORD", 0, 0, 4, 0 },
        { 0, "TEXCOORD", 1, 0, 4, 0 },
        { 0, "TEXCOORD", 2, 0, 4, 0 },
        { 0, "TEXCOORD", 3, 0, 4, 0 },
    };
    UINT stride = sizeof(Nv2aXVsOut), zero = 0, need = n * (UINT)sizeof(Nv2aXVsOut), i;
    Vsh *v = s_vlast;
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11Buffer *nullb = NULL;

    if (!v || !v->code || n != s_vlast_nverts || !n)
        return 0;
    if (!v->so && FAILED(ID3D11Device_CreateGeometryShaderWithStreamOutput(
                    s_dev, v->code, v->code_size, decl, 10, &stride, 1,
                    D3D11_SO_NO_RASTERIZED_STREAM, NULL, &v->so))) {
        v->so = NULL;
        return 0;
    }
    if (need > so_cap) {
        D3D11_BUFFER_DESC bd;
        if (sob) ID3D11Buffer_Release(sob);
        if (stg) ID3D11Buffer_Release(stg);
        sob = stg = NULL;
        so_cap = 0;
        memset(&bd, 0, sizeof bd);
        bd.ByteWidth = need;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_STREAM_OUTPUT;
        if (FAILED(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &sob)))
            return 0;
        bd.Usage = D3D11_USAGE_STAGING;
        bd.BindFlags = 0;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &stg)))
            return 0;
        so_cap = need;
    }
    if (s_vlast_raw) {
        ID3D11Buffer *bufs[NV2A_XVS_MAX_STREAMS];
        for (i = 0; i < s_vlast_ns; i++)
            bufs[i] = s_vring;
        ID3D11DeviceContext_IASetInputLayout(s_ctx, s_vlast_il);
        ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, s_vlast_ns, bufs, s_vlast_strides,
                                               s_vlast_offs);
    } else {
        ID3D11DeviceContext_IASetInputLayout(s_ctx, v->il);
        if (v->nin)
            ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &s_vlast_stride,
                                                   &s_vlast_voff);
    }
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext_VSSetShader(s_ctx, v->vs, NULL, 0);
    ID3D11DeviceContext_GSSetShader(s_ctx, v->so, NULL, 0);
    ID3D11DeviceContext_SOSetTargets(s_ctx, 1, &sob, &zero);
    ID3D11DeviceContext_Draw(s_ctx, n, 0);
    ID3D11DeviceContext_SOSetTargets(s_ctx, 1, &nullb, &zero);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)stg, (ID3D11Resource *)sob);
    if (FAILED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)stg, 0, D3D11_MAP_READ, 0, &m)))
        return 0;
    memcpy(out, m.pData, need);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)stg, 0);
    for (i = 0; i < n; i++) {
        float *p = out[i].pos, w = p[3];
        if (w != 0.0f) {
            float zd = p[2] / w;
            const float mode=s_xlast.alpha[3],scale=mode>=2&&mode<=4?.75f:mode>0?mode:1.0f;
            const float shift=mode>=2&&mode<=4?(mode-3)*.25f:0;
            p[0] = ((p[0] / w-shift) / scale + 1.0f) * 0.5f * s_xlast.surf[0];
            p[1] = (1.0f - p[1] / w) * 0.5f * s_xlast.surf[1];
            p[2] = s_xlast.depth[1] != 0.0f ? zd / s_xlast.depth[1] + s_xlast.depth[0] : zd;
        }
    }
    return 1;
}

/* ---- visibility tests -------------------------------------------------------- */

#define MAX_QUERIES 64
#define MAX_REPORTS 1024
static ID3D11Query *s_q_pool[MAX_QUERIES];
static int s_q_free;                    /* pool entries not in use */
static ID3D11Query *s_q_pending[MAX_QUERIES];
static int s_q_npending;
static ID3D11Query *s_q_active;
static uint64_t s_q_total;
static ID3D11Query *s_event_pool[MAX_REPORTS];
static int s_event_free;

static void query_error(const char *operation, HRESULT hr)
{
    fprintf(stderr, "[GPU-D3D11] %s failed 0x%08lX (device removed reason 0x%08lX);"
                    " cannot safely complete visibility reports\n",
            operation, (unsigned long)hr,
            (unsigned long)ID3D11Device_GetDeviceRemovedReason(s_dev));
    fflush(stderr);
    abort();
}

static ID3D11Query *query_create(D3D11_QUERY type)
{
    HRESULT hr;
    D3D11_QUERY_DESC qd;
    ID3D11Query *q = NULL;
    qd.Query = type;
    qd.MiscFlags = 0;
    hr = ID3D11Device_CreateQuery(s_dev, &qd, &q);
    if (FAILED(hr))
        query_error("CreateQuery", hr);
    return q;
}

static ID3D11Query *query_get(void)
{
    return s_q_free ? s_q_pool[--s_q_free] : query_create(D3D11_QUERY_OCCLUSION);
}

static void query_put(ID3D11Query *q)
{
    if (s_q_free < MAX_QUERIES)
        s_q_pool[s_q_free++] = q;
    else
        ID3D11Query_Release(q);
}

static ID3D11Query *event_get(void)
{
    return s_event_free ? s_event_pool[--s_event_free] : query_create(D3D11_QUERY_EVENT);
}

static void event_put(ID3D11Query *q)
{
    if (s_event_free < MAX_REPORTS)
        s_event_pool[s_event_free++] = q;
    else
        ID3D11Query_Release(q);
}

/* Wait for every ended query and add its count. */
static void query_drain(void)
{
    int i;
    double t0 = qpc_ms();
    for (i = 0; i < s_q_npending; i++) {
        UINT64 n = 0;
        HRESULT hr;
        do {
            hr = ID3D11DeviceContext_GetData(s_ctx, (ID3D11Asynchronous *)s_q_pending[i],
                                            &n, sizeof n, 0);
            if (hr == S_FALSE)
                SwitchToThread();
        } while (hr == S_FALSE);
        if (hr != S_OK)
            query_error("GetData(occlusion)", hr);
        s_q_total += n;
        query_put(s_q_pending[i]);
    }
    s_q_npending = 0;
    s_q_wait_ms += qpc_ms() - t0;
}

static void query_end(void)
{
    if (!s_q_active)
        return;
    ID3D11DeviceContext_End(s_ctx, (ID3D11Asynchronous *)s_q_active);
    if (s_q_npending == MAX_QUERIES)
        query_drain();
    s_q_pending[s_q_npending++] = s_q_active;
    s_q_active = NULL;
}

static int query_suspend_clear(void)
{
    int counting = s_q_active != NULL;
    if (counting)
        query_end();
    return counting;
}

static void query_resume_clear(int counting)
{
    if (counting) {
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    }
}

/* RECOMP_GPU_ZPASS=async: a report is written into guest memory when its
 * queries complete, polled from the NV2A thread's loop, instead of waiting
 * for the GPU at GET_REPORT. The title sees the result later in time, as it
 * would from the console's GPU, and D3D's own polling waits for it.
 * RECOMP_GPU_ZPASS=sync retains the blocking diagnostic reference. */
typedef struct {
    uint32_t va;
    uint64_t stamp;
    uint64_t prefix;
    int reset;                          /* a clear came before these queries */
    int nq;
    ID3D11Query *event;
    ID3D11Query *q[MAX_QUERIES];
} Report;
static Report *s_rep;                   /* ring of MAX_REPORTS */
static uint32_t s_rep_head, s_rep_tail;
static uint64_t s_rep_running;          /* the count since the last clear, resolved */
static int s_rep_reset_next;
static int s_zpass_async;
static double s_rep_last_flush;

static void report_write(const Report *r, uint64_t count)
{
    uint8_t *at = GUEST(r->va);
    uint32_t c = count > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)count, done = 0;
    memcpy(at, &r->stamp, 8);
    memcpy(at + 8, &c, 4);
    MemoryBarrier();
    memcpy(at + 12, &done, 4);
    s_rep_written++;
}

static void report_pending(void)
{
    double now = qpc_ms();
    if (now - s_rep_last_flush > 0.25) {
        ID3D11DeviceContext_Flush(s_ctx);
        s_rep_last_flush = now;
    }
    s_rep_flushes++;
}

/* Complete reports in command order, only after all preceding GPU work. */
static void d3d_poll(void)
{
    LONG owner = InterlockedCompareExchange(&s_device_thread, 0, 0);
    if (!owner || (DWORD)owner != GetCurrentThreadId() || !s_rep)
        return;
    while (s_rep_head != s_rep_tail) {
        Report *r = &s_rep[s_rep_head % MAX_REPORTS];
        uint64_t sum = r->prefix;
        BOOL done = FALSE;
        HRESULT hr;
        int i;
        hr = ID3D11DeviceContext_GetData(s_ctx, (ID3D11Asynchronous *)r->event,
                                        &done, sizeof done, D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_FALSE || (hr == S_OK && !done)) {
            report_pending();
            return;
        }
        if (hr != S_OK)
            query_error("GetData(report event)", hr);
        for (i = 0; i < r->nq; i++) {
            UINT64 n = 0;
            hr = ID3D11DeviceContext_GetData(s_ctx, (ID3D11Asynchronous *)r->q[i], &n,
                                             sizeof n, D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_FALSE) {
                report_pending();
                return;
            }
            if (hr != S_OK)
                query_error("GetData(occlusion)", hr);
            sum += n;
        }
        if (r->reset)
            s_rep_running = 0;
        s_rep_running += sum;
        report_write(r, s_rep_running);
        for (i = 0; i < r->nq; i++)
            query_put(r->q[i]);
        event_put(r->event);
        s_rep_head++;
    }
}

static int d3d_zpass_report(uint32_t report_va, uint64_t stamp)
{
    Report *r;
    int counting = s_q_active != NULL, i;

    if (!s_zpass_async || !s_dev)
        return 0;
    if (!s_rep && !(s_rep = (Report *)calloc(MAX_REPORTS, sizeof *s_rep))) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate asynchronous reports; using synchronous queries\n");
        s_zpass_async = 0;
        return 0;
    }
    if (s_rep_tail - s_rep_head == MAX_REPORTS)
        while (s_rep_tail - s_rep_head == MAX_REPORTS) {
            d3d_poll();
            SwitchToThread();
        }
    query_end();
    r = &s_rep[s_rep_tail % MAX_REPORTS];
    r->va = report_va;
    r->stamp = stamp;
    r->prefix = s_q_total;
    r->reset = s_rep_reset_next;
    r->nq = s_q_npending;
    r->event = event_get();
    ID3D11DeviceContext_End(s_ctx, (ID3D11Asynchronous *)r->event);
    for (i = 0; i < s_q_npending; i++)
        r->q[i] = s_q_pending[i];
    s_q_npending = 0;
    s_q_total = 0;
    s_rep_reset_next = 0;
    s_rep_tail++;
    if (counting) {
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    }
    d3d_poll();
    return 1;
}

static void d3d_zpass(int op)
{
    if (!ensure_device())
        return;
    if (s_zpass_async && op == NV2A_ZPASS_CLEAR) {
        /* Discarded queries may still be in flight, so never reuse them. */
        int counting = s_q_active != NULL, i;
        query_end();
        for (i = 0; i < s_q_npending; i++)
            ID3D11Query_Release(s_q_pending[i]);
        s_q_npending = 0;
        s_q_total = 0;
        s_rep_reset_next = 1;
        if (counting) {
            s_q_active = query_get();
            ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
        }
        return;
    }
    if (op == NV2A_ZPASS_BEGIN) {
        query_end();
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    } else if (op == NV2A_ZPASS_END) {
        query_end();
    } else {                                /* CLEAR_REPORT_VALUE */
        int counting = s_q_active != NULL;
        query_end();
        query_drain();
        s_q_total = 0;
        if (counting) {
            s_q_active = query_get();
            ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
        }
    }
}

static uint32_t d3d_zpass_count(void)
{
    int counting = s_q_active != NULL;
    if (!s_dev)
        return 0;
    query_end();
    query_drain();
    if (counting) {
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    }
    s_q_reads++;
    return s_q_total > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)s_q_total;
}

/* ---- readback and flip ----------------------------------------------------- */

static ID3D11Texture2D *s_staging;
static uint32_t s_staging_w, s_staging_h, s_staging_bpp;

/* Resolve only the exact linear RGBA8 alias of a fully initialized Xbox
 * Z24S8 image. NV097 colour 0x41 is LU_IMAGE_R8G8B8A8 (nv2a_regs.h).
 * Its alpha is the low-byte stencil, not an invented bloom/depth mask.
 * Ordinary colour textures and experimental 0x2E depth sampling are unchanged. */
static int resolve_depth_color_alias(const Nv2aTexture *t,int cube)
{
    Depth *d=NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11ShaderResourceView *none[4]={NULL,NULL,NULL,NULL};
    uint64_t span;
    uintptr_t cursor,end;
    uint32_t x,y;
    int i;
    HRESULT hr;
    if(cube||t->color!=0x41||gpu_texture_mip_levels(t)!=1)return 0;
    for(i=0;i<s_ndepth;i++)if((s_depth[i].va&0x0FFFFFFFu)==(t->offset&0x0FFFFFFFu)){d=&s_depth[i];break;}
    if(!d||!d->tex||!d->gpu_dirty)return 0;
    if(d->format!=2||d->surface_type!=1||!d->initialized||!d->stencil_initialized||
       t->width!=d->w||t->height!=d->h||t->pitch!=d->pitch||t->pitch<(uint64_t)t->width*4) {
        static int warned;
        if(!warned++)fprintf(stderr,"[GPU-D3D11] RGBA8 depth alias retains guest fallback: incompatible or uninitialized Z24S8 source %08X\n",d->va);
        return 0;
    }
    span=(uint64_t)(d->h-1)*d->pitch+(uint64_t)d->w*4;
    if(!readable_guest_range(d->va,span))return -1;
    // Check every destination region before publishing shared guest bytes.
    // Protected pages keep their protection; no partial row is written.
    cursor=(uintptr_t)GUEST(d->va);end=cursor+(uintptr_t)span;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t base,available;
        if(VirtualQuery((const void *)cursor,&info,sizeof info)!=sizeof info||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY)))return -1;
        base=(uintptr_t)info.BaseAddress;
        if(cursor<base||cursor-base>=info.RegionSize)return -1;
        available=info.RegionSize-(cursor-base);cursor+=available<end-cursor?available:end-cursor;
    }
    ID3D11Texture2D_GetDesc(d->tex,&td);
    if(!s_depth_color_staging||s_depth_color_w!=d->w||s_depth_color_h!=d->h||s_depth_color_format!=td.Format) {
        if(s_depth_color_staging)ID3D11Texture2D_Release(s_depth_color_staging);
        s_depth_color_staging=NULL;
        td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.MiscFlags=0;
        hr=ID3D11Device_CreateTexture2D(s_dev,&td,NULL,&s_depth_color_staging);
        if(FAILED(hr))goto failed;
        s_depth_color_w=d->w;s_depth_color_h=d->h;s_depth_color_format=td.Format;
    }
    ID3D11DeviceContext_PSSetShaderResources(s_ctx,0,4,none);
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx,0,NULL,NULL);
    ID3D11DeviceContext_CopyResource(s_ctx,(ID3D11Resource *)s_depth_color_staging,(ID3D11Resource *)d->tex);
    if(s_cur)bind_output_views(s_cur,s_cur_depth);
    hr=ID3D11DeviceContext_Map(s_ctx,(ID3D11Resource *)s_depth_color_staging,0,D3D11_MAP_READ,0,&mapped);
    if(FAILED(hr))goto failed;
    for(y=0;y<d->h;y++)for(x=0;x<d->w;x++) {
        uint32_t native,guest;
        memcpy(&native,(const uint8_t *)mapped.pData+(size_t)y*mapped.RowPitch+(size_t)x*4,4);
        // D3D R24G8: depth[23:0], stencil[31:24]. Xbox: depth[31:8],
        // stencil[7:0]. Rotate exactly eight bits before the existing 0x41
        // decoder maps R/G/B/A; do not reinterpret a depth SRV as colour.
        guest=(native<<8)|(native>>24);
        memcpy(GUEST(d->va)+(size_t)y*d->pitch+(size_t)x*4,&guest,4);
    }
    ID3D11DeviceContext_Unmap(s_ctx,(ID3D11Resource *)s_depth_color_staging,0);
    d->gpu_dirty=0;
    return 1;
failed:
    fprintf(stderr,"[GPU-D3D11] RGBA8 depth alias readback failed for %08X: %08lX\n",d->va,(unsigned long)hr);
    return -1;
}

static int d3d_readback(uint32_t color_va)
{
    Surf *sf = find_surf(color_va);
    D3D11_MAPPED_SUBRESOURCE m;
    uint32_t y, row;
    double t0 = qpc_ms();

    if (!sf || !s_dev)
        return 0;
    if(!sync_guest_surface(sf,1))return 0;
    if (!s_staging || s_staging_w != sf->w || s_staging_h != sf->h || s_staging_bpp != sf->bpp) {
        D3D11_TEXTURE2D_DESC td;
        if (s_staging)
            ID3D11Texture2D_Release(s_staging);
        s_staging = NULL;
        memset(&td, 0, sizeof td);
        td.Width = sf->w; td.Height = sf->h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = surf_format(sf->bpp);
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &s_staging)))
            return 0;
        s_staging_w = sf->w; s_staging_h = sf->h; s_staging_bpp = sf->bpp;
    }
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)s_staging,
                                     (ID3D11Resource *)sf->tex);
    if (FAILED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_staging, 0,
                                       D3D11_MAP_READ, 0, &m)))
        return 0;
    row = sf->w * sf->bpp;
    if (row > sf->pitch)
        row = sf->pitch;
    for (y = 0; y < sf->h; y++)
        memcpy(GUEST(sf->va) + (size_t)y * sf->pitch, (const uint8_t *)m.pData + (size_t)y * m.RowPitch,
               row);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_staging, 0);
    if(!snapshot_guest_surface(sf))return 0;
    sf->guest_check_gen=s_gen;sf->gpu_dirty=0;
    s_st.readbacks++;
    s_st.readback_ms += qpc_ms() - t0;
    return 1;
}

static void d3d_before_ui(const Nv2aSurface *surface)
{
    bf_motion_trace_ui(s_gen);
    Surf *sf;
    if(!s_dev||!surface||(sf=get_surf(surface))==NULL||sf->aa_generation==s_gen)return;
    /* Mark even when disabled, so enabling AA midway through interface draws
     * cannot filter text that has already been painted into this surface. */
    sf->aa_generation=s_gen;
    motion_disable_draw();if(s_cur)bind_output_views(s_cur,s_cur_depth);
    if(s_q_active) {
        sf->ui_generation=s_gen;
        if(bf_ui_composition_enabled()) {
            HRESULT capture=bf_ui_composition_begin(&sf->ui_overlay,s_dev,s_ctx,s_gen,sf->va,sf->w,sf->h);
            if(FAILED(capture)) {
                bf_ui_composition_invalidate(sf->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_TARGET);
                fprintf(stderr,"[GPU-UI] capture unavailable %08lX; original image retained\n",(unsigned long)capture);
            }
            bf_ui_composition_invalidate(sf->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_TARGET);
        }
        return;
    }
    if((bf_scene_aa_enabled()||bf_scene_snapshot_enabled()||temporal_depth_enabled()||
        bf_ui_composition_enabled())&&!sync_guest_surface(sf,1))return;
    if(temporal_depth_enabled()&&sf->temporal_frozen) {
        BfSceneDepthHoldInfo held=sf->temporal_identity;
        held.source.frame=s_gen;held.geometry_serial=sf->temporal_geometry_serial;
        if(bf_scene_depth_hold_finish(sf->temporal_depth,s_ctx,sf->tex,&held)) {
            static unsigned lines;
            if(lines++<8)fprintf(stderr,"[TEMPORAL-DEPTH] retained frame=%u color=%08X depth=%08X view=%08X context=%08X geometry=%llu draws=%u; projection_unverified\n",
                s_gen,sf->va,held.source.depth_va,held.view,held.context,(unsigned long long)held.geometry_serial,held.source.depth_draws);
        }
    }
    sf->ui_generation=s_gen;
    if(bf_ui_composition_enabled()) {
        HRESULT capture=bf_ui_composition_begin(&sf->ui_overlay,s_dev,s_ctx,s_gen,sf->va,sf->w,sf->h);
        if(FAILED(capture)) {
            bf_ui_composition_invalidate(sf->ui_overlay,BF_UI_CAPTURE_UNSUPPORTED_TARGET);
            fprintf(stderr,"[GPU-UI] capture unavailable %08lX; original image retained\n",(unsigned long)capture);
        }
    }
    if(bf_scene_snapshot_enabled()&&!sf->scene_tainted&&sf->scene_depth_draws&&sf->scene_generation==s_gen) {
        int i;
        for(i=0;i<s_ndepth;i++) {
            Depth *d=&s_depth[i];
            if(d->va==sf->scene_depth_va&&d->initialized&&d->w==sf->w&&d->h==sf->h) {
                BfSceneSnapshotInfo info={(uint64_t)s_gen,sf->va,d->va,sf->scene_depth_draws,sf->w,sf->h};
                bf_scene_snapshot_publish(s_dev,s_ctx,sf->tex,d->tex,&info);
                break;
            }
        }
    }
    if(!bf_scene_aa_enabled())return;
    if(bf_scene_antialias(s_dev,s_ctx,sf->tex)) {
        sf->gpu_dirty=1;
        if(getenv("BF_TRACE_AA_UI")&&(s_gen<8||s_gen%300==0))
            fprintf(stderr,"[AA] scene before UI frame=%u surface=%08X %ux%u\n",s_gen,sf->va,sf->w,sf->h);
    }
}

static void publish_ui_surface(Surf *sf)
{
    if(sf->ui_published_generation==s_gen)return;
    bf_ui_composition_publish(sf->ui_overlay,s_ctx,s_gen);
    sf->ui_published_generation=s_gen;
}

/* Called after all source draws/readback, before the completed framebuffer
 * is published. Its executor generation must travel with that CPU fallback;
 * the independent window-thread source counter is not a GPU scene identity. */
void bruteforce_d3d11_prepare_present(uint32_t color_va)
{
    int i;
    bf_temporal_handoff_source(s_gen,color_va);
    if(bf_motion_trace_active()) {
        BfMotionSnapshot motion;
        if(bf_motion_renderer_snapshot(s_motion_renderer,color_va,s_gen,&motion))
            fprintf(stderr,"[TEMPORAL-SOURCE] frame=%u color=%08X draws=%u tracked=%u previous=%u new=%u transparent=%u unknown_opaque=%u unsupported_projection=%u resource_failures=%u clear=%u jitter=%u complete=%u\n",
                s_gen,color_va,motion.draws,motion.tracked_draws,motion.previous_draws,motion.new_history_draws,
                motion.transparent_draws,motion.untracked_opaque,motion.unsupported_projection,motion.resource_failures,
                motion.clear_observed,motion.jitter_applied,motion.complete);
    }
    if(!s_ctx||!bf_ui_composition_enabled())return;
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx,0,NULL,NULL);
    for(i=0;i<s_nsurf;i++)if(s_surf[i].va==color_va)publish_ui_surface(&s_surf[i]);
    if(s_cur)bind_output_views(s_cur,s_cur_depth);
}

static void d3d_flip(void)
{
    if(bf_ui_composition_enabled()&&s_ctx) {
        int i;
        /* Native consumers may unwrap the separate HUD texture. It must not
         * remain an OM output while ownership passes to another API/queue. */
        ID3D11DeviceContext_OMSetRenderTargets(s_ctx,0,NULL,NULL);
        for(i=0;i<s_nsurf;i++) {
            BfUiCompositionSnapshot captured;
            publish_ui_surface(&s_surf[i]);
            if(temporal_depth_enabled()&&ui_capture_trace_diagnostics()&&
               bf_ui_composition_snapshot(s_surf[i].ui_overlay,s_gen,&captured)) {
                static unsigned lines;
                if(lines++<16)fprintf(stderr,"[TEMPORAL-UI] frame=%u color=%08X size=%ux%u draws=%u status=%u source_reason=%s\n",
                    s_gen,s_surf[i].va,captured.info.width,captured.info.height,
                    captured.info.draws,(unsigned)captured.info.status,
                    ui_source_change_name(captured.info.source_change_reason));
            }
        }
        if(s_cur)bind_output_views(s_cur,s_cur_depth);
    }
    bf_motion_renderer_complete(s_motion_renderer,s_gen);
    bf_motion_trace_flip(s_gen);
    s_wide_scope=0;s_ui_anchor=1;
    s_gen++;
    stats_flip();
}

static const Nv2aBackend s_backend = { d3d_clear, d3d_draw, d3d_flip };
static const Nv2aBackendExt s_backend_ext = { d3d_xdraw, d3d_xdraw_vsh, d3d_vsh_readback,
                                              d3d_vsh_raw_ok, d3d_readback, d3d_zpass,
                                              d3d_zpass_count, d3d_zpass_report, d3d_poll,
                                              NV2A_BACKEND_CAP_WINDOW_CLIP, d3d_before_ui, bruteforce_d3d11_prepare_present };

int bruteforce_d3d11_install(void)
{
    const char *scene_api=getenv("RECOMP_GPU");
    const char *warp = getenv("RECOMP_D3D11_WARP");
    const char *mulzero = getenv("RECOMP_GPU_VSH_MULZERO");
    const char *tex_gpu = getenv("RECOMP_GPU_TEX_DECODE");
    const char *tex_log = getenv("RECOMP_GPU_TEXLOG");
    const char *depth_texture = getenv("RECOMP_GPU_DEPTH_TEXTURE");

    if (depth_texture && *depth_texture && strcmp(depth_texture, "0") != 0 &&
        strcmp(depth_texture, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_DEPTH_TEXTURE must be 0 or 1\n");
        return 0;
    }
    if (!gpu_pscache_init()) return 0;
    s_warp = warp && strcmp(warp, "1") == 0;
    s_d3d12=scene_api&&strcmp(scene_api,"d3d12")==0;
    s_depth_texture = depth_texture && strcmp(depth_texture, "1") == 0;
    s_tex_gpu = !tex_gpu || strcmp(tex_gpu, "0") != 0;
    s_tex_log = tex_log && strcmp(tex_log, "1") == 0;
    s_vsh_flags = mulzero && strcmp(mulzero, "1") == 0 ? GPU_VSH_MULZERO : 0;
    s_vsh_sync = getenv("RECOMP_GPU_VSH_SYNC") && strcmp(getenv("RECOMP_GPU_VSH_SYNC"), "1") == 0;
    { const char *reports=getenv("RECOMP_GPU_ZPASS");
      s_zpass_async=!reports||!*reports||strcmp(reports,"async")==0; }
    s_tex = (Tex *)calloc(MAX_TEX, sizeof *s_tex);
    if (!s_tex) {
        fprintf(stderr, "Cannot allocate the D3D11 texture cache\n");
        return 0;
    }
    if(atexit(release_color_snapshots)!=0) {
        free(s_tex);s_tex=NULL;
        fprintf(stderr,"Cannot register colour snapshot shutdown cleanup\n");
        return 0;
    }
    nv2a_backend_ext_register(&s_backend_ext);
    nv2a_backend_register(&s_backend);
    bf_aspect_surface_reader(completed_surface_wide);
    bf_aspect_backend_commands(bruteforce_d3d11_wide_scope,bruteforce_d3d11_ui_anchor);
    fprintf(stderr, "[GPU] %s scene backend registered (RECOMP_GPU=%s%s)\n",
            s_d3d12?"Direct3D 12 via D3D11On12":"Direct3D 11",s_d3d12?"d3d12":"d3d11",s_warp ? ", WARP" : "");
    if (s_depth_texture)
        fprintf(stderr, "[GPU-D3D11] experimental D24 depth texture aliases enabled\n");
    return 1;
}

/* GPU semaphore and software-event writes must follow real GPU completion.
 * EVENT queries are ended after the preceding immediate-context commands;
 * Flush alone only submits them. This runs solely on the NV2A consumer. */
static void wait_for_scene(void)
{
    static ID3D11Query *event;
    D3D11_QUERY_DESC descriptor = {D3D11_QUERY_EVENT,0};
    BOOL done = FALSE;
    HRESULT result;
    ULONGLONG started;
    if (!s_ctx) return; /* no GPU commands have been issued */
    if(s_d3d12) {
        result=bf_scene_d3d12_wait(s_ctx);
        if(SUCCEEDED(result))return;
        fprintf(stderr,"[GPU-D3D12] scene completion failed %08lX\n",(unsigned long)result);abort();
    }
    if (!event && FAILED(ID3D11Device_CreateQuery(s_dev,&descriptor,&event))) {
        fprintf(stderr,"[GPU-D3D11] completion query allocation failed\n"); abort();
    }
    ID3D11DeviceContext_End(s_ctx,(ID3D11Asynchronous *)event);
    ID3D11DeviceContext_Flush(s_ctx);
    started = GetTickCount64();
    for (;;) {
        result=ID3D11DeviceContext_GetData(s_ctx,(ID3D11Asynchronous *)event,&done,sizeof done,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (result == S_OK && done) return;
        if (FAILED(result) || GetTickCount64()-started > 5000) {
            fprintf(stderr,"[GPU-D3D11] completion failed %08lX, device %08lX\n",(unsigned long)result,
                (unsigned long)ID3D11Device_GetDeviceRemovedReason(s_dev)); abort();
        }
        SwitchToThread();
    }
}

void bruteforce_d3d11_wait(void)
{
    double start, elapsed;
    if (!profile_enabled()) { wait_for_scene(); return; }
    start = qpc_ms();
    wait_for_scene();
    elapsed = qpc_ms() - start;
    s_completion_profile.ms += elapsed;
    if (elapsed > s_completion_profile.max_ms) s_completion_profile.max_ms = elapsed;
    ++s_completion_profile.calls;
}
