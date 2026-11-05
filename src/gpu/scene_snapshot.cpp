#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "scene_snapshot.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void check(HRESULT h,const char *why){if(FAILED(h)){char text[180];std::snprintf(text,sizeof text,"%s (%08lX)",why,(unsigned long)h);throw std::runtime_error(text);}}
constexpr char source[]=R"SS(
Texture2D<float> deviceDepth:register(t0);
RWTexture2D<float> normalizedDepth:register(u0);
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 uint w,h;normalizedDepth.GetDimensions(w,h);
 if(id.x<w&&id.y<h)normalizedDepth[id.xy]=deviceDepth.Load(int3(id.xy,0));
})SS";
struct SavedCompute {
    ID3D11DeviceContext *context;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ID3D11ClassInstance *classes[D3D11_SHADER_MAX_INTERFACES]{};
    UINT count=D3D11_SHADER_MAX_INTERFACES;
    explicit SavedCompute(ID3D11DeviceContext *c):context(c){c->CSGetShader(&shader,classes,&count);c->CSGetShaderResources(0,1,&srv);c->CSGetUnorderedAccessViews(0,1,&uav);}
    ~SavedCompute(){
        ID3D11ShaderResourceView *input=srv.Get();ID3D11UnorderedAccessView *output=uav.Get();UINT keep=UINT(-1);
        context->CSSetShaderResources(0,1,&input);context->CSSetUnorderedAccessViews(0,1,&output,&keep);
        context->CSSetShader(shader.Get(),classes,count);for(UINT i=0;i<count;++i)if(classes[i])classes[i]->Release();
    }
};
struct State {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> color,depth_source,depth,color_read,depth_read;
    ComPtr<ID3D11ShaderResourceView> color_view,depth_source_view,depth_view;
    ComPtr<ID3D11UnorderedAccessView> depth_uav;
    ComPtr<ID3D11ComputeShader> shader;
    D3D11_TEXTURE2D_DESC previous_color{},previous_depth{};
    void initialize(ID3D11Device *d,const D3D11_TEXTURE2D_DESC &c,const D3D11_TEXTURE2D_DESC &z){
        if(device.Get()==d&&previous_color.Width==c.Width&&previous_color.Height==c.Height&&previous_color.Format==c.Format&&previous_depth.Format==z.Format)return;
        *this=State{};device=d;
        ComPtr<ID3DBlob> code,errors;const auto compiled=D3DCompile(source,sizeof source-1,"scene-device-depth",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(compiled)&&errors)std::fprintf(stderr,"[SCENE] %s\n",(const char*)errors->GetBufferPointer());check(compiled,"scene depth shader compile");
        check(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"scene depth shader");
        auto desc=c;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        check(d->CreateTexture2D(&desc,nullptr,&color),"scene color snapshot");check(d->CreateShaderResourceView(color.Get(),nullptr,&color_view),"scene color view");
        desc=z;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const bool d24=z.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||z.Format==DXGI_FORMAT_R24G8_TYPELESS;
        desc.Format=d24?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_R32_TYPELESS;
        check(d->CreateTexture2D(&desc,nullptr,&depth_source),"scene depth snapshot");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=d24?DXGI_FORMAT_R24_UNORM_X8_TYPELESS:DXGI_FORMAT_R32_FLOAT;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
        check(d->CreateShaderResourceView(depth_source.Get(),&view,&depth_source_view),"scene raw depth view");
        desc.Format=DXGI_FORMAT_R32_FLOAT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        check(d->CreateTexture2D(&desc,nullptr,&depth),"scene normalized depth");check(d->CreateShaderResourceView(depth.Get(),nullptr,&depth_view),"scene normalized depth view");check(d->CreateUnorderedAccessView(depth.Get(),nullptr,&depth_uav),"scene depth output view");
        previous_color=c;previous_depth=z;
    }
    void capture(ID3D11Device *d,ID3D11DeviceContext *c,ID3D11Texture2D *input_color,ID3D11Texture2D *input_depth){
        D3D11_TEXTURE2D_DESC color_desc{},depth_desc{};input_color->GetDesc(&color_desc);input_depth->GetDesc(&depth_desc);
        require(color_desc.Width&&color_desc.Width<=4096&&color_desc.Height&&color_desc.Height<=4096&&color_desc.Width==depth_desc.Width&&color_desc.Height==depth_desc.Height,"scene color/depth dimensions differ");
        require(color_desc.MipLevels==1&&depth_desc.MipLevels==1&&color_desc.ArraySize==1&&depth_desc.ArraySize==1&&color_desc.SampleDesc.Count==1&&depth_desc.SampleDesc.Count==1,"scene snapshot requires single-sample 2D targets");
        require(color_desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||color_desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM,"unsupported scene color format");
        require(depth_desc.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||depth_desc.Format==DXGI_FORMAT_R24G8_TYPELESS||depth_desc.Format==DXGI_FORMAT_D32_FLOAT||depth_desc.Format==DXGI_FORMAT_R32_TYPELESS,"unsupported device depth format");
        for(auto *resource:{input_color,input_depth}){ComPtr<ID3D11Device> owner;resource->GetDevice(&owner);require(owner.Get()==d,"scene target belongs to another device");}
        ComPtr<ID3D11Device> owner;c->GetDevice(&owner);require(owner.Get()==d&&c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"scene capture requires the owning immediate context");
        initialize(d,color_desc,depth_desc);
        c->CopyResource(color.Get(),input_color);c->CopyResource(depth_source.Get(),input_depth);
        SavedCompute saved(c);
        auto *input=depth_source_view.Get();auto *output=depth_uav.Get();UINT keep=UINT(-1);
        c->CSSetShader(shader.Get(),nullptr,0);c->CSSetShaderResources(0,1,&input);c->CSSetUnorderedAccessViews(0,1,&output,&keep);
        c->Dispatch((color_desc.Width+7)/8,(color_desc.Height+7)/8,1);
        input=nullptr;output=nullptr;c->CSSetShaderResources(0,1,&input);c->CSSetUnorderedAccessViews(0,1,&output,&keep);
    }
    std::vector<unsigned char> read(ID3D11DeviceContext *c,ID3D11Texture2D *input,ComPtr<ID3D11Texture2D> &staging){
        D3D11_TEXTURE2D_DESC desc{};input->GetDesc(&desc);
        if(!staging){desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&desc,nullptr,&staging),"scene readback staging");}
        c->CopyResource(staging.Get(),input);D3D11_MAPPED_SUBRESOURCE mapped{};check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"scene readback");
        std::vector<unsigned char> result(size_t(desc.Width)*desc.Height*4);
        for(unsigned y=0;y<desc.Height;++y)std::copy_n((const unsigned char*)mapped.pData+size_t(y)*mapped.RowPitch,desc.Width*4,result.data()+size_t(y)*desc.Width*4);
        c->Unmap(staging.Get(),0);return result;
    }
};
std::unique_ptr<State> state;
BfSceneSnapshotConsumer consumer{};void *consumer_user{};
struct CaptureOptions {std::filesystem::path directory,trigger;unsigned limit=0,min_draws=64;uint64_t start=0;};
unsigned setting(const char *name,unsigned fallback,unsigned maximum){const char *s=std::getenv(name);if(!s||!*s)return fallback;char *end{};const auto value=std::strtoul(s,&end,10);return *end||value>maximum?fallback:unsigned(value);}
const CaptureOptions &options(){static CaptureOptions value=[](){CaptureOptions o;const auto *dir=std::getenv("BF_SCENE_CAPTURE_DIR");if(dir&&*dir){o.directory=std::filesystem::path(dir);const auto *trigger=std::getenv("BF_SCENE_CAPTURE_TRIGGER");if(trigger&&*trigger)o.trigger=std::filesystem::path(trigger);o.limit=setting("BF_SCENE_CAPTURE_COUNT",4,8);o.start=setting("BF_SCENE_CAPTURE_START_FRAME",0,10000000);o.min_draws=setting("BF_SCENE_CAPTURE_MIN_DEPTH_DRAWS",64,100000);}return o;}();return value;}
unsigned written{};
void dump_pair(State &saved,ID3D11DeviceContext *context,const BfSceneSnapshot &snapshot,
               const std::filesystem::path &base,bool before_ui,const char *stage,const BfSceneDepthHoldInfo *held=nullptr){
    auto color=saved.read(context,snapshot.color,saved.color_read),depth=saved.read(context,snapshot.device_depth,saved.depth_read);
    const auto &info=snapshot.info;float low=1,high=0;uint64_t occupied=0;
    for(size_t i=0;i<depth.size();i+=4){float z;std::memcpy(&z,depth.data()+i,4);require(std::isfinite(z)&&z>=0&&z<=1,"non-normalized scene device depth");low=std::min(low,z);high=std::max(high,z);occupied+=z<1;}
    {std::ofstream file(std::filesystem::path(base.wstring()+L".color.ppm"),std::ios::binary);file<<"P6\n"<<info.width<<" "<<info.height<<"\n255\n";
        const bool bgra=saved.previous_color.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
        for(size_t i=0;i<color.size();i+=4){const unsigned char rgb[]={color[i+(bgra?2:0)],color[i+1],color[i+(bgra?0:2)]};file.write((const char*)rgb,3);}require(bool(file),"scene color file write failed");}
    {std::ofstream file(std::filesystem::path(base.wstring()+L".depth.f32"),std::ios::binary);file.write((const char*)depth.data(),depth.size());require(bool(file),"scene depth file write failed");}
    {std::ofstream file(std::filesystem::path(base.wstring()+L".json"));file<<"{\n  \"frame\": "<<info.frame<<",\n  \"width\": "<<info.width<<", \"height\": "<<info.height<<",\n  \"color_va\": "<<info.color_va<<", \"depth_va\": "<<info.depth_va<<",\n  \"depth_draws\": "<<info.depth_draws<<",\n  \"depth_min\": "<<low<<", \"depth_max\": "<<high<<",\n  \"depth_pixels_below_one\": "<<occupied<<",\n  \"before_native_ui\": "<<(before_ui?"true":"false")<<",\n";
        if(stage)file<<"  \"capture_stage\": \""<<stage<<"\",\n";
        if(held)file<<"  \"view\": "<<held->view<<", \"context\": "<<held->context<<", \"geometry_serial\": "<<held->geometry_serial<<",\n";
        file<<"  \"depth_space\": \"device_0_to_1_projection_unverified\",\n  \"motion_available\": false, \"jitter_available\": false,\n  \"temporal_sdk_ready\": false\n}\n";require(bool(file),"scene metadata file write failed");}
}
void dump(ID3D11DeviceContext *context,const BfSceneSnapshot &snapshot){
    const auto &o=options();if(written>=o.limit||snapshot.info.frame<o.start||snapshot.info.depth_draws<o.min_draws)return;
    std::filesystem::create_directories(o.directory);char prefix[80];
    std::snprintf(prefix,sizeof prefix,"frame-%llu-%08X",(unsigned long long)snapshot.info.frame,snapshot.info.color_va);
    auto base=o.directory/prefix;dump_pair(*state,context,snapshot,base,true,nullptr);
    const auto &info=snapshot.info;
    ++written;std::fprintf(stderr,"[SCENE] captured real pre-UI color/device depth: %s (%ux%u, %u depth draws)\n",base.string().c_str(),info.width,info.height,info.depth_draws);
}
// Separate resources and budget: diagnostics must not mutate a normal
// consumer's retained scene, invoke it, or imply pre-UI/SDK provenance.
std::unique_ptr<State> probe_state;
struct ProbeKey {uint64_t frame{};uint32_t color{},depth{};BfDepthProbeStage stage{};};
std::array<ProbeKey,8> probe_keys{};
unsigned probe_written{};
const CaptureOptions &probe_options(){static CaptureOptions value=[](){CaptureOptions o;const auto *dir=std::getenv("BF_DEPTH_PROBE_DIR");if(dir&&*dir){o.directory=std::filesystem::path(dir);const auto *trigger=std::getenv("BF_DEPTH_PROBE_TRIGGER");if(trigger&&*trigger)o.trigger=std::filesystem::path(trigger);o.limit=setting("BF_DEPTH_PROBE_COUNT",4,8);o.min_draws=setting("BF_DEPTH_PROBE_MIN_DRAWS",64,100000);}return o;}();return value;}
}
extern "C" void bf_scene_snapshot_consumer(BfSceneSnapshotConsumer callback,void *user){consumer=callback;consumer_user=user;}
extern "C" int bf_scene_snapshot_enabled(void){return consumer||written<options().limit;}
extern "C" int bf_scene_snapshot_publish(ID3D11Device *device,ID3D11DeviceContext *context,ID3D11Texture2D *color,ID3D11Texture2D *depth,const BfSceneSnapshotInfo *info)try{
    if(!bf_scene_snapshot_enabled())return 0;
    const auto &capture=options();
    if(!consumer&&(!info||written>=capture.limit||info->frame<capture.start||info->depth_draws<capture.min_draws||
        (!capture.trigger.empty()&&!std::filesystem::exists(capture.trigger))))return 0;
    require(device&&context&&color&&depth&&info&&info->frame&&color!=depth,"invalid scene snapshot arguments");
    if(!state)state=std::make_unique<State>();state->capture(device,context,color,depth);
    auto actual=*info;actual.width=state->previous_color.Width;actual.height=state->previous_color.Height;
    BfSceneSnapshot snapshot{actual,state->color.Get(),state->depth.Get(),state->color_view.Get(),state->depth_view.Get()};
    if(consumer)consumer(&snapshot,consumer_user);dump(context,snapshot);return 1;
}catch(const std::exception &e){static bool warned=false;if(!warned){std::fprintf(stderr,"[SCENE] snapshot rejected; original scene retained: %s\n",e.what());warned=true;}return 0;}
extern "C" void bf_scene_snapshot_shutdown(void){consumer=nullptr;consumer_user=nullptr;state.reset();}
struct BfSceneDepthHold {
    State saved;
    BfSceneDepthHoldInfo identity{};
    BfSceneSnapshot result{};
    bool valid=false;
};
extern "C" int bf_scene_depth_hold_freeze(BfSceneDepthHold **hold,ID3D11Device *device,
    ID3D11DeviceContext *context,ID3D11Texture2D *color,ID3D11Texture2D *depth,
    const BfSceneDepthHoldInfo *info) try {
    if(hold&&*hold)(*hold)->valid=false;
    require(hold&&device&&context&&color&&depth&&color!=depth&&info&&info->source.frame&&
        info->source.color_va&&info->source.depth_va&&info->source.depth_draws&&info->view&&info->context&&
        info->geometry_serial,"invalid held scene identity");
    if(!*hold)*hold=new BfSceneDepthHold;
    auto &h=**hold;h.saved.capture(device,context,color,depth);
    require(info->source.width==h.saved.previous_color.Width&&info->source.height==h.saved.previous_color.Height,
        "held scene dimensions differ from identity");
    h.identity=*info;
    h.result={info->source,h.saved.color.Get(),h.saved.depth.Get(),h.saved.color_view.Get(),h.saved.depth_view.Get()};
    h.valid=true;return 1;
} catch(const std::exception &e) {
    static unsigned warnings;if(warnings++<4)std::fprintf(stderr,"[TEMPORAL] depth hold rejected: %s\n",e.what());return 0;
}
extern "C" const BfSceneSnapshot *bf_scene_depth_hold_finish(BfSceneDepthHold *h,ID3D11DeviceContext *context,
    ID3D11Texture2D *color,const BfSceneDepthHoldInfo *info) try {
    if(!h||!h->valid||!context||!color||!info)return nullptr;
    const auto &a=h->identity;const auto &b=*info;
    require(a.source.frame==b.source.frame&&a.source.color_va==b.source.color_va&&
        a.source.depth_va==b.source.depth_va&&a.source.depth_draws==b.source.depth_draws&&
        a.source.width==b.source.width&&a.source.height==b.source.height&&a.view==b.view&&a.context==b.context&&
        a.geometry_serial==b.geometry_serial,"held depth no longer matches scene geometry");
    D3D11_TEXTURE2D_DESC desc{};color->GetDesc(&desc);
    const auto &expected=h->saved.previous_color;
    require(desc.Width==expected.Width&&desc.Height==expected.Height&&desc.Format==expected.Format&&
        desc.MipLevels==1&&desc.ArraySize==1&&desc.SampleDesc.Count==1,"held scene color changed shape");
    ComPtr<ID3D11Device> owner,context_owner;color->GetDevice(&owner);context->GetDevice(&context_owner);
    require(owner.Get()==h->saved.device.Get()&&context_owner.Get()==owner.Get()&&
        context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"held scene requires original immediate context");
    // The frozen R32 device depth is intentionally untouched here. Later
    // bloom/depth-alias writes cannot replace it with a mostly-clear mask.
    context->CopyResource(h->saved.color.Get(),color);
    static const CaptureOptions capture=[] {
        CaptureOptions o;if(const auto *path=std::getenv("BF_TEMPORAL_DEPTH_CAPTURE_DIR");path&&*path) {
            o.directory=std::filesystem::u8path(path);o.limit=setting("BF_TEMPORAL_DEPTH_CAPTURE_COUNT",4,8);
            if(const auto *trigger=std::getenv("BF_TEMPORAL_DEPTH_CAPTURE_TRIGGER");trigger&&*trigger)o.trigger=std::filesystem::u8path(trigger);
        }return o;
    }();
    static unsigned written=0;
    if(written<capture.limit&&(capture.trigger.empty()||std::filesystem::exists(capture.trigger))) {
        std::filesystem::create_directories(capture.directory);
        const auto base=capture.directory/("held-"+std::to_string(b.source.frame)+"-"+std::to_string(b.source.color_va));
        dump_pair(h->saved,context,h->result,base,true,"held_world_depth_pre_ui_color",&b);++written;
    }
    return &h->result;
} catch(const std::exception &) {if(h)h->valid=false;return nullptr;}
extern "C" void bf_scene_depth_hold_destroy(BfSceneDepthHold *hold){delete hold;}
extern "C" int bf_depth_probe_enabled(void){return probe_written<probe_options().limit;}
extern "C" int bf_depth_probe_publish(ID3D11Device *device,ID3D11DeviceContext *context,ID3D11Texture2D *color,ID3D11Texture2D *depth,const BfSceneSnapshotInfo *info,BfDepthProbeStage stage)try{
    static const char *stages[]={"before_depth_clear","shared_depth_write","depth_recreate"};
    if(!bf_depth_probe_enabled()||!info||!info->frame||!info->depth_draws||unsigned(stage)>=std::size(stages))return 0;
    const auto &o=probe_options();if(info->depth_draws<o.min_draws||(!o.trigger.empty()&&!std::filesystem::exists(o.trigger)))return 0;
    for(unsigned i=0;i<probe_written;i++){const auto &key=probe_keys[i];if(key.frame==info->frame&&key.color==info->color_va&&key.depth==info->depth_va&&key.stage==stage)return 0;}
    require(device&&context&&color&&depth&&color!=depth,"invalid depth probe arguments");
    if(!probe_state)probe_state=std::make_unique<State>();
    probe_state->capture(device,context,color,depth);
    auto actual=*info;actual.width=probe_state->previous_color.Width;actual.height=probe_state->previous_color.Height;
    require(info->width==actual.width&&info->height==actual.height,"depth probe descriptor dimensions differ");
    BfSceneSnapshot snapshot{actual,probe_state->color.Get(),probe_state->depth.Get(),probe_state->color_view.Get(),probe_state->depth_view.Get()};
    std::filesystem::create_directories(o.directory);char prefix[128];
    std::snprintf(prefix,sizeof prefix,"probe-%u-%llu-%08X-%s",probe_written,(unsigned long long)actual.frame,actual.color_va,stages[unsigned(stage)]);
    const auto base=o.directory/prefix;dump_pair(*probe_state,context,snapshot,base,false,stages[unsigned(stage)]);
    probe_keys[probe_written++]={actual.frame,actual.color_va,actual.depth_va,stage};
    std::fprintf(stderr,"[DEPTH-PROBE] captured %s color/device depth: %s (%ux%u, %u depth draws; SDK-ready=false)\n",stages[unsigned(stage)],base.string().c_str(),actual.width,actual.height,actual.depth_draws);
    return 1;
}catch(const std::exception &e){static bool warned=false;if(!warned){std::fprintf(stderr,"[DEPTH-PROBE] diagnostic rejected; original scene retained: %s\n",e.what());warned=true;}return 0;}
extern "C" void bf_depth_probe_shutdown(void){probe_state.reset();}
