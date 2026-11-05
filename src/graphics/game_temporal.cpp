#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "game_temporal.h"
#include "gpu/scene_device.h"
#include <d3d11on12.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef BF_HAS_FSR_TEMPORAL
#include "fsr_temporal.h"
#endif

namespace bf::graphics {
Dx12TextureFrame GameTemporalFrame::presentation() const {
    Dx12TextureFrame frame{};frame.texture=color.Get();frame.stamp.sequence=sequence;frame.source_color_va=color_va;
    if(hud) {frame.overlay.texture=hud.Get();frame.overlay.sequence=sequence;frame.overlay.color_va=color_va;}
    return frame;
}
namespace {
using Microsoft::WRL::ComPtr;
std::string status="AMD temporal SDK is not configured";
#ifdef BF_HAS_FSR_TEMPORAL
void require(bool value,const char *why) {if(!value)throw std::runtime_error(why);}
void check(HRESULT result,const char *why) {
    if(SUCCEEDED(result))return;
    char detail[256];std::snprintf(detail,sizeof detail,"%s (0x%08lX)",why,(unsigned long)result);throw std::runtime_error(detail);
}
void barrier(ID3D12GraphicsCommandList *commands,ID3D12Resource *resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    if(from==to)return;
    D3D12_RESOURCE_BARRIER transition{};transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};commands->ResourceBarrier(1,&transition);
}
struct Slot {
    ComPtr<ID3D12Resource> output,hud;
    std::weak_ptr<const GameTemporalFrame> lease;
};
struct Processor {
    DWORD thread=GetCurrentThreadId();
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context;
    std::shared_ptr<Dx12DeviceContext> owner;
    std::unique_ptr<FsrTemporal> fsr;
    FsrOptions options{};
    std::vector<Slot> slots;
    std::shared_ptr<const GameTemporalFrame> latest;
    uint64_t last_sequence{},issued_sequence{};
    float issued_x{},issued_y{};
    bool reset_next=true;
    std::string provider;
    std::vector<FsrVersion> available;

    void on_thread() const {require(GetCurrentThreadId()==thread,"Temporal processor called outside its owning render thread");}
    ComPtr<ID3D12Resource> create_texture(UINT width,UINT height,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags) const {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;desc.Flags=flags;
        ComPtr<ID3D12Resource> texture;
        check(owner->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture)),"Create temporal output");
        return texture;
    }
    void validate_texture(ID3D11Texture2D *texture,UINT width,UINT height,const char *why) const {
        require(texture!=nullptr,why);ComPtr<ID3D11Device> device;texture->GetDevice(&device);
        require(device.Get()==device11.Get(),"Temporal input belongs to another scene device");
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        require(desc.Width==width&&desc.Height==height&&desc.MipLevels==1&&desc.ArraySize==1&&desc.SampleDesc.Count==1&&
            desc.SampleDesc.Quality==0&&desc.Usage==D3D11_USAGE_DEFAULT&&(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE),why);
    }
    void validate(const BfTemporalInputs &input) const {
        constexpr uint32_t required=BF_TEMPORAL_COLOR_BEFORE_UI|BF_TEMPORAL_DEPTH_COHERENT|BF_TEMPORAL_MOTION_COMPLETE|
            BF_TEMPORAL_PROJECTION_VERIFIED|BF_TEMPORAL_JITTER_APPLIED|BF_TEMPORAL_SOURCE_COMPLETE;
        require((input.provenance&required)==required,"Temporal source lacks independently verified scene/depth/motion/projection/jitter provenance");
        require(input.sequence&&input.scene&&input.color_va&&input.sequence>last_sequence,"Temporal source identity is missing or repeated");
        require(issued_sequence==input.sequence&&input.jitter_x==issued_x&&input.jitter_y==issued_y,"Temporal source did not apply its issued SDK jitter");
        require(std::isfinite(input.elapsed_ms)&&input.elapsed_ms>0&&input.elapsed_ms<=1000&&std::isfinite(input.near_plane)&&input.near_plane>0&&
            std::isfinite(input.far_plane)&&input.far_plane>input.near_plane&&std::isfinite(input.vertical_fov)&&input.vertical_fov>0&&input.vertical_fov<3.14f,
            "Temporal source camera/frame time is invalid");
        validate_texture(input.color,options.render_width,options.render_height,"Temporal scene colour is missing or incompatible");
        validate_texture(input.depth,options.render_width,options.render_height,"Temporal actual depth is missing or incompatible");
        validate_texture(input.motion,options.render_width,options.render_height,"Temporal actual motion is missing or incompatible");
        D3D11_TEXTURE2D_DESC color{},depth{},motion{};input.color->GetDesc(&color);input.depth->GetDesc(&depth);input.motion->GetDesc(&motion);
        require(color.Format==DXGI_FORMAT_B8G8R8A8_UNORM||color.Format==DXGI_FORMAT_R8G8B8A8_UNORM||
            color.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||color.Format==DXGI_FORMAT_R32G32B32A32_FLOAT,"Temporal scene colour format is unsupported");
        require(depth.Format==DXGI_FORMAT_R32_FLOAT,"Temporal depth must be actual normalized R32_FLOAT");
        require(motion.Format==DXGI_FORMAT_R16G16_FLOAT||motion.Format==DXGI_FORMAT_R32G32_FLOAT,"Temporal motion must be actual previous-minus-current RG pixel motion");
        require((input.reactive==nullptr)==(input.composition==nullptr),"Temporal masks must be a coherent pair");
        if(input.reactive) {
            require((input.provenance&BF_TEMPORAL_MASKS_COHERENT)!=0,"Temporal material masks are not coherent with the source");
            for(auto *mask:{input.reactive,input.composition}) {
                validate_texture(mask,options.render_width,options.render_height,"Temporal material mask is incompatible");
                D3D11_TEXTURE2D_DESC desc{};mask->GetDesc(&desc);require(desc.Format==DXGI_FORMAT_R8_UNORM,"Temporal material masks must be R8_UNORM");
            }
        } else require((input.provenance&BF_TEMPORAL_OPAQUE_SCENE)!=0,"Temporal transparent/reactive scene requires actual material masks");
        if(input.hud) {
            require(input.hud_sequence==input.sequence&&input.hud_color_va==input.color_va,"Native HUD belongs to a different source surface/generation");
            validate_texture(input.hud,options.render_width,options.render_height,"Native HUD dimensions/device are incompatible");
            D3D11_TEXTURE2D_DESC desc{};input.hud->GetDesc(&desc);require(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT,"Native HUD must contain premultiplied FP16 coverage");
        }
        std::array<ID3D11Texture2D *,6> textures={input.color,input.depth,input.motion,input.reactive,input.composition,input.hud};
        for(size_t i=0;i<textures.size();++i)if(textures[i])for(size_t j=0;j<i;++j)require(textures[i]!=textures[j],"Temporal scene attachments alias");
    }
    Slot &acquire_slot(bool hud) {
        for(auto &slot:slots)if(slot.lease.expired()) {
            if(hud&&!slot.hud)slot.hud=create_texture(options.render_width,options.render_height,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_NONE);
            return slot;
        }
        require(slots.size()<3,"All temporal output leases are still retained; original source must be presented");
        Slot slot;slot.output=create_texture(options.output_width,options.output_height,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(hud)slot.hud=create_texture(options.render_width,options.render_height,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_NONE);
        slots.push_back(std::move(slot));return slots.back();
    }
};
Processor *processor{}; // Explicit lifecycle; no COM/SDK object destroyed at atexit.
struct Dispatch {
    Processor *processor;
    const BfTemporalInputs *input;
    Slot *slot;
    UINT reactive=UINT_MAX,composition=UINT_MAX,hud=UINT_MAX;
};
HRESULT record(ID3D12Device *device,ID3D12CommandQueue *queue,ID3D12GraphicsCommandList *commands,
    ID3D12Resource *const *resources,UINT count,void *user) {
    auto &work=*static_cast<Dispatch *>(user);auto &state=*work.processor;const auto &input=*work.input;
    require(device==state.owner->device.Get()&&queue==state.owner->queue.Get(),"Temporal bridge and presenter do not share native device/queue");
    require(count>=3,"Temporal native attachments are incomplete");
    // Scene bridge resources enter and leave COMMON. The SDK requires all
    // actual inputs in NON_PIXEL_SHADER_RESOURCE and its real output in UAV.
    for(UINT index=0;index<count;++index)if(index!=work.hud)
        barrier(commands,resources[index],D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrier(commands,work.slot->output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TemporalSceneFrame frame{};frame.color=resources[0];frame.depth=resources[1];frame.motion=resources[2];
    if(work.reactive!=UINT_MAX)frame.reactive=resources[work.reactive];
    if(work.composition!=UINT_MAX)frame.composition=resources[work.composition];
    frame.sequence=input.sequence;frame.scene=input.scene;frame.jitter_x=input.jitter_x;frame.jitter_y=input.jitter_y;
    frame.elapsed_ms=input.elapsed_ms;frame.near_plane=input.near_plane;frame.far_plane=input.far_plane;frame.vertical_fov=input.vertical_fov;
    frame.reset=input.reset!=0||state.reset_next;
    state.fsr->record(commands,frame,work.slot->output.Get());
    barrier(commands,work.slot->output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    for(UINT index=0;index<count;++index)if(index!=work.hud)
        barrier(commands,resources[index],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    if(work.hud!=UINT_MAX) {
        barrier(commands,resources[work.hud],D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(commands,work.slot->hud.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyResource(work.slot->hud.Get(),resources[work.hud]);
        barrier(commands,work.slot->hud.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        barrier(commands,resources[work.hud],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    }
    return S_OK;
}
#endif
}

std::shared_ptr<const GameTemporalFrame> game_temporal_take_frame(uint64_t sequence,uint32_t color_va) {
#ifdef BF_HAS_FSR_TEMPORAL
    if(!processor||processor->thread!=GetCurrentThreadId()||!processor->latest||processor->latest->sequence!=sequence||processor->latest->color_va!=color_va)return {};
    return processor->latest;
#else
    (void)sequence;(void)color_va;return {};
#endif
}
std::shared_ptr<const GameTemporalFrame> game_temporal_latest_frame(uint32_t color_va) {
#ifdef BF_HAS_FSR_TEMPORAL
    if(!processor||processor->thread!=GetCurrentThreadId()||!processor->latest||processor->latest->color_va!=color_va)return {};
    return processor->latest;
#else
    (void)color_va;return {};
#endif
}
}

extern "C" {
int bf_game_temporal_available(void) {
#ifdef BF_HAS_FSR_TEMPORAL
    return 1;
#else
    return 0;
#endif
}
int bf_game_temporal_configure(const BfGameTemporalConfig *config) {
    using namespace bf::graphics;
#ifdef BF_HAS_FSR_TEMPORAL
    if(processor&&processor->thread!=GetCurrentThreadId()) {status="Temporal configuration called outside its owning render thread";return 0;}
    try {
        require(config&&config->device&&config->context&&config->dll_directory,"Temporal configuration is incomplete");
        if(processor)processor->on_thread();
        auto candidate=std::make_unique<Processor>();candidate->device11=config->device;candidate->context=config->context;
        require(config->context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"Temporal scene requires an immediate context");
        ComPtr<ID3D11Device> owner11;config->context->GetDevice(&owner11);require(owner11.Get()==config->device,"Temporal context belongs to another device");
        ComPtr<ID3D11On12Device1> bridge;check(candidate->device11.As(&bridge),"Temporal scene requires D3D11On12");
        ComPtr<ID3D12Device> native;check(bridge->GetD3D12Device(IID_PPV_ARGS(&native)),"Query actual native scene device");
        ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Query scene adapter factory");
        ComPtr<IDXGIAdapter1> adapter;check(factory->EnumAdapterByLuid(native->GetAdapterLuid(),IID_PPV_ARGS(&adapter)),"Query actual scene adapter");
        DXGI_ADAPTER_DESC1 desc{};check(adapter->GetDesc1(&desc),"Describe actual scene adapter");
        candidate->owner=acquire_dx12_device((desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0);
        require(candidate->owner->device.Get()==native.Get(),"Temporal scene is outside the shared device owner");
        const std::filesystem::path directory(config->dll_directory);require(directory.is_absolute()&&std::filesystem::is_directory(directory),"Temporal SDK directory must be an existing absolute path");
        candidate->fsr=std::make_unique<FsrTemporal>(native.Get(),candidate->owner->queue.Get(),directory);
        candidate->options.render_width=config->render_width;candidate->options.render_height=config->render_height;
        candidate->options.output_width=config->output_width;candidate->options.output_height=config->output_height;
        candidate->options.upscale_version=config->provider;candidate->options.inverted_depth=config->inverted_depth!=0;candidate->options.nonlinear_color=config->nonlinear_color!=0;
        candidate->available=candidate->fsr->versions(false);
        candidate->fsr->configure(candidate->options);candidate->provider=candidate->fsr->upscale_name();
        delete processor;processor=candidate.release();status="AMD temporal super resolution configured; awaiting verified source";return 1;
    }catch(const std::exception &error){status=error.what();delete processor;processor=nullptr;return 0;}
    catch(...){status="Temporal configuration failed";delete processor;processor=nullptr;return 0;}
#else
    (void)config;status="AMD temporal SDK was not included in this build";return 0;
#endif
}
int bf_game_temporal_ready(void) {
#ifdef BF_HAS_FSR_TEMPORAL
    return bf::graphics::processor&&bf::graphics::processor->thread==GetCurrentThreadId();
#else
    return 0;
#endif
}
int bf_game_temporal_next_jitter(uint64_t sequence,float *x,float *y) {
    using namespace bf::graphics;
#ifdef BF_HAS_FSR_TEMPORAL
    try {
        require(processor&&x&&y,"Temporal jitter requires a configured processor and outputs");processor->on_thread();
        require(sequence&&sequence>processor->last_sequence,"Temporal jitter requires a new actual source sequence");
        if(processor->issued_sequence!=sequence) {
            if(processor->issued_sequence)processor->reset_next=true;
            processor->fsr->jitter(static_cast<unsigned>(sequence),processor->issued_x,processor->issued_y);processor->issued_sequence=sequence;
        }
        *x=processor->issued_x;*y=processor->issued_y;return 1;
    }catch(const std::exception &error){status=error.what();return 0;}
    catch(...){status="Temporal jitter query failed";return 0;}
#else
    (void)sequence;(void)x;(void)y;status="AMD temporal SDK was not included in this build";return 0;
#endif
}
int bf_game_temporal_submit(const BfTemporalInputs *input) {
    using namespace bf::graphics;
#ifdef BF_HAS_FSR_TEMPORAL
    if(processor&&processor->thread!=GetCurrentThreadId()) {status="Temporal submit called outside its owning render thread";return 0;}
    try {
        require(processor&&input,"Temporal source requires a configured processor");auto &state=*processor;state.on_thread();state.validate(*input);
        auto &slot=state.acquire_slot(input->hud!=nullptr);Dispatch work{&state,input,&slot};
        std::vector<ID3D11Resource *> resources={input->color,input->depth,input->motion};
        if(input->reactive){work.reactive=UINT(resources.size());resources.push_back(input->reactive);work.composition=UINT(resources.size());resources.push_back(input->composition);}
        if(input->hud){work.hud=UINT(resources.size());resources.push_back(input->hud);}
        check(bf_scene_d3d12_access(state.context.Get(),resources.data(),UINT(resources.size()),record,&work),"Temporal native resource dispatch");
        auto frame=std::make_shared<GameTemporalFrame>();frame->sequence=input->sequence;frame->scene=input->scene;frame->color_va=input->color_va;
        frame->width=state.options.output_width;frame->height=state.options.output_height;frame->color=slot.output;frame->owner=state.owner;
        frame->provider=state.provider;
        if(input->hud)frame->hud=slot.hud;slot.lease=frame;state.latest=std::move(frame);
        state.last_sequence=input->sequence;state.issued_sequence=0;state.reset_next=false;status="AMD temporal super resolution dispatched from verified source";return 1;
    }catch(const std::exception &error){status=error.what();if(processor){processor->reset_next=true;processor->issued_sequence=0;processor->latest.reset();}return 0;}
    catch(...){status="Temporal source dispatch failed";if(processor){processor->reset_next=true;processor->issued_sequence=0;processor->latest.reset();}return 0;}
#else
    (void)input;status="AMD temporal SDK was not included in this build";return 0;
#endif
}
const char *bf_game_temporal_provider(void) {
#ifdef BF_HAS_FSR_TEMPORAL
    return bf::graphics::processor?bf::graphics::processor->provider.c_str():"Unavailable";
#else
    return "Unavailable";
#endif
}
uint32_t bf_game_temporal_provider_count(void) {
#ifdef BF_HAS_FSR_TEMPORAL
    const auto *state=bf::graphics::processor;
    return state&&state->thread==GetCurrentThreadId()?static_cast<uint32_t>(state->available.size()):0;
#else
    return 0;
#endif
}
int bf_game_temporal_provider_at(uint32_t index,uint64_t *id,char *label,uint32_t capacity) {
#ifdef BF_HAS_FSR_TEMPORAL
    const auto *state=bf::graphics::processor;
    if(!state||state->thread!=GetCurrentThreadId()||index>=state->available.size()||!id||!label||!capacity)return 0;
    const auto &provider=state->available[index];if(provider.name.size()>=capacity)return 0;
    *id=provider.id;std::memcpy(label,provider.name.c_str(),provider.name.size()+1);return 1;
#else
    (void)index;(void)id;(void)label;(void)capacity;return 0;
#endif
}
const char *bf_game_temporal_status(void) {return bf::graphics::status.c_str();}
void bf_game_temporal_shutdown(void) {
#ifdef BF_HAS_FSR_TEMPORAL
    delete bf::graphics::processor;bf::graphics::processor=nullptr;
#endif
    bf::graphics::status="AMD temporal processor is stopped";
}
}
