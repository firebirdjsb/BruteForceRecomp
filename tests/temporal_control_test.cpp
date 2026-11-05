#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "graphics/temporal_control.h"
#include "gpu/scene_device.h"
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool pass,const char *why) {if(!pass)throw std::runtime_error(why);}
uint32_t bits(float value) {uint32_t word;std::memcpy(&word,&value,4);return word;}
}
int main(int argc,char **argv) try {
    const bool fsr3=argc>1&&!std::strcmp(argv[1],"--fsr3");
    _putenv_s("BF_UPSCALER",fsr3?"fsr3":"fsr4");
    require(bf_temporal_requested(),"Explicit temporal provider request");
    BfTemporalView view{};
    require(!bf_temporal_prepare_view(nullptr,nullptr,1,0x80001000,640,480,nullptr,&view),"Missing scene must not issue fake jitter");
    if(!bf_game_temporal_available()) {
        std::puts("PASS unavailable temporal controller rejects scene preparation");return 0;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    require(SUCCEEDED(bf_scene_d3d12_create(0,&device,&context,&level)),"Create actual shared scene bridge");
    BFMotionRecord record{};record.words[BF_MO_VIEW_VALID]=1;
    record.words[BF_MO_CONTEXT]=0x81001000;record.words[BF_MO_VIEW]=0x81002000;
    auto *p=record.words+BF_MO_PROJECTION_WORDS;
    p[1]=bits(30);p[2]=bits(.1f);p[3]=bits(250);p[5]=bits(std::tan(3.14159265358979323846f/6));
    bf_temporal_output_size(1280,720);
    require(bf_temporal_prepare_view(device.Get(),context.Get(),91,0x80001000,640,480,&record,&view),bf_game_temporal_status());
    require(bf_game_temporal_provider()[0]==(fsr3?'3':'4'),"Requested actual provider major must match its SDK label");
    require(view.sequence==91&&view.color_va==0x80001000&&std::isfinite(view.jitter_x)&&std::isfinite(view.jitter_y)&&
        view.near_plane==.1f&&view.far_plane==250&&std::abs(view.vertical_fov-3.14159265358979323846f/3)<1e-6f,"Recover ordered view scalars with actual issued SDK jitter");
    BfTemporalView again{};
    require(bf_temporal_prepare_view(device.Get(),context.Get(),91,0x80001000,640,480,&record,&again)&&
        again.jitter_x==view.jitter_x&&again.jitter_y==view.jitter_y,"Repeated exact source retains its issued jitter");
    require(!bf_temporal_prepare_view(device.Get(),context.Get(),91,0x80002000,640,480,&record,&again),"Another source cannot share an issued frame ticket");
    p[5]=bits(.9f);
    require(!bf_temporal_prepare_view(device.Get(),context.Get(),92,0x80001000,640,480,&record,&again),"Inconsistent camera metadata must reject scene preparation");
    bf_game_temporal_shutdown();require(SUCCEEDED(bf_scene_d3d12_wait(context.Get())),"Real translation completion");
    context.Reset();device.Reset();bf_scene_d3d12_shutdown();
    std::puts("PASS actual provider selection, ordered projection metadata, SDK jitter ticket and mismatched-source rejection");return 0;
} catch(const std::exception &error) {
    bf_game_temporal_shutdown();bf_scene_d3d12_shutdown();std::fprintf(stderr,"FAIL %s\n",error.what());return 1;
}
