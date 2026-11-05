#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "graphics/game_temporal.h"
#include "graphics/temporal_handoff.h"
#include "gpu/scene_device.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef BF_HAS_FSR_TEMPORAL
#include "graphics/fsr_temporal.h"
#include <d3dcompiler.h>
#endif

using namespace bf::graphics;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool condition,const char *why) {if(!condition)throw std::runtime_error(why);}
void check(HRESULT result,const char *why) {require(SUCCEEDED(result),why);}
#ifdef BF_HAS_FSR_TEMPORAL
constexpr UINT rw=320,rh=180,ow=640,oh=360;
// This is a synthetic GPU scene, not game input substitution. One compute draw
// analytically renders the same jittered moving foreground/background into
// colour, projection depth, previous-minus-current motion and material masks.
// The static background's zero motion follows its actual unchanged position.
constexpr char fixture_shader[]=R"(
cbuffer Frame : register(b0) { float4 jitter_left; };
RWTexture2D<float4> color : register(u0);
RWTexture2D<float> depth : register(u1);
RWTexture2D<float2> motion : register(u2);
RWTexture2D<float> reactive : register(u3);
RWTexture2D<float> composition : register(u4);
RWTexture2D<float4> hud : register(u5);
[numthreads(8,8,1)] void draw(uint3 id:SV_DispatchThreadID) {
    if(id.x>=320 || id.y>=180)return;
    float2 p=float2(id.xy)+.5+jitter_left.xy;
    bool object=p.x>=jitter_left.z && p.x<jitter_left.z+48 && p.y>=40 && p.y<105;
    bool tile=((uint(p.x)/8+uint(p.y)/8)&1)!=0;
    color[id.xy]=object?float4(.9,.12,.04,1):tile?float4(.08,.12,.18,1):float4(.04,.08,.12,1);
    float z=object?3:10;
    depth[id.xy]=100/99.9-(100*.1)/(99.9*z);
    motion[id.xy]=object?float2(jitter_left.w-jitter_left.z,0):float2(0,0);
    reactive[id.xy]=object?.2:0;
    composition[id.xy]=object?.1:0;
    hud[id.xy]=(id.x<24 && id.y<12)?float4(0,1,0,1):float4(0,0,0,0);
})";
struct Scene {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> constants;
    std::array<ComPtr<ID3D11Texture2D>,6> textures;
    std::array<ComPtr<ID3D11UnorderedAccessView>,6> views;
    Scene() {
        D3D_FEATURE_LEVEL level{};check(bf_scene_d3d12_create(0,&device,&context,&level),"Create shared D3D11On12 fixture");
        ComPtr<ID3DBlob> binary,errors;
        const HRESULT compiled=D3DCompile(fixture_shader,sizeof fixture_shader-1,"temporal-contract",nullptr,nullptr,"draw","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
        if(FAILED(compiled)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char *>(errors->GetBufferPointer()));
        check(compiled,"Compile analytic GPU fixture");check(device->CreateComputeShader(binary->GetBufferPointer(),binary->GetBufferSize(),nullptr,&shader),"Create analytic GPU shader");
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&buffer,nullptr,&constants),"Create exact 16-byte GPU fixture constants");
        const DXGI_FORMAT formats[]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,
            DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT};
        for(size_t i=0;i<textures.size();++i) {
            D3D11_TEXTURE2D_DESC desc{};desc.Width=rw;desc.Height=rh;desc.MipLevels=desc.ArraySize=1;desc.Format=formats[i];desc.SampleDesc.Count=1;
            desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            check(device->CreateTexture2D(&desc,nullptr,&textures[i]),"Create actual GPU scene attachment");
            check(device->CreateUnorderedAccessView(textures[i].Get(),nullptr,&views[i]),"Create actual scene attachment UAV");
        }
    }
    void draw(float jitter_x,float jitter_y,float left,float previous_left) {
        const float values[4]={jitter_x,jitter_y,left,previous_left};D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map fixture constants");std::memcpy(mapped.pData,values,sizeof values);context->Unmap(constants.Get(),0);
        ID3D11UnorderedAccessView *outputs[6];for(size_t i=0;i<views.size();++i)outputs[i]=views[i].Get();
        context->CSSetUnorderedAccessViews(0,6,outputs,nullptr);auto *buffer=constants.Get();context->CSSetConstantBuffers(0,1,&buffer);context->CSSetShader(shader.Get(),nullptr,0);
        context->Dispatch((rw+7)/8,(rh+7)/8,1);
        ID3D11UnorderedAccessView *none[6]{};context->CSSetUnorderedAccessViews(0,6,none,nullptr);context->CSSetShader(nullptr,nullptr,0);
    }
    BfTemporalInputs inputs(uint64_t sequence,float x,float y) const {
        BfTemporalInputs input{};input.color=textures[0].Get();input.depth=textures[1].Get();input.motion=textures[2].Get();input.reactive=textures[3].Get();input.composition=textures[4].Get();
        input.sequence=sequence;input.scene=sequence<6?1:2;input.color_va=0x80001000u;
        input.provenance=BF_TEMPORAL_COLOR_BEFORE_UI|BF_TEMPORAL_DEPTH_COHERENT|BF_TEMPORAL_MOTION_COMPLETE|BF_TEMPORAL_PROJECTION_VERIFIED|
            BF_TEMPORAL_JITTER_APPLIED|BF_TEMPORAL_SOURCE_COMPLETE|BF_TEMPORAL_MASKS_COHERENT;
        input.jitter_x=x;input.jitter_y=y;input.elapsed_ms=1000.f/30;input.near_plane=.1f;input.far_plane=100;input.vertical_fov=1.04719755f;input.reset=sequence==1||sequence==6;
        input.hud=textures[5].Get();input.hud_sequence=sequence;input.hud_color_va=input.color_va;return input;
    }
};
std::vector<uint32_t> show(const std::shared_ptr<const GameTemporalFrame> &ticket) {
    require(ticket&&dx12_frame_texture(ticket->presentation()),"Present actual SDK output ticket");
    std::vector<uint32_t> pixels;UINT width{},height{};require(dx12_readback(pixels,width,height)&&width==ow&&height==oh,"Read back actual SDK output dimensions");
    require(pixels[5*ow+5]==0xff00ff00u,"Copied native HUD must compose after actual SDK output");return pixels;
}
double centroid(const std::vector<uint32_t> &pixels) {
    double sum{};unsigned count{};
    for(UINT y=90;y<200;++y)for(UINT x=0;x<ow;++x) {
        const auto color=pixels[size_t(y)*ow+x];if((color&255)>140&&((color>>8)&255)<100){sum+=x;++count;}
    }
    require(count>1000,"Actual SDK output lost synthetic moving foreground");return sum/count;
}
#endif
}
int main(int argc,char **argv) {
#ifndef BF_HAS_FSR_TEMPORAL
    (void)argc;(void)argv;
    try {
        require(!bf_game_temporal_available()&&!bf_game_temporal_ready(),"SDK-absent build must remain unavailable");
        BfGameTemporalConfig config{};BfTemporalInputs input{};float x=123,y=456;
        require(!bf_game_temporal_configure(&config)&&!bf_game_temporal_next_jitter(1,&x,&y)&&!bf_game_temporal_submit(&input),"SDK-absent calls must fail closed");
        require(x==123&&y==456&&!game_temporal_take_frame(1,1)&&!game_temporal_latest_frame(1),"Unavailable processor must not manufacture jitter or output");
        bf_game_temporal_shutdown();std::puts("PASS SDK-absent temporal processor fails closed without creating resources or fabricated inputs");return 0;
    }catch(const std::exception &error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
#else
    _putenv_s("BF_GRAPHICS_READBACK","1");_putenv_s("BF_DX12_DEBUG","1");_putenv_s("RECOMP_PRESENT_VSYNC","0");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BFGameTemporalContract";
    if(!RegisterClassW(&wc))return 1;
    HWND window=CreateWindowW(wc.lpszClassName,L"Temporal GPU contract verification",WS_POPUP,0,0,ow,oh,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 1;ShowWindow(window,SW_SHOWNOACTIVATE);
    int result{};
    try {
        Scene scene;auto owner=acquire_dx12_device();wchar_t exe[32768]{};require(GetModuleFileNameW(nullptr,exe,32768)>0,"Find test DLL directory");
        const auto directory=std::filesystem::path(exe).parent_path();const auto path=directory.wstring();
        BfGameTemporalConfig config{};config.device=scene.device.Get();config.context=scene.context.Get();config.render_width=rw;config.render_height=rh;
        config.output_width=ow;config.output_height=oh;config.dll_directory=path.c_str();config.nonlinear_color=1;
        require(bf_game_temporal_available()&&bf_game_temporal_configure(&config),bf_game_temporal_status());
        std::printf("Automatic provider: %s\n",bf_game_temporal_provider());
        const bool fsr3=argc>1&&std::strcmp(argv[1],"--fsr3")==0;
        std::string selected;
        for(uint32_t index=0;index<bf_game_temporal_provider_count();++index) {
            uint64_t id{};char label[256]{};require(bf_game_temporal_provider_at(index,&id,label,sizeof label),"Enumerate actual provider ID/label");
            if(std::strstr(label,fsr3?"3.1.":"4.1.")){config.provider=id;selected=label;break;}
        }
        require(config.provider&&bf_game_temporal_configure(&config),bf_game_temporal_status());
        require(selected==bf_game_temporal_provider(),"Explicit provider label must match the actual selected SDK implementation");
        require(dx12_init(window,rw,rh,false),"Initialize shared GPU output presenter");
        std::vector<std::shared_ptr<const GameTemporalFrame>> held;
        std::vector<uint32_t> first_pixels;
        float previous_left=64;
        for(uint64_t sequence=1;sequence<=8;++sequence) {
            float x{},y{},same_x{},same_y{};require(bf_game_temporal_next_jitter(sequence,&x,&y),bf_game_temporal_status());
            require(bf_game_temporal_next_jitter(sequence,&same_x,&same_y)&&x==same_x&&y==same_y,"Repeated pre-draw jitter query must retain exact source phase");
            const float left=60+float(sequence)*4;scene.draw(x,y,left,previous_left);auto input=scene.inputs(sequence,x,y);
            if(sequence<=3) {
                auto invalid=input;
                if(sequence==1)invalid.provenance&=~BF_TEMPORAL_MOTION_COMPLETE;
                if(sequence==2)invalid.jitter_x+=.01f;
                if(sequence==3)invalid.hud_sequence++;
                require(!bf_game_temporal_submit(&invalid)&&!game_temporal_take_frame(sequence,input.color_va),"Reject unverified motion, unapplied jitter or cross-source HUD");
                require(bf_game_temporal_next_jitter(sequence,&x,&y),bf_game_temporal_status());scene.draw(x,y,left,previous_left);input=scene.inputs(sequence,x,y);
            }
            if(sequence==4) {
                require(!bf_game_temporal_submit(&input),"Bounded output pool must not overwrite three retained frame leases");
                require(show(held.front())==first_pixels,"Retained SDK output and copied HUD must survive source texture reuse and pool pressure");
                held.erase(held.begin());
                require(bf_game_temporal_next_jitter(sequence,&x,&y),bf_game_temporal_status());scene.draw(x,y,left,previous_left);input=scene.inputs(sequence,x,y);
            }
            require(bf_game_temporal_submit(&input),bf_game_temporal_status());
            auto ticket=game_temporal_take_frame(sequence,input.color_va);
            require(ticket&&ticket==game_temporal_latest_frame(input.color_va)&&!game_temporal_take_frame(sequence,input.color_va+4)&&
                !game_temporal_take_frame(sequence+1,input.color_va),"Completed output lookup requires exact executor source identity");
            if(sequence==1) {
                bf_temporal_handoff_source(sequence,input.color_va);
                require(!bf_temporal_ticket_acquire(input.color_va+4),"Opaque handoff must reject another framebuffer surface");
                void *opaque=bf_temporal_ticket_acquire(input.color_va);require(opaque!=nullptr,"Retain exact producer GPU ticket");
                require(bf_temporal_handoff_publishable(input.color_va),"Completed source is publishable");
                bf_temporal_handoff_reject(sequence,input.color_va+4);
                require(bf_temporal_handoff_publishable(input.color_va),"Other surface cannot reject completed source");
                bf_temporal_handoff_reject(sequence,input.color_va);
                require(!bf_temporal_handoff_publishable(input.color_va),"Rejected jittered source must preserve preceding presentation");
                bf_temporal_handoff_source(sequence,input.color_va);
                require(!bf_temporal_handoff_publishable(input.color_va),"Repeated source cannot clear its rejection");
                bf_temporal_handoff_source(sequence+1,input.color_va);
                require(bf_temporal_handoff_publishable(input.color_va),"Next native source resumes normal publication");
                std::exception_ptr failure;bool released{};
                std::thread window_thread([&] {
                    try {
                        require(!bf_temporal_ticket_acquire(input.color_va),"Window thread must not implicitly acquire latest producer source");
                        bf_temporal_ticket_select(opaque);bf_temporal_ticket_release(opaque);released=true;
                        const auto selected=selected_temporal_frame();require(selected==ticket&&selected->sequence==sequence,"Window selection must retain exact completed ticket after original opaque lease release");
                        bf_temporal_ticket_clear();require(!selected_temporal_frame(),"Clear window-selected ticket");
                    }catch(...){failure=std::current_exception();}
                });window_thread.join();if(!released)bf_temporal_ticket_release(opaque);if(failure)std::rethrow_exception(failure);
            }
            if(sequence==1) {
                const float overwrite[4]={0,0,1,1};scene.context->ClearUnorderedAccessViewFloat(scene.views[5].Get(),overwrite);
                check(bf_scene_d3d12_wait(scene.context.Get()),"Complete deliberate source-HUD overwrite");
            }
            const auto pixels=show(ticket);require(std::abs(centroid(pixels)-(2*(left+24)-.5))<5,"Temporal output foreground disagrees with actual synthetic scene geometry");
            if(sequence==1)first_pixels=pixels;
            if(sequence<=3)held.push_back(ticket);
            if(sequence>=4)held.clear();
            previous_left=left;
        }
        auto retained=game_temporal_latest_frame(0x80001000u);const auto pixels=show(retained);
        bf_game_temporal_shutdown();require(!bf_game_temporal_ready()&&show(retained)==pixels,"Retained output must survive processor shutdown after real queue completion");
        dx12_shutdown();retained.reset();held.clear();
        std::printf("PASS actual %s SDK dispatch through D3D11On12, GPU-rendered coherent synthetic attachments, issued jitter, source guards, output leases, copied native HUD, reset and shared-queue presentation. No gameplay coverage claim.\n",selected.c_str());
    }catch(const std::exception &error){std::fprintf(stderr,"FAIL %s; processor: %s\n",error.what(),bf_game_temporal_status());result=1;}
    bf_game_temporal_shutdown();dx12_shutdown();bf_scene_d3d12_shutdown();DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return result;
#endif
}
