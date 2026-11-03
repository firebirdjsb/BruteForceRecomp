#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "frame_interpolation.h"
#include "shaders.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <stdexcept>
#include <cstdio>
#include <array>
#include <cstring>
namespace bf::graphics {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT result,const char *operation) {
    if(FAILED(result)){char text[160];std::snprintf(text,sizeof text,"Interpolation %s failed: %08lX",operation,(unsigned long)result);throw std::runtime_error(text);}
}
constexpr auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
void transition(ID3D12GraphicsCommandList *commands,ID3D12Resource *image,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};commands->ResourceBarrier(1,&barrier);
}
}
struct FrameInterpolation::Impl {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> flow,warp;ComPtr<ID3D12DescriptorHeap> heap;
    std::array<ComPtr<ID3D12Resource>,2> history;
    ComPtr<ID3D12Resource> vectors,errors,output;
    unsigned width{},height{},columns{},rows{},stride{},current{};bool loaded{},flow_valid{};
    bool novel{};uint64_t generated_sequence{};float generated_phase{};
    InterpolationTimeline timeline;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(unsigned slot) {auto p=heap->GetCPUDescriptorHandleForHeapStart();p.ptr+=SIZE_T(slot)*stride;return p;}
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(unsigned slot) {auto p=heap->GetGPUDescriptorHandleForHeapStart();p.ptr+=UINT64(slot)*stride;return p;}
    ComPtr<ID3D12Resource> texture(unsigned w,unsigned h,DXGI_FORMAT format,bool writable) {
        D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;memory.CreationNodeMask=memory.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;
        desc.Flags=writable?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> image;check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&desc,readable,nullptr,IID_PPV_ARGS(&image)),"texture");return image;
    }
    void srv(ID3D12Resource *image,unsigned slot) {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=image->GetDesc().Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels=1;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;device->CreateShaderResourceView(image,&view,cpu(slot));
    }
    void uav(ID3D12Resource *image,unsigned slot) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=image->GetDesc().Format;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(image,nullptr,&view,cpu(slot));
    }
    void initialize(ID3D12Device *api,unsigned w,unsigned h) {
        device=api;width=w;height=h;columns=(w+7)/8;rows=(h+7)/8;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=8;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)),"descriptor heap");stride=device->GetDescriptorHandleIncrementSize(desc.Type);
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,0}};
        D3D12_ROOT_PARAMETER parameters[3]{};
        for(unsigned i=0;i<2;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i].DescriptorTable={1,&ranges[i]};}
        parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[2].Constants={0,0,8};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC signature{3,parameters,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,message;
        check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&message),"root serialization");
        check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"root signature");
        for(unsigned i=0;i<2;++i) {
            D3D_SHADER_MACRO defines[]={{"BF_FLOW","1"},{nullptr,nullptr}};ComPtr<ID3DBlob> shader,error;
            HRESULT result=D3DCompile(shaders::interpolation,sizeof(shaders::interpolation)-1,"BruteForce-interpolation",i?nullptr:defines,nullptr,i?"warpMain":"flowMain","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&error);
            if(FAILED(result)&&error)std::fprintf(stderr,"[INTERPOLATION] %s\n",(const char*)error->GetBufferPointer());check(result,"shader compile");
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root.Get();pipeline.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
            auto &target=i?warp:flow;check(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&target)),"pipeline");
        }
        for(auto &image:history)image=texture(w,h,DXGI_FORMAT_B8G8R8A8_UNORM,false);
        vectors=texture(columns,rows,DXGI_FORMAT_R32G32B32A32_FLOAT,true);errors=texture(columns,rows,DXGI_FORMAT_R32G32_FLOAT,true);
        output=texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true);srv(vectors.Get(),2);srv(errors.Get(),3);uav(vectors.Get(),4);uav(errors.Get(),5);uav(output.Get(),6);uav(output.Get(),7);
    }
    ID3D12Resource *record(ID3D12GraphicsCommandList *commands,ID3D12Resource *source,FrameStamp stamp) {
        novel=false;
        const auto step=timeline.advance(stamp);
        if(step.reset){loaded=false;flow_valid=false;generated_sequence=0;}
        if(!stamp.output_fps)return nullptr;
        if(step.upload) {
            current=loaded?1-current:0;
            transition(commands,history[current].Get(),readable,D3D12_RESOURCE_STATE_COPY_DEST);
            commands->CopyResource(history[current].Get(),source);
            transition(commands,history[current].Get(),D3D12_RESOURCE_STATE_COPY_DEST,readable);loaded=true;flow_valid=false;
        }
        if(!loaded)return nullptr;
        if(!step.pair||step.alpha>=1)return history[current].Get();
        if(step.alpha<=0)return history[1-current].Get();
        srv(history[1-current].Get(),0);srv(history[current].Get(),1);
        ID3D12DescriptorHeap *heaps[]={heap.Get()};commands->SetDescriptorHeaps(1,heaps);commands->SetComputeRootSignature(root.Get());
        std::array<uint32_t,8> constants{width,height,columns,rows};static_assert(sizeof(float)==sizeof(uint32_t));std::memcpy(&constants[4],&step.alpha,4);
        commands->SetComputeRootDescriptorTable(0,gpu(0));commands->SetComputeRoot32BitConstants(2,8,constants.data(),0);
        if(!flow_valid) {
            transition(commands,vectors.Get(),readable,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(commands,errors.Get(),readable,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands->SetPipelineState(flow.Get());commands->SetComputeRootDescriptorTable(1,gpu(4));commands->Dispatch((columns+7)/8,(rows+7)/8,1);
            transition(commands,vectors.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,readable);transition(commands,errors.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,readable);flow_valid=true;
        }
        commands->SetPipelineState(warp.Get());commands->SetComputeRootDescriptorTable(1,gpu(6));
        transition(commands,output.Get(),readable,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);commands->Dispatch((width+7)/8,(height+7)/8,1);
        transition(commands,output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,readable);
        novel=generated_sequence!=stamp.sequence||generated_phase!=step.alpha;generated_sequence=stamp.sequence;generated_phase=step.alpha;
        return output.Get();
    }
};
FrameInterpolation::FrameInterpolation(ID3D12Device *device,unsigned w,unsigned h):impl_(std::make_unique<Impl>()){impl_->initialize(device,w,h);}
FrameInterpolation::~FrameInterpolation()=default;
void FrameInterpolation::reset(){impl_->timeline.clear();impl_->loaded=impl_->flow_valid=impl_->novel=false;impl_->generated_sequence=0;}
bool FrameInterpolation::generated() const{return impl_->novel;}
ID3D12Resource *FrameInterpolation::record(ID3D12GraphicsCommandList *commands,ID3D12Resource *source,FrameStamp stamp){return impl_->record(commands,source,stamp);}
}
