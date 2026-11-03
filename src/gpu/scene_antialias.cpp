#define NOMINMAX
#include "scene_antialias.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
namespace {
using Microsoft::WRL::ComPtr;
constexpr char source[]=R"AA(
Texture2D<float4> image:register(t0);SamplerState linearClamp:register(s0);
struct V{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V v;float2 p=float2((id<<1)&2,id&2);v.pos=float4(p*float2(2,-2)+float2(-1,1),0,1);v.uv=p;return v;}
float luma(float3 c){return dot(c,float3(.299,.587,.114));}
float4 ps(V v):SV_Target{
 uint w,h;image.GetDimensions(w,h);float2 step=1.0/float2(w,h),uv=v.uv;
 float4 m=image.SampleLevel(linearClamp,uv,0);
 float nw=luma(image.SampleLevel(linearClamp,uv+float2(-1,-1)*step,0).rgb);
 float ne=luma(image.SampleLevel(linearClamp,uv+float2(1,-1)*step,0).rgb);
 float sw=luma(image.SampleLevel(linearClamp,uv+float2(-1,1)*step,0).rgb);
 float se=luma(image.SampleLevel(linearClamp,uv+float2(1,1)*step,0).rgb);
 float lm=luma(m.rgb),lo=min(lm,min(min(nw,ne),min(sw,se))),hi=max(lm,max(max(nw,ne),max(sw,se)));
 if(hi-lo<max(8.0/255.0,hi*.125))return m;
 float2 d=float2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
 d=clamp(d/(min(abs(d.x),abs(d.y))+max((nw+ne+sw+se)*.03125,2.0/255.0)),-8.0,8.0)*step;
 float3 a=.5*(image.SampleLevel(linearClamp,uv-d/6,0).rgb+image.SampleLevel(linearClamp,uv+d/6,0).rgb);
 float3 b=a*.5+.25*(image.SampleLevel(linearClamp,uv-d*.5,0).rgb+image.SampleLevel(linearClamp,uv+d*.5,0).rgb);
 float lb=luma(b);return float4(lb<lo||lb>hi?a:b,m.a);
}
)AA";
void require(HRESULT h){if(FAILED(h))throw std::runtime_error("scene AA resource or shader creation failed");}
struct SavedState {
    ID3D11DeviceContext *c;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology{};
    ComPtr<ID3D11ShaderResourceView> texture;ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11DepthStencilView> dsv;ID3D11RenderTargetView *targets[8]{};
    D3D11_VIEWPORT viewports[16]{};D3D11_RECT scissors[16]{};UINT nv=16,ns=16,mask{},reference{};float factor[4]{};
    explicit SavedState(ID3D11DeviceContext *ctx):c(ctx) {
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->GSGetShader(&gs,nullptr,nullptr);
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
        c->PSGetShaderResources(0,1,&texture);c->PSGetSamplers(0,1,&sampler);
        c->RSGetState(&raster);c->RSGetViewports(&nv,viewports);c->RSGetScissorRects(&ns,scissors);
        c->OMGetBlendState(&blend,factor,&mask);c->OMGetDepthStencilState(&depth,&reference);
        c->OMGetRenderTargets(8,targets,&dsv);
    }
    ~SavedState() {
        ID3D11ShaderResourceView *empty=nullptr;c->PSSetShaderResources(0,1,&empty);
        c->OMSetRenderTargets(8,targets,dsv.Get());for(auto *p:targets)if(p)p->Release();
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->GSSetShader(gs.Get(),nullptr,0);
        c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
        auto *t=texture.Get();auto *s=sampler.Get();c->PSSetShaderResources(0,1,&t);c->PSSetSamplers(0,1,&s);
        c->RSSetState(raster.Get());c->RSSetViewports(nv,viewports);c->RSSetScissorRects(ns,scissors);
        c->OMSetBlendState(blend.Get(),factor,mask);c->OMSetDepthStencilState(depth.Get(),reference);
    }
};
struct Filter {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11Texture2D> snapshot;ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_TEXTURE2D_DESC previous{};
    void initialize(ID3D11Device *d) {
        if(device.Get()==d&&vs&&ps&&sampler&&raster&&depth&&blend)return;
        *this=Filter{};device=d;
        ComPtr<ID3DBlob> v,p,error;
        require(D3DCompile(source,sizeof source-1,"scene-aa",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&error));
        require(D3DCompile(source,sizeof source-1,"scene-aa",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&error));
        require(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));
        require(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        require(d->CreateSamplerState(&s,&sampler));
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        require(d->CreateRasterizerState(&r,&raster));
        D3D11_DEPTH_STENCIL_DESC z{};require(d->CreateDepthStencilState(&z,&depth));
        D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        require(d->CreateBlendState(&b,&blend));
    }
    void apply(ID3D11Device *d,ID3D11DeviceContext *c,ID3D11Texture2D *target) {
        initialize(d);D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
        if(desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1)throw std::runtime_error("unsupported scene AA surface");
        if(!snapshot||desc.Width!=previous.Width||desc.Height!=previous.Height||desc.Format!=previous.Format) {
            snapshot.Reset();srv.Reset();previous=desc;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;
            desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
            require(d->CreateTexture2D(&desc,nullptr,&snapshot));require(d->CreateShaderResourceView(snapshot.Get(),nullptr,&srv));
        }
        ComPtr<ID3D11RenderTargetView> rtv;require(d->CreateRenderTargetView(target,nullptr,&rtv));
        SavedState state(c);c->OMSetRenderTargets(0,nullptr,nullptr);
        c->CopyResource(snapshot.Get(),target);auto *view=rtv.Get();c->OMSetRenderTargets(1,&view,nullptr);
        D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};c->RSSetViewports(1,&viewport);
        c->RSSetState(raster.Get());c->OMSetBlendState(blend.Get(),nullptr,UINT_MAX);c->OMSetDepthStencilState(depth.Get(),0);
        c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs.Get(),nullptr,0);c->GSSetShader(nullptr,nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
        auto *texture=srv.Get();auto *ss=sampler.Get();c->PSSetShaderResources(0,1,&texture);c->PSSetSamplers(0,1,&ss);
        c->Draw(3,0);
    }
};
}
extern "C" int bf_scene_antialias(ID3D11Device *d,ID3D11DeviceContext *c,ID3D11Texture2D *target) try {
    if(!d||!c||!target)return 0;
    static Filter filter;filter.apply(d,c,target);return 1;
} catch(const std::exception &e) {
    static unsigned errors{};if(errors++<4)std::fprintf(stderr,"[AA] Original scene retained: %s\n",e.what());return 0;
}
