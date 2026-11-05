Texture2D<float4> source : register(t0);
SamplerState linearClamp : register(s0);

#if defined(BF_EASU) || defined(BF_RCAS)
#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"
cbuffer Constants : register(b0) {
    uint4 con0, con1, con2, con3;
    uint2 outputSize;
    uint2 padding;
};
RWTexture2D<float4> outputImage : register(u0);
#ifdef BF_EASU
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return source.GatherRed(linearClamp, p); }
AF4 FsrEasuGF(AF2 p) { return source.GatherGreen(linearClamp, p); }
AF4 FsrEasuBF(AF2 p) { return source.GatherBlue(linearClamp, p); }
#else
#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) {
    // RCAS samples a cross extending one texel beyond the image at its edges.
    return source.Load(int3(clamp(p, int2(0, 0), int2(outputSize) - 1), 0));
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#endif
#include "ffx_fsr1.h"
[numthreads(8, 8, 1)]
void computeMain(uint3 position : SV_DispatchThreadID) {
    if (any(position.xy >= outputSize)) return;
    float3 color;
#ifdef BF_EASU
    FsrEasuF(color, position.xy, con0, con1, con2, con3);
#else
    FsrRcasF(color.r, color.g, color.b, position.xy, con0);
#endif
    outputImage[position.xy] = float4(color, 1.0);
}
#else
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vertexMain(uint id : SV_VertexID) {
    Vertex result;
    float2 p = float2((id << 1) & 2, id & 2);
    result.position = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    result.uv = p;
    return result;
}
float4 pixelMain(Vertex vertex) : SV_Target {
    return float4(source.Sample(linearClamp, vertex.uv).rgb, 1);
}
float4 pixelOverlay(Vertex vertex) : SV_Target {
    return source.Sample(linearClamp, vertex.uv);
}
#endif
