/* Tests generic Xbox NV2A register-combiner and vertex HLSL emitters.
 * No title code, game assets, GPU resources or actual game renderer here. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gpu/rc_hlsl.h"
#include "gpu/vsh_hlsl.h"
#ifdef _WIN32
#include <d3dcompiler.h>
#endif

static char pixel[128 * 1024];
static char pixel_repeat[128 * 1024];
static char vertex[128 * 1024];
static char vertex_repeat[128 * 1024];
static const uint32_t program[1][4] = {{0u, 0u, 0u, 1u}};

#ifdef _WIN32
static int shader_compiles(const char *hlsl, const char *entry,
                           const char *target) {
    ID3DBlob *bytecode = NULL, *errors = NULL;
    HRESULT result = D3DCompile(hlsl, strlen(hlsl), "synthetic_nv2a", NULL, NULL,
                                entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                0, &bytecode, &errors);
    if (FAILED(result)) {
        if (errors)
            fprintf(stderr, "%s compilation: %.*s\n", target,
                    (int)ID3D10Blob_GetBufferSize(errors),
                    (const char *)ID3D10Blob_GetBufferPointer(errors));
        if (errors) ID3D10Blob_Release(errors);
        if (bytecode) ID3D10Blob_Release(bytecode);
        return 0;
    }
    if (errors) ID3D10Blob_Release(errors);
    if (!bytecode || ID3D10Blob_GetBufferSize(bytecode) == 0)
        return 0;
    ID3D10Blob_Release(bytecode);
    return 1;
}
#endif

int main(void) {
    GpuRcKey key = {0};
    const size_t p = gpu_rc_hlsl(&key, pixel, sizeof(pixel));
    const size_t p2 = gpu_rc_hlsl(&key, pixel_repeat, sizeof(pixel_repeat));
    const size_t v = gpu_vsh_hlsl(program, 1u, 0u, NULL, 0u,
                                  vertex, sizeof(vertex));
    const size_t v2 = gpu_vsh_hlsl(program, 1u, 0u, NULL, 0u,
                                   vertex_repeat, sizeof(vertex_repeat));
    if (!p || !v || p != p2 || v != v2 ||
        strcmp(pixel, pixel_repeat) || strcmp(vertex, vertex_repeat) ||
        !strstr(pixel, "ps_main") || !strstr(vertex, "vs_main")) {
        fputs("NV2A shader generation was empty or non-deterministic\n", stderr);
        return 1;
    }
    /* Refuse incomplete/truncated buffers; no backend should compile those. */
    char tiny[8];
    if (gpu_rc_hlsl(&key, tiny, sizeof(tiny)) ||
        gpu_vsh_hlsl(program, 1u, 0u, NULL, 0u, tiny, sizeof(tiny))) {
        fputs("NV2A HLSL output overflow was not rejected\n", stderr);
        return 2;
    }
#ifdef _WIN32
    if (!shader_compiles(pixel, "ps_main", "ps_5_0") ||
        !shader_compiles(vertex, "vs_main", "vs_5_0")) {
        fputs("Generated NV2A shader failed D3DCompile\n", stderr);
        return 3;
    }
#endif
    puts("NV2A HLSL generators: deterministic and bounded; Windows compiles PS/VS");
    return 0;
}
