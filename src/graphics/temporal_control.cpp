#include "temporal_control.h"
#include "ui/upscaler_state.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <iterator>
#include <string>
#include <stdexcept>

namespace {
std::atomic<uint64_t> output_size{};
unsigned requested_major() {
    const auto mode=bf_upscaler_mode();return mode==3||mode==4?mode:0;
}
float scalar(uint32_t word) {float f;std::memcpy(&f,&word,sizeof f);return f;}
struct Configuration {
    ID3D11Device *device{};ID3D11DeviceContext *context{};
    uint32_t width{},height{},output_width{},output_height{};
    unsigned major{};
    BfTemporalView issued{};
} configuration;
struct Capabilities {
    ID3D11Device *device{};ID3D11DeviceContext *context{};
    uint64_t providers[2]{};
    std::string labels[2];
} capabilities;
std::filesystem::path dll_directory() {
    wchar_t path[32768];const DWORD n=GetModuleFileNameW(nullptr,path,DWORD(std::size(path)));
    if(!n||n>=std::size(path))throw std::runtime_error("Cannot locate temporal SDK directory");
    return std::filesystem::path(path).parent_path();
}
}
extern "C" int bf_temporal_requested(void) {return requested_major()!=0;}
extern "C" void bf_temporal_output_size(uint32_t width,uint32_t height) {
    if(!width||!height||width>16384||height>16384)return;
    output_size.store((uint64_t(width)<<32)|height,std::memory_order_release);
}
extern "C" void bf_temporal_probe_device(ID3D11Device *device,ID3D11DeviceContext *context,
    uint32_t width,uint32_t height) try {
    if(!device||!context||width!=640||height!=480)return;
    auto &caps=capabilities;
    if(caps.device==device&&caps.context==context)return;
    if(!bf_game_temporal_available()) {
        caps={device,context,{},{}};
        bf_upscaler_capabilities(-1,0,0);
        if(bf_temporal_requested())bf_upscaler_status(0,"","Temporal SDK unavailable in this player");
        return;
    }
    const uint64_t target=output_size.load(std::memory_order_acquire);
    const uint32_t ow=uint32_t(target>>32),oh=uint32_t(target);
    if(ow<width||oh<height)return; // The window has not published its fitted output yet.
    const auto directory=dll_directory();
    BfGameTemporalConfig options{device,context,width,height,ow,oh,0,directory.c_str(),0,1};
    caps={device,context,{},{}};
    if(bf_game_temporal_configure(&options)) {
        uint64_t candidates[2]{};std::string labels[2];
        for(unsigned i=0;i<bf_game_temporal_provider_count();++i) {
            uint64_t id=0;char name[256]{};
            if(bf_game_temporal_provider_at(i,&id,name,sizeof name)&&name[1]=='.'&&(name[0]=='3'||name[0]=='4')) {
                const unsigned slot=unsigned(name[0]-'3');candidates[slot]=id;labels[slot]=name;
            }
        }
        // Installed versions are not hardware capability proof. Creating an
        // explicit provider context must succeed on this actual scene device.
        for(unsigned i=0;i<2;++i)if(candidates[i]) {
            options.provider=candidates[i];
            if(bf_game_temporal_configure(&options)) {caps.providers[i]=candidates[i];caps.labels[i]=labels[i];}
        }
    }
    bf_game_temporal_shutdown();configuration={};
    bf_upscaler_capabilities(-1,caps.providers[0]!=0,caps.providers[1]!=0);
    if(const auto major=requested_major();major&&!caps.providers[major-3])
        bf_upscaler_status(0,"","Requested temporal provider unavailable");
} catch(...) {
    bf_game_temporal_shutdown();configuration={};
    bf_upscaler_capabilities(-1,0,0);
    if(bf_temporal_requested())bf_upscaler_status(0,"","Temporal device probe failed");
}
extern "C" int bf_temporal_prepare_view(ID3D11Device *device,ID3D11DeviceContext *context,uint64_t sequence,
    uint32_t color_va,uint32_t width,uint32_t height,const BFMotionRecord *record,BfTemporalView *out) try {
    if(out)*out={};
    if(!out||!bf_game_temporal_available()||!device||!context||!sequence||!color_va||
       !record||!record->words[BF_MO_VIEW_VALID]||!record->words[BF_MO_VIEW]||!record->words[BF_MO_CONTEXT]||
       !width||!height)return 0;
    const auto *p=record->words+BF_MO_PROJECTION_WORDS;
    const float near_z=scalar(p[2]),far_z=scalar(p[3]),half_y=scalar(p[1]),tan_y=scalar(p[5]);
    // Original 6AD90 stores atan(tanHalfY) in degrees at projection+18.
    // These scalar fields do not establish the title VSH's OP.z coefficients.
    constexpr float degrees_to_radians=0.01745329251994329577f;
    const float fov=2*half_y*degrees_to_radians;
    if(!std::isfinite(near_z)||!std::isfinite(far_z)||!std::isfinite(fov)||!std::isfinite(tan_y)||
       near_z<=0||far_z<=near_z||fov<=0||fov>=3.14f||tan_y<=0||
       std::abs(std::tan(fov*.5f)-tan_y)>std::max(1e-5f,tan_y*1e-4f))return 0;
    auto &c=configuration;
    // Selection and resize are latched for a whole source, so a live F1 change
    // cannot alter jitter or destroy its SDK context halfway through geometry.
    if(c.issued.sequence==sequence) {
        if(c.issued.color_va!=color_va||c.issued.view!=record->words[BF_MO_VIEW]||
           c.issued.context!=record->words[BF_MO_CONTEXT]||c.issued.near_plane!=near_z||
           c.issued.far_plane!=far_z||c.issued.vertical_fov!=fov)return 0;
        *out=c.issued;return 1;
    }
    const auto major=requested_major();if(!major)return 0;
    bf_temporal_probe_device(device,context,width,height);
    const uint64_t target=output_size.load(std::memory_order_acquire);
    const uint32_t ow=uint32_t(target>>32),oh=uint32_t(target);
    if(ow<width||oh<height||capabilities.device!=device||!capabilities.providers[major-3])return 0;
    if(!bf_game_temporal_ready()||c.device!=device||c.context!=context||c.major!=major||c.width!=width||
       c.height!=height||c.output_width!=ow||c.output_height!=oh) {
        const auto directory=dll_directory();
        BfGameTemporalConfig options{device,context,width,height,ow,oh,capabilities.providers[major-3],directory.c_str(),0,1};
        if(!bf_game_temporal_configure(&options)) {
            bf_upscaler_status(0,"",bf_game_temporal_status());return 0;
        }
        c={device,context,width,height,ow,oh,major,{}};
        bf_upscaler_status(0,bf_game_temporal_provider(),"Waiting for verified scene");
        std::fprintf(stderr,"[TEMPORAL] configured actual AMD %s, %ux%u -> %ux%u; awaiting verified scene inputs\n",
            bf_game_temporal_provider(),width,height,ow,oh);
    }
    BfTemporalView view{sequence,color_va,record->words[BF_MO_VIEW],record->words[BF_MO_CONTEXT],0,0,near_z,far_z,fov};
    if(!bf_game_temporal_next_jitter(sequence,&view.jitter_x,&view.jitter_y))return 0;
    c.issued=view;*out=view;return 1;
} catch(...) {return 0;}
