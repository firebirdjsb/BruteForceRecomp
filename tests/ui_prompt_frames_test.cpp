#include "ui/host_ui.h"
#include <algorithm>
#include <cstdio>
#include <vector>
namespace {
std::vector<uint32_t> paint() {
    std::vector<uint32_t> pixels(640*480,0xff102030);
    // Model the native label immediately after its 18-pixel icon slot.
    for(unsigned y=100;y<120;y++)for(unsigned x=118;x<150;x++)pixels[y*640+x]=0xffabcdef;
    bf_ui_paint(pixels.data(),640,480);return pixels;
}
}
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"Prompt frame failure line %d\n",__LINE__);return 1;}}while(0)
int main() {
    bf_ui_mouse_activity();bf_ui_prompts_publish(1);bf_ui_prompts_select(1);
    const auto empty=paint();
    bf_ui_prompt({100,100,18,20},"ENTER");bf_ui_prompts_publish(2);bf_ui_prompts_select(2);
    const auto menu=paint();CHECK(menu!=empty&&menu[102*640+92]==0xff59bfd9);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        if(x<92||x>=116||y<102||y>=117)CHECK(menu[y*640+x]==empty[y*640+x]);
    // Repeats of the same image retain its keys, independent of wall time.
    bf_ui_prompts_publish(3);bf_ui_prompts_select(2);CHECK(paint()==menu);
    // The first image without the menu has no keys: no 250 ms fade/cache.
    bf_ui_prompts_select(3);CHECK(paint()==empty);
    bf_ui_prompt({200,100,18,20},"ESC");bf_ui_prompts_publish(4);bf_ui_prompts_select(4);
    const auto moved=paint();CHECK(moved!=empty&&moved[102*640+92]==empty[102*640+92]);
    // Pending future-frame commands cannot leak into the current frame.
    bf_ui_prompt({300,100,18,20},"TAB");CHECK(paint()==moved);
    bf_ui_prompts_publish(5);bf_ui_prompts_select(5);CHECK(paint()!=moved);
    for(uint64_t frame=6;frame<20;frame++){bf_ui_prompts_publish(frame);bf_ui_prompts_select(frame);CHECK(paint()==empty);}
    bf_ui_prompts_select(2);CHECK(paint()==empty); // Retired metadata fails closed.
    bf_ui_prompt({100,100,18,20},"ENTER");bf_ui_prompts_publish(20);bf_ui_prompts_select(20);
    CHECK(paint()==menu);bf_ui_focus_lost();CHECK(paint()==empty);
    std::puts("Keycap label clearance, exact source-frame lifetime, repeats, animation and publication isolation passed.");
}
