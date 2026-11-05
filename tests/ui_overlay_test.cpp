#include "ui/host_ui.h"
#include "ui/ui_draw.h"
#include "graphics/native_aspect_state.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr unsigned width=640,height=480;
using Image=std::vector<uint32_t>;

Image paint(bool overlay,uint32_t background=0) {
    Image image(width*height,overlay?0u:background);
    if(overlay)bf_ui_paint_overlay(image.data(),width,height);
    else bf_ui_paint(image.data(),width,height);
    return image;
}

bool matches_composite(const Image &overlay,const Image &background,uint32_t color) {
    for(size_t i=0;i<overlay.size();++i) {
        const unsigned alpha=overlay[i]>>24;
        for(unsigned shift:{0u,8u,16u}) {
            const unsigned premult=(overlay[i]>>shift)&255u;
            const unsigned back=(color>>shift)&255u;
            if(premult>alpha)return false;
            const int composed=int(premult+std::lround(back*(255.0-alpha)/255.0));
            const int reference=int((background[i]>>shift)&255u);
            if(std::abs(composed-reference)>2)return false;
        }
        if((background[i]>>24)!=255u)return false;
    }
    return true;
}

bool compare_backgrounds(bool wide) {
    bf_aspect_present_source(wide?1:0);
    const auto overlay=paint(true);
    for(const uint32_t color:{0xff102030u,0xffabcdefu,0xff000000u,0xfff0e0d0u}) {
        const auto ordinary=paint(false,color);
        if(!matches_composite(overlay,ordinary,color))return false;
    }
    return true;
}

bool test_opaque_ink_coverage() {
    Image pixels(width*height,0u);
    bf::ui::Canvas canvas{pixels.data(),width,height,0.75f,0.0f,true};
    canvas.box(20,20,20,10,0x40123456u);
    canvas.box(25,20,20,10,0x80789abcu);
    if(pixels[22*width+105]!=0xff789abcu)return false;
    for(const uint32_t pixel:pixels) {
        const unsigned alpha=pixel>>24;
        for(unsigned shift:{0u,8u,16u})if(((pixel>>shift)&255u)>alpha)return false;
    }
    return true;
}
}

int main() {
#ifdef _WIN32
    _putenv_s("BF_UI_SETTINGS","");
#else
    unsetenv("BF_UI_SETTINGS");
#endif
    bf_ui_settings_initial(1);
    if(!test_opaque_ink_coverage())return 10;
    bf_ui_mouse_activity();
    bf_ui_menu_seen();
    bf_ui_mouse(220,210,width,height,0);
    bf_ui_prompt({100,100,18,20},"ENTER");
    bf_ui_prompts_publish(1);
    bf_ui_prompts_select(1);
    const auto first=paint(true);
    if(std::all_of(first.begin(),first.end(),[](uint32_t p){return p==0;}))return 1;

    // Selection is tied to the exact source frame; overlay painting must not
    // consume pending prompts or extend their lifetime.
    bf_ui_prompts_publish(2);
    bf_ui_prompts_select(2);
    const auto empty=paint(true);
    if(std::any_of(empty.begin(),empty.end(),[](uint32_t p){return p!=0;}))return 2;
    bf_ui_prompts_select(1);
    if(paint(true)!=first)return 3;

    if(!compare_backgrounds(false)||!compare_backgrounds(true))return 4;
    BfUiPointer pointer{};
    if(!bf_ui_pointer(&pointer)||pointer.x!=220||pointer.y!=210||pointer.action!=0||bf_ui_pointer(&pointer))return 9;

    // The same overlay entry point paints the host F1 screen and authenticated
    // tutorial panel through the ordinary painter's shared state/order path.
    bf_ui_key(112,0);
    if(!compare_backgrounds(false)||!compare_backgrounds(true))return 5;
    bf_ui_key(112,0);

    std::array<BfUiTutorialNode,17> nodes{};
    nodes[0]={0x0BB9C4AF,0x0039E0C0,4,{0,0,0,0}};
    nodes[1]={0xEE52EBAF,0x0039E290,4,{202,13,256,256}};
    constexpr uint32_t text_tags[]={0xE4BA1ECE,0xF1E4D812,0x00AAB5C6,0x1BA3E47C,0xF6B4200F,
        0xE2E50FBE,0x03445ADC,0x09D15D91,0xE84B1D21,0xE4ABC6F1};
    constexpr uint32_t image_tags[]={0xEE52EBAF,0x07B09CCB,0xE6C60D0A,0x07900EF7,0xE525F3DB};
    for(unsigned i=0;i<10;i++)nodes[2+i]={text_tags[i],0x0039E0C0,4,{0,0,1,1}};
    for(unsigned i=0;i<5;i++)nodes[12+i]={image_tags[i],0x0039E290,4,{0,0,1,1}};
    if(!bf_ui_tutorial_prepare(0xF0EA44DC,{0,0,470,360},nodes.data(),nodes.size()))return 6;
    if(!compare_backgrounds(false)||!compare_backgrounds(true))return 7;
    bf_ui_tutorial_clear();
    bf_ui_prompts_select(2);
    const auto cleared=paint(true);
    if(std::any_of(cleared.begin(),cleared.end(),[](uint32_t p){return p!=0;}))return 8;

    std::puts("PASS transparent premultiplied UI overlay, source-frame prompt lifetime, F1, tutorial, 4:3 and wide composites");
    return 0;
}
