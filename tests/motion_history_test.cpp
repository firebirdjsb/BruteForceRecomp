#include "gpu/motion_history.h"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace bf::gpu;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"Motion GPU history failed line %d: %s\n",__LINE__,#c);return 1;}}while(0)
namespace {
struct Record {float clip[4];uint32_t id;};
static_assert(sizeof(Record)==20);
struct SORecords {ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> view;uint32_t count{};};
struct Pipeline {ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11GeometryShader> so;ComPtr<ID3D11InputLayout> layout;};
struct GPU {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11Buffer> constants;
    Pipeline current,lookup;
    bool compile(const std::string &source,Pipeline &p) {
        ComPtr<ID3DBlob> code,errors;
        if(FAILED(D3DCompile(source.data(),source.size(),"synthetic-motion-so",nullptr,nullptr,"main","vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors))) {
            if(errors)std::fprintf(stderr,"%s\n",static_cast<const char *>(errors->GetBufferPointer()));return false;
        }
        const D3D11_INPUT_ELEMENT_DESC input[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32_UINT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        const D3D11_SO_DECLARATION_ENTRY output[]={
            {0,"SV_POSITION",0,0,4,0},{0,"TEXCOORD",0,0,1,0}};UINT stride=20;
        return SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&p.vs))&&
            SUCCEEDED(device->CreateInputLayout(input,2,code->GetBufferPointer(),code->GetBufferSize(),&p.layout))&&
            SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(),code->GetBufferSize(),
                output,2,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&p.so));
    }
    bool initialize() {
        D3D_FEATURE_LEVEL level;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return false;
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=32;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&cb,nullptr,&constants)))return false;
        const std::string declarations=R"(
struct Input {float4 p:POSITION;uint id:TEXCOORD0;};
struct Output {float4 p:SV_POSITION;uint id:TEXCOORD0;};
)";
        const std::string render=declarations+R"(
cbuffer Transform:register(b0) {float4 shift;float4 deform;};
Output main(Input v) {Output o;o.p=v.p+shift;
o.p.xy+=deform.xy*float2(v.id%3,v.id%5);o.id=v.id;return o;}
)";
        const std::string read=std::string(MotionHistory::lookup_hlsl())+declarations+R"(
StructuredBuffer<BFHistoryPosition> previous:register(t0);
cbuffer Lookup:register(b0) {uint count;uint frame;uint2 unused;float4 pad;};
Output main(Input v) {Output o;float4 p;
bool valid=bf_history_previous(previous,count,v.id,frame,p);
o.p=valid?p:float4(-999,-999,-999,1);o.id=valid?1:0;return o;}
)";
        return compile(render,current)&&compile(read,lookup);
    }
    SORecords run(const std::vector<uint32_t> &ids,const float *transform=nullptr,
                  const MotionPrevious *previous=nullptr,bool nonfinite=false) {
        SORecords result;std::vector<Record> input(ids.size());
        for(size_t i=0;i<ids.size();++i) {
            input[i]={{float(ids[i]%7)*.1f,float(ids[i]%11)*.05f,.5f,1},ids[i]};
            if(nonfinite&&i==0)input[i].clip[0]=std::nanf("");
        }
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(input.size()*sizeof(Record));desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=input.data();ComPtr<ID3D11Buffer> vertices;
        if(FAILED(device->CreateBuffer(&desc,&data,&vertices)))return result;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_STREAM_OUTPUT|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateBuffer(&desc,nullptr,&result.buffer)))return result;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_UINT;srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=UINT(input.size()*5);
        if(FAILED(device->CreateShaderResourceView(result.buffer.Get(),&srv,&result.view)))return {};
        const Pipeline &p=previous?lookup:current;uint32_t lookup_cb[8]{};float normal_cb[8]{};
        if(transform)std::memcpy(normal_cb,transform,sizeof normal_cb);
        if(previous){lookup_cb[0]=previous->count;lookup_cb[1]=previous->expected_frame;}
        context->UpdateSubresource(constants.Get(),0,nullptr,previous?static_cast<void *>(lookup_cb):static_cast<void *>(normal_cb),0,0);
        auto *cb=constants.Get();context->VSSetConstantBuffers(0,1,&cb);
        auto *source=previous?previous->positions.Get():nullptr;context->VSSetShaderResources(0,1,&source);
        UINT stride=20,offset=0;auto *vb=vertices.Get();context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetInputLayout(p.layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(p.vs.Get(),nullptr,0);context->GSSetShader(p.so.Get(),nullptr,0);
        auto *target=result.buffer.Get();context->SOSetTargets(1,&target,&offset);context->Draw(UINT(ids.size()),0);
        target=nullptr;source=nullptr;context->SOSetTargets(1,&target,&offset);context->GSSetShader(nullptr,nullptr,0);
        context->VSSetShaderResources(0,1,&source);result.count=uint32_t(ids.size());return result;
    }
    std::vector<Record> read(const SORecords &records) {
        std::vector<Record> result(records.count);D3D11_BUFFER_DESC desc{};records.buffer->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        if(FAILED(device->CreateBuffer(&desc,nullptr,&staging)))return {};
        context->CopyResource(staging.Get(),records.buffer.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return {};
        std::memcpy(result.data(),mapped.pData,result.size()*sizeof(Record));context->Unmap(staging.Get(),0);return result;
    }
    std::vector<Record> lookup_values(const MotionPrevious &previous,const std::vector<uint32_t> &ids) {
        return read(run(ids,nullptr,&previous));
    }
};
MotionIdentity identity(uint32_t object,uint32_t slot) {return {object,1,slot,0x40000,0x50000,0x60000,0x70000,0x80000,0x123456789ull,0xFEDCBA987ull};}
bool expected(const Record &r,uint32_t id,const float *transform) {
    const float wanted[4]={float(id%7)*.1f+transform[0]+transform[4]*float(id%3),
        float(id%11)*.05f+transform[1]+transform[5]*float(id%5),.5f+transform[2],1+transform[3]};
    if(r.id!=1)return false;
    for(unsigned i=0;i<4;++i)if(std::fabs(r.clip[i]-wanted[i])>2e-6f)return false;return true;
}
}
int main() {
    GPU gpu;CHECK(gpu.initialize());
    MotionHistory history(gpu.device.Get(),gpu.context.Get());CHECK(history.ready());
    const auto a=identity(0x10000,3),b=identity(0x20000,4);
    const std::vector<uint32_t> ids={500000,7,1000};
    const float a1[8]={.1f,.2f,0,0,0,0,0,0},b1[8]={-.3f,.1f,0,0,0,0,0,0};
    const float a2[8]={.2f,-.1f,0,0,.02f,.03f,0,0},b2[8]={-.2f,-.2f,0,0,-.01f,.04f,0,0};
    CHECK(history.begin_frame(1));auto av=history.prepare(a,ids.data(),3),bv=history.prepare(b,ids.data(),3);
    CHECK(!av.frame_valid&&!bv.frame_valid&&av.reason==MotionReset::first_frame);
    auto values=gpu.lookup_values(av,ids);CHECK(values.size()==3);for(auto &v:values)CHECK(!v.id);
    auto so=gpu.run(ids,a1);CHECK(so.view&&history.capture(a,so.view.Get(),so.count));
    so=gpu.run(ids,b1);CHECK(so.view&&history.capture(b,so.view.Get(),so.count));
    CHECK(history.complete_frame(1));
    std::puts("PASS first-frame invalid and real VS/SO GPU scatter for noncontiguous original IDs.");
    CHECK(history.begin_frame(2));av=history.prepare(a,ids.data(),3);bv=history.prepare(b,ids.data(),3);
    CHECK(av.frame_valid&&bv.frame_valid&&av.expected_frame==1);
    values=gpu.lookup_values(av,ids);CHECK(values.size()==3);for(unsigned i=0;i<3;++i)CHECK(expected(values[i],ids[i],a1));
    values=gpu.lookup_values(bv,ids);CHECK(values.size()==3);for(unsigned i=0;i<3;++i)CHECK(expected(values[i],ids[i],b1));
    values=gpu.lookup_values(av,{123});CHECK(values.size()==1&&!values[0].id);
    so=gpu.run(ids,a2);CHECK(history.capture(a,so.view.Get(),so.count));
    // Capturing current positions cannot overwrite the separately retained
    // previous frame consumed by this frame's VS.
    values=gpu.lookup_values(av,ids);for(unsigned i=0;i<3;++i)CHECK(expected(values[i],ids[i],a1));
    so=gpu.run(ids,b2);CHECK(history.capture(b,so.view.Get(),so.count));CHECK(history.complete_frame(2));
    CHECK(history.begin_frame(3));av=history.prepare(a,ids.data(),3);bv=history.prepare(b,ids.data(),3);
    values=gpu.lookup_values(av,ids);for(unsigned i=0;i<3;++i)CHECK(expected(values[i],ids[i],a2));
    values=gpu.lookup_values(bv,ids);for(unsigned i=0;i<3;++i)CHECK(expected(values[i],ids[i],b2));
    std::puts("PASS independent shared-geometry instances, per-vertex deformation and camera displacement retained exactly.");
    // Deliberately omit one original ID this frame. Its two-frames-old cell
    // survives physically, but the GPU lookup must reject its old frame tag.
    so=gpu.run({7,1000},a2);CHECK(history.capture(a,so.view.Get(),so.count));CHECK(history.complete_frame(3));
    CHECK(history.begin_frame(4));av=history.prepare(a,ids.data(),3);
    values=gpu.lookup_values(av,ids);CHECK(!values[0].id&&values[1].id&&values[2].id);
    auto changed=ids;changed[0]=999999;av=history.prepare(a,changed.data(),3);
    CHECK(!av.frame_valid&&av.reason==MotionReset::domain_changed);
    values=gpu.lookup_values(av,changed);for(auto &v:values)CHECK(!v.id);
    CHECK(history.complete_frame(4));CHECK(history.begin_frame(10));av=history.prepare(a,ids.data(),3);
    CHECK(!av.frame_valid&&av.reason==MotionReset::frame_gap);
    auto reused=a;++reused.generation;av=history.prepare(reused,ids.data(),3);
    CHECK(!av.frame_valid&&av.reason==MotionReset::slot_reused);
    so=gpu.run(ids,a1);CHECK(history.capture(reused,so.view.Get(),so.count));
    CHECK(!history.capture(reused,so.view.Get(),so.count)&&history.last_reason()==MotionReset::duplicate_submission);
    CHECK(history.complete_frame(10));CHECK(history.begin_frame(11));av=history.prepare(reused,ids.data(),3);
    CHECK(!av.frame_valid);values=gpu.lookup_values(av,ids);
    // Even physically written duplicate-submission data is not published.
    // frame_valid is an additional whole-entry gate used by the consumer.
    CHECK(!av.frame_valid&&history.last_reason()==MotionReset::frame_gap);
    for(auto &v:values)CHECK(!v.id);
    std::puts("PASS per-ID missed-frame invalidity, changed domains, source-frame gaps, slot generations and duplicate-submission rejection.");
    // A caller must not use an unpublished source frame as previous data.
    history.reset();CHECK(history.stats().retained_bytes==0);CHECK(history.begin_frame(1));
    av=history.prepare(a,ids.data(),3);so=gpu.run(ids,a1);CHECK(history.capture(a,so.view.Get(),so.count));
    CHECK(history.begin_frame(2));av=history.prepare(a,ids.data(),3);CHECK(!av.frame_valid);
    // Nonfinite post-VSH outputs carry an explicit invalid bit on the GPU.
    so=gpu.run(ids,a1,nullptr,true);CHECK(history.capture(a,so.view.Get(),so.count));CHECK(history.complete_frame(2));
    CHECK(history.begin_frame(3));av=history.prepare(a,ids.data(),3);values=gpu.lookup_values(av,ids);
    CHECK(!values[0].id&&values[1].id&&values[2].id);CHECK(history.complete_frame(3));
    MotionHistory limited(gpu.device.Get(),gpu.context.Get(),204);CHECK(limited.ready());
    CHECK(limited.begin_frame(1));av=limited.prepare(a,ids.data(),3);CHECK(av.positions);
    so=gpu.run(ids,a1);CHECK(limited.capture(a,so.view.Get(),so.count)&&limited.complete_frame(1));
    CHECK(limited.begin_frame(2));bv=limited.prepare(b,ids.data(),3);CHECK(bv.positions&&!bv.frame_valid);
    av=limited.prepare(a,ids.data(),3);CHECK(!av.positions&&av.reason==MotionReset::budget_exhausted);
    CHECK(limited.stats().retained_bytes<=204&&limited.stats().entries==1&&limited.stats().evictions==1);
    CHECK(limited.capture(b,so.view.Get(),so.count)&&limited.complete_frame(2));
    CHECK(limited.begin_frame(3));av=limited.prepare(a,ids.data(),3);CHECK(av.positions&&!av.frame_valid);
    CHECK(limited.stats().evictions==2&&limited.stats().retained_bytes==204);
    MotionHistory excessive(gpu.device.Get(),gpu.context.Get(),MotionHistory::max_budget+1);CHECK(!excessive.ready());
    auto unknown=a;unknown.generation=0;av=history.prepare(unknown,ids.data(),3);CHECK(!av.positions);
    // Generic compiled-command identities have an explicit lifetime kind and
    // no actor slot. This does not authorize the withdrawn retail PB adapter.
    history.reset();CHECK(history.begin_frame(1));auto compiled=a;
    compiled.kind=MotionIdentityKind::static_pushbuffer;compiled.live_slot=UINT32_MAX;
    compiled.part=compiled.subdraw=0x90000;
    av=history.prepare(compiled,ids.data(),3);CHECK(av.positions&&!av.frame_valid);
    so=gpu.run(ids,a1);CHECK(history.capture(compiled,so.view.Get(),so.count)&&history.complete_frame(1));
    CHECK(history.begin_frame(2));av=history.prepare(compiled,ids.data(),3);CHECK(av.frame_valid);
    values=gpu.lookup_values(av,ids);for(unsigned i=0;i<3;++i)CHECK(expected(values[i],ids[i],a1));
    auto malformed=compiled;malformed.subdraw+=4;CHECK(!history.prepare(malformed,ids.data(),3).positions);
    auto generation=compiled;++generation.generation;
    av=history.prepare(generation,ids.data(),3);CHECK(av.positions&&!av.frame_valid&&av.reason==MotionReset::slot_reused);
    std::puts("PASS unpublished frames, nonfinite vertices, bounded LRU eviction and active-frame budget rejection.");
    std::puts("GPU position-history component passed; production contains no Map/GetData/Flush/wait or readback path.");
}
