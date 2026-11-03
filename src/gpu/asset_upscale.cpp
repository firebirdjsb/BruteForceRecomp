#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "asset_upscale.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cstdio>
namespace {
using Microsoft::WRL::ComPtr;
constexpr char shader[]=R"HLSL(
Texture2DArray<float4> image:register(t0);
RWTexture2DArray<float4> result:register(u0);
cbuffer Params:register(b0){uint2 outputSize;uint faces,sourceMip;uint2 sourceSize;uint reconstruct,padding;};
float4 load(int2 p,uint face){return image.Load(int4(clamp(p,int2(0,0),int2(sourceSize)-1),face,sourceMip));}
float4 weights(float t){float t2=t*t,t3=t2*t;return float4(-.5*t+t2-.5*t3,1-2.5*t2+1.5*t3,.5*t+2*t2-1.5*t3,-.5*t2+.5*t3);}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=outputSize)||id.z>=faces)return;
 if(!reconstruct){result[id]=image.Load(int4(id.xy,id.z,sourceMip));return;}
 float2 p=(float2(id.xy)+.5)*float2(sourceSize)/float2(outputSize)-.5;
 int2 base=int2(floor(p));float2 t=frac(p);float4 wx=weights(t.x),wy=weights(t.y),value=0;
 [unroll]for(int y=0;y<4;y++)[unroll]for(int x=0;x<4;x++)value+=load(base+int2(x-1,y-1),id.z)*wx[x]*wy[y];
 // Bound reconstruction by the four adjacent source texels in every channel.
 // Keep alpha independent: it can be shader data, not just transparency.
 float4 a=load(base,id.z),b=load(base+int2(1,0),id.z),c=load(base+int2(0,1),id.z),d=load(base+1,id.z);
 result[id]=clamp(value,min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
})HLSL";
}
extern "C" int bf_asset_upscale(ID3D11Device *device,ID3D11DeviceContext *context,
    ID3D11Texture2D *source,unsigned factor,ID3D11Texture2D **texture,ID3D11ShaderResourceView **view) {
    if(!device||!context||!source||!texture||!view||(factor!=2&&factor!=4))return 0;
    *texture=nullptr;*view=nullptr;
    D3D11_TEXTURE2D_DESC original{};source->GetDesc(&original);
    const unsigned extra=factor==4?2:1;
    if(!original.Width||!original.Height||!original.MipLevels||original.SampleDesc.Count!=1||
       !(original.BindFlags&D3D11_BIND_SHADER_RESOURCE)||original.Width>4096/factor||original.Height>4096/factor||
       (original.ArraySize!=1&&original.ArraySize!=6))return 0;
    // Cache CPU bytecode only. Device resources retain local ownership and
    // cannot outlive the renderer's device shutdown through a global cache.
    static const ComPtr<ID3DBlob> code=[] {
        ComPtr<ID3DBlob> bytes,error;
        if(FAILED(D3DCompile(shader,sizeof shader-1,"asset-reconstruction",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytes,&error))) {
            if(error)std::fprintf(stderr,"[ASSET] %s\n",static_cast<const char*>(error->GetBufferPointer()));bytes.Reset();
        }
        return bytes;
    }();
    ComPtr<ID3D11ComputeShader> compute;
    if(!code)return 0;
    if(FAILED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&compute)))return 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC inputDesc{};inputDesc.Format=original.Format;inputDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    inputDesc.Texture2DArray.MipLevels=original.MipLevels;inputDesc.Texture2DArray.ArraySize=original.ArraySize;
    ComPtr<ID3D11ShaderResourceView> input;if(FAILED(device->CreateShaderResourceView(source,&inputDesc,&input)))return 0;
    D3D11_TEXTURE2D_DESC desc=original;desc.Width*=factor;desc.Height*=factor;desc.MipLevels+=extra;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;desc.CPUAccessFlags=0;
    desc.MiscFlags=original.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> output;if(FAILED(device->CreateTexture2D(&desc,nullptr,&output)))return 0;
    D3D11_BUFFER_DESC bufferDesc{};bufferDesc.ByteWidth=32;bufferDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bufferDesc.Usage=D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Buffer> constants;if(FAILED(device->CreateBuffer(&bufferDesc,nullptr,&constants)))return 0;
    // Prepare all views before modifying immediate-context state; failures
    // then retain the renderer's untouched original resource.
    std::array<ComPtr<ID3D11UnorderedAccessView>,16> levels;
    if(desc.MipLevels>levels.size())return 0;
    for(unsigned mip=0;mip<desc.MipLevels;++mip){
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=desc.Format;uav.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        uav.Texture2DArray.MipSlice=mip;uav.Texture2DArray.ArraySize=desc.ArraySize;
        if(FAILED(device->CreateUnorderedAccessView(output.Get(),&uav,&levels[mip])))return 0;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC outputDesc{};outputDesc.Format=desc.Format;
    outputDesc.ViewDimension=desc.MiscFlags?D3D11_SRV_DIMENSION_TEXTURECUBE:D3D11_SRV_DIMENSION_TEXTURE2D;
    if(desc.MiscFlags)outputDesc.TextureCube.MipLevels=desc.MipLevels;else outputDesc.Texture2D.MipLevels=desc.MipLevels;
    ComPtr<ID3D11ShaderResourceView> sampled;if(FAILED(device->CreateShaderResourceView(output.Get(),&outputDesc,&sampled)))return 0;
    ID3D11ShaderResourceView *inputs[]={input.Get()};ID3D11Buffer *buffers[]={constants.Get()};
    context->CSSetShader(compute.Get(),nullptr,0);context->CSSetShaderResources(0,1,inputs);context->CSSetConstantBuffers(0,1,buffers);
    for(unsigned mip=0;mip<desc.MipLevels;++mip){
        const unsigned srcMip=mip<extra?0:mip-extra;
        const unsigned w=std::max(1u,desc.Width>>mip),h=std::max(1u,desc.Height>>mip);
        std::array<unsigned,8> values{w,h,desc.ArraySize,srcMip,std::max(1u,original.Width>>srcMip),std::max(1u,original.Height>>srcMip),mip<extra?1u:0u,0};
        context->UpdateSubresource(constants.Get(),0,nullptr,values.data(),0,0);
        ID3D11UnorderedAccessView *views[]={levels[mip].Get()};context->CSSetUnorderedAccessViews(0,1,views,nullptr);context->Dispatch((w+7)/8,(h+7)/8,desc.ArraySize);
    }
    ID3D11UnorderedAccessView *noOutput[]={nullptr};ID3D11ShaderResourceView *noInput[]={nullptr};ID3D11Buffer *noBuffer[]={nullptr};
    context->CSSetUnorderedAccessViews(0,1,noOutput,nullptr);context->CSSetShaderResources(0,1,noInput);context->CSSetConstantBuffers(0,1,noBuffer);context->CSSetShader(nullptr,nullptr,0);
    *texture=output.Detach();*view=sampled.Detach();return 1;
}
