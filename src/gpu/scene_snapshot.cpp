#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "scene_snapshot.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
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
void dump(ID3D11DeviceContext *context,const BfSceneSnapshot &snapshot){
    const auto &o=options();if(written>=o.limit||snapshot.info.frame<o.start||snapshot.info.depth_draws<o.min_draws)return;
    auto color=state->read(context,snapshot.color,state->color_read),depth=state->read(context,snapshot.device_depth,state->depth_read);
    const auto &info=snapshot.info;float low=1,high=0;uint64_t occupied=0;
    for(size_t i=0;i<depth.size();i+=4){float z;std::memcpy(&z,depth.data()+i,4);require(std::isfinite(z)&&z>=0&&z<=1,"non-normalized scene device depth");low=std::min(low,z);high=std::max(high,z);occupied+=z<1;}
    std::filesystem::create_directories(o.directory);char prefix[80];std::snprintf(prefix,sizeof prefix,"frame-%llu-%08X",(unsigned long long)info.frame,info.color_va);
    auto base=o.directory/prefix;
    {std::ofstream file(std::filesystem::path(base.wstring()+L".color.ppm"),std::ios::binary);file<<"P6\n"<<info.width<<" "<<info.height<<"\n255\n";
        const bool bgra=state->previous_color.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
        for(size_t i=0;i<color.size();i+=4){const unsigned char rgb[]={color[i+(bgra?2:0)],color[i+1],color[i+(bgra?0:2)]};file.write((const char*)rgb,3);}require(bool(file),"scene color file write failed");}
    {std::ofstream file(std::filesystem::path(base.wstring()+L".depth.f32"),std::ios::binary);file.write((const char*)depth.data(),depth.size());require(bool(file),"scene depth file write failed");}
    {std::ofstream file(std::filesystem::path(base.wstring()+L".json"));file<<"{\n  \"frame\": "<<info.frame<<",\n  \"width\": "<<info.width<<", \"height\": "<<info.height<<",\n  \"color_va\": "<<info.color_va<<", \"depth_va\": "<<info.depth_va<<",\n  \"depth_draws\": "<<info.depth_draws<<",\n  \"depth_min\": "<<low<<", \"depth_max\": "<<high<<",\n  \"depth_pixels_below_one\": "<<occupied<<",\n  \"before_native_ui\": true,\n  \"depth_space\": \"device_0_to_1_projection_unverified\",\n  \"motion_available\": false, \"jitter_available\": false,\n  \"temporal_sdk_ready\": false\n}\n";require(bool(file),"scene metadata file write failed");}
    ++written;std::fprintf(stderr,"[SCENE] captured real pre-UI color/device depth: %s (%ux%u, %u depth draws)\n",base.string().c_str(),info.width,info.height,info.depth_draws);
}
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
