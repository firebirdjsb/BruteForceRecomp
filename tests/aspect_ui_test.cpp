#include "graphics/native_aspect_state.h"
#include "ui/graphics_options.h"
#include "ui/host_ui.h"
#include "ui/ui_draw.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <utility>
// This focused UI executable has no scene renderer; retain the AA UI contract.
extern "C" void bf_scene_aa_set_enabled(int){}
namespace {
int surface(uint32_t address){return address==0x12340000u;}
int scope_value{};unsigned anchor_value=9;
void scope_command(int value){scope_value=value;}
void anchor_command(unsigned value){anchor_value=value;}
bool near(float a,float b){return std::abs(a-b)<0.02f;}
}
int main() {
    // CPU-only runtime hosts receive no graphics commands until a backend
    // registers. Registration carries metadata only, never GPU completion.
    bf_aspect_command_scope(1);bf_aspect_command_anchor(2);
    if(scope_value||anchor_value!=9)return 22;
    bf_aspect_backend_commands(scope_command,anchor_command);
    bf_aspect_command_scope(1);bf_aspect_command_anchor(2);
    if(scope_value!=1||anchor_value!=2)return 23;
    bf_aspect_backend_commands(nullptr,nullptr);
    bf_aspect_command_scope(0);bf_aspect_command_anchor(0);
    if(scope_value!=1||anchor_value!=2)return 24;
    if(bf_aspect_requested()||bf_aspect_ready()||bf_aspect_frame_wide()||bf_aspect_available())return 1;
    bf_aspect_request(1);if(bf_aspect_ready())return 2;
    bf_aspect_scene_supported(1);if(bf_aspect_ready())return 3;
    bf_aspect_presenter_supported(1);if(!bf_aspect_ready()||!bf_aspect_available())return 4;
    bf_aspect_present_source(1);bf_aspect_request(0);
    if(bf_aspect_ready()||!bf_aspect_frame_wide())return 5;
    bf_aspect_surface_reader(surface);
    if(!bf_aspect_surface_wide(0x12340000u)||bf_aspect_surface_wide(0x12340004u))return 6;
    bf_aspect_surface_reader(nullptr);if(bf_aspect_surface_wide(0x12340000u))return 7;
    const auto path=std::filesystem::temp_directory_path()/
        ("bf-aspect-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".settings");
    {std::ofstream old(path);old<<"BF_GRAPHICS 1\nauto d3d12 0 1 0 1080 1 1 0 100 1 30 0 1 1\n";}
    if(bf::load_graphics(path).native_widescreen)return 8;
    auto options=bf::load_graphics(path);options.native_widescreen=true;bf::save_graphics(path,options);
    if(!bf::load_graphics(path).native_widescreen)return 9;
    _putenv_s("BF_UI_SETTINGS",path.string().c_str());_putenv_s("BF_NATIVE_WIDESCREEN","");
    bf_ui_settings_initial(1);if(!bf_aspect_requested())return 10;
    // Check inverse pointer mapping at multiple viewport shapes, including a
    // 4:3 window containing a letterboxed 16:9 camera and centered 4:3 controls.
    for(const auto size:{std::pair{1280,720},std::pair{640,480},std::pair{1600,720}}) {
        const float vh=std::min(float(size.second),float(size.first)*9.0f/16.0f),vw=vh*16.0f/9.0f;
        for(const float logical:{112.0f,320.0f,528.0f}) {
            const int px=int(std::lround((size.first-vw)*0.5f+(320+(logical-320)*0.75f)*vw/640));
            const int py=int(std::lround((size.second-vh)*0.5f+240*vh/480));
            float gx{},gy{};if(!bf_ui_point(px,py,size.first,size.second,&gx,&gy)||std::abs(gx-logical)>0.6f||!near(gy,240))return 11;
        }
        float gx{},gy{};if(bf_ui_point(0,size.second/2,size.first,size.second,&gx,&gy))return 12;
    }
    std::vector<uint32_t> pixels(640*480);
    bf::ui::Canvas canvas{pixels.data(),640,480,0.75f};canvas.box(112,100,416,20,0xffabcdef);
    if(pixels[100*640+163]!=0||pixels[100*640+164]!=0xffabcdef||pixels[100*640+475]!=0xffabcdef||pixels[100*640+476]!=0)return 13;
    // All four quarter-pixel phases of a compensated one-pixel font stroke
    // must survive with 3/4 pixel of coverage, including its alpha channel.
    for(int x=316;x<320;++x) {
        std::fill(pixels.begin(),pixels.end(),0);
        canvas.box(x,10,1,5,0xffffffff);
        for(unsigned y=10;y<15;++y) {
            unsigned energy{},nonzero{};
            for(unsigned column=0;column<640;++column) {
                const auto pixel=pixels[y*640+column];
                energy+=pixel&255;nonzero+=pixel!=0;
                if((pixel&255)!=((pixel>>24)&255))return 18;
            }
            if(energy<191||energy>192||nonzero<1||nonzero>2)return 19;
        }
    }
    // A small I has a one-column stem between its three-column caps. Its stem
    // must survive every phase. Check a real glyph as well as the primitive.
    for(int x=316;x<320;++x) {
        std::fill(pixels.begin(),pixels.end(),0);canvas.text(x,20,"I",0xffffffff);
        for(unsigned y=20;y<25;++y) {
            unsigned energy{};for(unsigned column=0;column<640;++column)energy+=pixels[y*640+column]&255;
            if(y>20&&y<24) {if(energy<191||energy>192)return 20;}
            else if(energy<380)return 20;
        }
    }
    // Ordinary 4:3 boxes keep exact opaque color and clipping, with no blends.
    std::fill(pixels.begin(),pixels.end(),0x12345678);bf::ui::Canvas original{pixels.data(),640,480};
    original.box(-1,30,3,2,0xa1b2c3d4);
    if(pixels[30*640]!=0xa1b2c3d4||pixels[30*640+1]!=0xa1b2c3d4||pixels[30*640+2]!=0x12345678||pixels[32*640]!=0x12345678)return 21;
    // F1 opens unchanged; Renderer row 6 toggles immediately and persists.
    bf_ui_key(112,0);bf_ui_key(9,0);
    for(int i=0;i<6;++i)bf_ui_key(40,0);
    bf_ui_key(13,0);if(bf_aspect_requested()||bf::load_graphics(path).native_widescreen)return 14;
    bf_aspect_scene_supported(0);bf_ui_key(13,0);if(bf_aspect_requested())return 15;
    bf_aspect_scene_supported(1);bf_ui_key(13,0);if(!bf_aspect_requested())return 16;
    bf_ui_paint(pixels.data(),640,480);
    bf_aspect_present_source(0);float gx{},gy{};
    if(!bf_ui_point(640,360,1280,720,&gx,&gy)||!near(gx,320)||!near(gy,240))return 17;
    std::filesystem::remove(path);_putenv_s("BF_UI_SETTINGS","");
    std::puts("PASS native aspect gate, completed-frame independence, old/new preferences, canvas and pointer safe area, live F1 setting");
}
