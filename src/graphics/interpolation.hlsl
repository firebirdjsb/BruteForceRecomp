Texture2D<float4> previousImage : register(t0);
Texture2D<float4> currentImage : register(t1);
Texture2D<float4> motion : register(t2);
Texture2D<float2> matchError : register(t3);
SamplerState linearClamp : register(s0);
cbuffer Parameters : register(b0) { uint2 size;uint2 blocks;float phase;uint3 reserved; };
#ifdef BF_FLOW
RWTexture2D<float4> motionOut : register(u0);
RWTexture2D<float2> errorOut : register(u1);
float3 colorAt(bool old,int2 p) {
    p=clamp(p,int2(0,0),int2(size)-1);
    return old?previousImage.Load(int3(p,0)).rgb:currentImage.Load(int3(p,0)).rgb;
}
float cost(bool forward,int2 p,int2 delta) {
    float value=0;
    [unroll]for(int y=-3;y<=3;y+=2)[unroll]for(int x=-3;x<=3;x+=2)
        value+=dot(abs(colorAt(forward,p+int2(x,y))-colorAt(!forward,p+int2(x,y)+delta)),float3(.299,.587,.114));
    return value/16;
}
float2 search(bool forward,int2 p,out float error) {
    float best=cost(forward,p,int2(0,0));int2 shift=0;
    // Coarse search covers +/-32 source pixels; refine at pixel precision.
    [loop]for(int y=-32;y<=32;y+=8)[loop]for(int x=-32;x<=32;x+=8) {
        float candidate=cost(forward,p,int2(x,y))+.00002*(abs(x)+abs(y));
        if(candidate<best){best=candidate;shift=int2(x,y);}
    }
    int2 coarse=shift;
    [loop]for(int y=-4;y<=4;++y)[loop]for(int x=-4;x<=4;++x) {
        int2 delta=coarse+int2(x,y);float candidate=cost(forward,p,delta)+.00002*(abs(delta.x)+abs(delta.y));
        if(candidate<best){best=candidate;shift=delta;}
    }
    error=best;return float2(shift);
}
[numthreads(8,8,1)]
void flowMain(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=blocks))return;
    int2 p=min(int2(id.xy*8+4),int2(size)-1);float a,b;
    float2 forward=search(true,p,a),backward=search(false,p,b);
    motionOut[id.xy]=float4(forward,backward);errorOut[id.xy]=float2(a,b);
}
#else
RWTexture2D<float4> interpolated : register(u0);
float4 sampleMotion(float2 p){return motion.SampleLevel(linearClamp,(p+.5)/(float2(blocks)*8),0);}
[numthreads(8,8,1)]
void warpMain(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=size))return;
    float2 p=float2(id.xy),uv=(p+.5)/float2(size);
    float3 old=previousImage.Load(int3(id.xy,0)).rgb,now=currentImage.Load(int3(id.xy,0)).rgb;
    // Exact stationary pixels (including HUD strokes) remain exact. Changing
    // zero-motion pixels use the new source, avoiding blended text/numbers.
    if(max(abs(old.r-now.r),max(abs(old.g-now.g),abs(old.b-now.b)))<.003) {interpolated[id.xy]=float4(now,1);return;}
    float2 a=p,b=p;
    [unroll]for(int i=0;i<3;++i) {
        a=p-phase*sampleMotion(a).xy;
        b=p-(1-phase)*sampleMotion(b).zw;
    }
    float4 va=sampleMotion(a),vb=sampleMotion(b);
    float ea=matchError.SampleLevel(linearClamp,(a+.5)/(float2(blocks)*8),0).x;
    float eb=matchError.SampleLevel(linearClamp,(b+.5)/(float2(blocks)*8),0).y;
    float3 ca=previousImage.SampleLevel(linearClamp,(a+.5)/float2(size),0).rgb;
    float3 cb=currentImage.SampleLevel(linearClamp,(b+.5)/float2(size),0).rgb;
    bool reliable=all(a>=0)&&all(a<float2(size))&&all(b>=0)&&all(b<float2(size))&&
        ea<.09&&eb<.09&&length(va.xy+vb.zw)<3.0&&length(va.xy)>.25&&length(ca-cb)<.3;
    interpolated[id.xy]=float4(reliable?lerp(ca,cb,phase):now,1);
}
#endif
