#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "fsr_temporal.h"
#include <ffx_api_loader.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_upscale.h>
#include <ffx_framegeneration.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace bf::graphics {
namespace {
using Microsoft::WRL::ComPtr;
void require(bool ok, const char *why) { if(!ok) throw std::runtime_error(why); }
void check(ffxReturnCode_t code, const char *operation) {
    if(code) { char message[180]; std::snprintf(message,sizeof message,"FSR %s failed: %u",operation,code); throw std::runtime_error(message); }
}
void message(uint32_t type,const wchar_t *text) { std::fwprintf(stderr,L"[FSR %s] %s\n",type?L"warning":L"error",text); }
bool color_format(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R8G8B8A8_UNORM || f==DXGI_FORMAT_R16G16B16A16_FLOAT || f==DXGI_FORMAT_R32G32B32A32_FLOAT;
}
}
struct FsrTemporal::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    HMODULE module{};
    ffxFunctions api{};
    ffxContext upscale{},generation{};
    FsrOptions options{};
    uint64_t last_sequence{},last_scene{};
    bool has_history{};
    std::string upscale_version,generation_version;
    void idle() {
        ComPtr<ID3D12Fence> fence;
        require(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))),"FSR shutdown fence creation failed");
        require(SUCCEEDED(queue->Signal(fence.Get(),1)),"FSR shutdown signal failed");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        require(event!=nullptr,"FSR shutdown event creation failed");
        const HRESULT result=fence->SetEventOnCompletion(1,event);
        const DWORD waited=SUCCEEDED(result)?WaitForSingleObject(event,30000):WAIT_FAILED;
        CloseHandle(event);
        require(waited==WAIT_OBJECT_0 && SUCCEEDED(device->GetDeviceRemovedReason()),"FSR GPU completion failed");
    }
    void destroy() {
        if(upscale||generation) idle();
        if(generation) check(api.DestroyContext(&generation,nullptr),"destroy frame generation");
        if(upscale) check(api.DestroyContext(&upscale,nullptr),"destroy upscaler");
        has_history=false;
    }
    ~Impl() {
        try { destroy(); } catch(const std::exception &e) {
            // Do not unload code that may still be referenced by outstanding
            // GPU/driver work after a device failure. Windows reclaims it at exit.
            std::fprintf(stderr,"[FSR] shutdown: %s\n",e.what()); return;
        }
        if(module) FreeLibrary(module);
    }
    std::vector<FsrVersion> versions(bool fg) const {
        ffxQueryDescGetVersions query{};
        query.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        query.createDescType=fg?FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION:FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        query.device=device.Get(); uint64_t count=0; query.outputCount=&count;
        check(api.Query(nullptr,&query.header),"enumerate version count");
        require(count<=256,"FSR returned an invalid version count");
        std::vector<uint64_t> ids(count); std::vector<const char *> names(count);
        query.versionIds=ids.data(); query.versionNames=names.data();
        check(api.Query(nullptr,&query.header),"enumerate versions");
        require(count<=ids.size(),"FSR version enumeration grew unexpectedly");
        std::vector<FsrVersion> result;
        for(size_t i=0;i<count;++i)result.push_back({ids[i],names[i]?names[i]:"unknown"});
        return result;
    }
    std::string provider(ffxContext &context) const {
        ffxQueryGetProviderVersion query{}; query.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        check(api.Query(&context,&query.header),"query selected implementation");
        require(query.versionName!=nullptr,"FSR did not report its selected implementation");
        return query.versionName;
    }
    void texture(ID3D12Resource *resource,unsigned w,unsigned h,const char *why,bool output=false) const {
        require(resource!=nullptr,why); auto desc=resource->GetDesc();
        ComPtr<ID3D12Device> owner; require(SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&owner)))&&owner.Get()==device.Get(),"FSR resource belongs to another device");
        require(desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&desc.Width==w&&desc.Height==h&&desc.DepthOrArraySize==1&&desc.MipLevels==1&&desc.SampleDesc.Count==1,why);
        require(!(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),why);
        if(output)require((desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)!=0&&color_format(desc.Format),why);
    }
};
FsrTemporal::FsrTemporal(ID3D12Device *device,ID3D12CommandQueue *queue,const std::filesystem::path &directory):impl_(std::make_unique<Impl>()) {
    require(device&&queue,"FSR requires a D3D12 device and queue");
    ComPtr<ID3D12Device> owner; require(SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&owner)))&&owner.Get()==device,"FSR queue belongs to another device");
    require(queue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,"FSR requires a direct queue");
    impl_->device=device;impl_->queue=queue;
    const auto path=std::filesystem::absolute(directory/L"amd_fidelityfx_loader_dx12.dll");
    impl_->module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    require(impl_->module!=nullptr,"AMD FSR loader DLL unavailable");
    ffxLoadFunctions(&impl_->api,impl_->module);
    require(impl_->api.CreateContext&&impl_->api.DestroyContext&&impl_->api.Configure&&impl_->api.Query&&impl_->api.Dispatch,"AMD FSR loader exports incomplete");
}
FsrTemporal::~FsrTemporal()=default;
std::vector<FsrVersion> FsrTemporal::versions(bool fg) const { return impl_->versions(fg); }
void FsrTemporal::configure(const FsrOptions &o) {
    require(o.render_width&&o.render_height&&o.output_width>=o.render_width&&o.output_height>=o.render_height&&o.output_width<=16384&&o.output_height<=16384,"Invalid FSR render/output dimensions");
    // Pin only IDs actually supported on this device; never silently label an
    // analytical fallback as the ML implementation the user requested.
    for(bool fg:{false,true}) {
        const auto id=fg?o.frame_generation_version:o.upscale_version;
        if(id&&(!fg||o.frame_generation)) {
            const auto available=versions(fg);
            require(std::any_of(available.begin(),available.end(),[&](const auto &v){return v.id==id;}),"Requested FSR implementation is unavailable on this device");
        }
    }
    auto &s=*impl_; s.destroy(); s.options=o;
    ffxCreateBackendDX12Desc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=s.device.Get();
    ffxOverrideVersion override{};override.header={FFX_API_DESC_TYPE_OVERRIDE_VERSION,&backend.header};override.versionId=o.upscale_version;
    ffxCreateContextDescUpscaleVersion version{};version.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION,o.upscale_version?&override.header:&backend.header};version.version=FFX_UPSCALER_VERSION;
    ffxCreateContextDescUpscale create{};create.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,&version.header};
    create.maxRenderSize={o.render_width,o.render_height};create.maxUpscaleSize={o.output_width,o.output_height};
    create.flags=FFX_UPSCALE_ENABLE_AUTO_EXPOSURE|FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    if(o.inverted_depth)create.flags|=FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
    if(o.nonlinear_color)create.flags|=FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
    create.fpMessage=message;
    try {
        check(s.api.CreateContext(&s.upscale,&create.header,nullptr),"create upscaler");s.upscale_version=s.provider(s.upscale);
        if(o.frame_generation) {
            override.versionId=o.frame_generation_version;
            ffxCreateContextDescFrameGenerationVersion fgVersion{};
            fgVersion.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION,o.frame_generation_version?&override.header:&backend.header};fgVersion.version=FFX_FRAMEGENERATION_VERSION;
            ffxCreateContextDescFrameGeneration fg{};fg.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&fgVersion.header};
            fg.maxRenderSize=create.maxRenderSize;fg.displaySize=create.maxUpscaleSize;fg.backBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
            fg.flags=FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
            if(o.inverted_depth)fg.flags|=FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
            check(s.api.CreateContext(&s.generation,&fg.header,nullptr),"create frame generation");s.generation_version=s.provider(s.generation);
        } else s.generation_version.clear();
    } catch(...) { s.destroy(); throw; }
}
std::string FsrTemporal::upscale_name() const {return impl_->upscale_version;}
std::string FsrTemporal::frame_generation_name() const {return impl_->generation_version;}
void FsrTemporal::jitter(unsigned sequence,float &x,float &y) const {
    auto &s=*impl_;require(s.upscale!=nullptr,"FSR not configured");int32_t count=0;
    ffxQueryDescUpscaleGetJitterPhaseCount phases{};phases.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    phases.renderWidth=s.options.render_width;phases.displayWidth=s.options.output_width;phases.pOutPhaseCount=&count;
    check(s.api.Query(&s.upscale,&phases.header),"jitter phase count");require(count>0,"Invalid FSR jitter phase count");
    ffxQueryDescUpscaleGetJitterOffset offset{};offset.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
    offset.index=int32_t(sequence%unsigned(count));offset.phaseCount=count;offset.pOutX=&x;offset.pOutY=&y;
    check(s.api.Query(&s.upscale,&offset.header),"jitter offset");
}
void FsrTemporal::record(ID3D12GraphicsCommandList *commands,const TemporalSceneFrame &f,ID3D12Resource *output,ID3D12Resource *generated) {
    auto &s=*impl_;const auto &o=s.options;
    require(commands&&s.upscale,"FSR command list/context unavailable");
    ComPtr<ID3D12Device> owner;require(SUCCEEDED(commands->GetDevice(IID_PPV_ARGS(&owner)))&&owner.Get()==s.device.Get(),"FSR command list belongs to another device");
    require(commands->GetType()==D3D12_COMMAND_LIST_TYPE_DIRECT,"FSR requires a direct command list");
    require(f.sequence>0&&(!s.has_history||f.sequence>s.last_sequence),"FSR requires a new rendered source frame");
    require(std::isfinite(f.jitter_x)&&std::isfinite(f.jitter_y)&&std::abs(f.jitter_x)<=.5f&&std::abs(f.jitter_y)<=.5f&&std::isfinite(f.elapsed_ms)&&f.elapsed_ms>0&&f.elapsed_ms<=1000,"Invalid FSR jitter/frame time");
    require(std::isfinite(f.near_plane)&&std::isfinite(f.far_plane)&&f.near_plane>0&&f.far_plane>f.near_plane&&std::isfinite(f.vertical_fov)&&f.vertical_fov>0&&f.vertical_fov<3.14f&&std::isfinite(f.meters_per_unit)&&f.meters_per_unit>0,"Invalid FSR camera parameters");
    for(const auto *v:{f.position,f.up,f.right,f.forward})for(unsigned i=0;i<3;++i)require(std::isfinite(v[i]),"Invalid FSR camera basis");
    s.texture(f.color,o.render_width,o.render_height,"Invalid FSR scene color");
    require(color_format(f.color->GetDesc().Format)||f.color->GetDesc().Format==DXGI_FORMAT_B8G8R8A8_UNORM,"Unsupported FSR color format");
    s.texture(f.depth,o.render_width,o.render_height,"Missing or invalid real scene depth");
    require(f.depth->GetDesc().Format==DXGI_FORMAT_R32_FLOAT,"FSR depth must be normalized R32_FLOAT");
    s.texture(f.motion,o.render_width,o.render_height,"Missing or invalid real scene motion vectors");
    require(f.motion->GetDesc().Format==DXGI_FORMAT_R16G16_FLOAT||f.motion->GetDesc().Format==DXGI_FORMAT_R32G32_FLOAT,"FSR motion must be RG float");
    s.texture(output,o.output_width,o.output_height,"Invalid FSR output",true);
    std::vector<ID3D12Resource *> resources={f.color,f.depth,f.motion,output};
    for(auto *mask:{f.reactive,f.composition})if(mask){s.texture(mask,o.render_width,o.render_height,"Invalid FSR mask");require(mask->GetDesc().Format==DXGI_FORMAT_R8_UNORM,"FSR mask must be R8_UNORM");resources.push_back(mask);}
    if(o.frame_generation){s.texture(generated,o.output_width,o.output_height,"Invalid FSR generated output",true);require(output->GetDesc().Format==DXGI_FORMAT_R8G8B8A8_UNORM&&generated->GetDesc().Format==DXGI_FORMAT_R8G8B8A8_UNORM,"Frame generation output must be RGBA8");resources.push_back(generated);}
    for(size_t i=0;i<resources.size();++i)for(size_t j=0;j<i;++j)require(resources[i]!=resources[j],"FSR input/output resources alias");
    const bool reset=f.reset||!s.has_history||f.scene!=s.last_scene||f.sequence-s.last_sequence!=1;
    ffxDispatchDescUpscale dispatch{};dispatch.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;dispatch.commandList=commands;
    dispatch.color=ffxApiGetResourceDX12(f.color);dispatch.depth=ffxApiGetResourceDX12(f.depth);dispatch.motionVectors=ffxApiGetResourceDX12(f.motion);
    dispatch.reactive=ffxApiGetResourceDX12(f.reactive);dispatch.transparencyAndComposition=ffxApiGetResourceDX12(f.composition);
    dispatch.output=ffxApiGetResourceDX12(output,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);dispatch.renderSize={o.render_width,o.render_height};dispatch.upscaleSize={o.output_width,o.output_height};
    dispatch.jitterOffset={f.jitter_x,f.jitter_y};dispatch.motionVectorScale={1,1};dispatch.frameTimeDelta=f.elapsed_ms;dispatch.preExposure=1;
    dispatch.reset=reset;dispatch.cameraNear=f.near_plane;dispatch.cameraFar=f.far_plane;dispatch.cameraFovAngleVertical=f.vertical_fov;dispatch.viewSpaceToMetersFactor=f.meters_per_unit;
    if(o.nonlinear_color)dispatch.flags=FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
    check(s.api.Dispatch(&s.upscale,&dispatch.header),"upscale dispatch");
    if(o.frame_generation) {
        ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        config.frameGenerationEnabled=true;config.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;config.frameID=f.sequence;config.generationRect={0,0,int32_t(o.output_width),int32_t(o.output_height)};
        check(s.api.Configure(&s.generation,&config.header),"configure frame generation");
        ffxDispatchDescFrameGenerationPrepareV2 prepare{};prepare.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        prepare.commandList=commands;prepare.frameID=f.sequence;prepare.renderSize=dispatch.renderSize;prepare.jitterOffset=dispatch.jitterOffset;prepare.motionVectorScale={1,1};
        prepare.frameTimeDelta=f.elapsed_ms;prepare.reset=reset;prepare.cameraNear=f.near_plane;prepare.cameraFar=f.far_plane;prepare.cameraFovAngleVertical=f.vertical_fov;prepare.viewSpaceToMetersFactor=f.meters_per_unit;
        prepare.depth=dispatch.depth;prepare.motionVectors=dispatch.motionVectors;
        std::copy_n(f.position,3,prepare.cameraPosition);std::copy_n(f.up,3,prepare.cameraUp);std::copy_n(f.right,3,prepare.cameraRight);std::copy_n(f.forward,3,prepare.cameraForward);
        check(s.api.Dispatch(&s.generation,&prepare.header),"prepare frame generation");
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};commands->ResourceBarrier(1,&barrier);
        ffxDispatchDescFrameGeneration fg{};fg.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;fg.commandList=commands;
        fg.presentColor=ffxApiGetResourceDX12(output);fg.outputs[0]=ffxApiGetResourceDX12(generated,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        fg.numGeneratedFrames=1;fg.reset=reset;fg.frameID=f.sequence;fg.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;fg.minMaxLuminance[1]=100;fg.generationRect=config.generationRect;
        const auto code=s.api.Dispatch(&s.generation,&fg.header);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commands->ResourceBarrier(1,&barrier);
        check(code,"generate intermediate frame");
    }
    s.last_sequence=f.sequence;s.last_scene=f.scene;s.has_history=true;
}
}
