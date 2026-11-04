#include "ui/hud_anchor.h"
#include "ui/host_ui.h"
#include "graphics/native_aspect_state.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
namespace {std::vector<unsigned char> memory(8u<<20);}
extern "C" {
ptrdiff_t xbox_GetMemoryOffset(){return reinterpret_cast<ptrdiff_t>(memory.data());}
size_t g_xbox_total_ram=8u<<20;
void bf_scene_aa_set_enabled(int){}
}
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"HUD anchor failure line %d\n",__LINE__);return 1;}}while(0)
static std::vector<uint32_t> paint(unsigned anchor,uint64_t id,bool wide) {
    bf_aspect_present_source(wide);bf_ui_prompt_anchor(anchor);
    bf_ui_prompt({300,100,18,20},"ENTER");
    bf_ui_prompts_publish(id);bf_ui_prompts_select(id);
    std::vector<uint32_t> pixels(640*480,0xff102030);bf_ui_paint(pixels.data(),640,480);return pixels;
}
int main(int argc,char **argv) {
    if(argc==2) {
        constexpr uint32_t frame=0x10000,node=0x20000,vtable=0x30000;
        uint32_t value=0x123456;std::memcpy(memory.data()+frame,&value,4);
        std::memcpy(memory.data()+node,&vtable,4);
        value=0xC6AE0;std::memcpy(memory.data()+vtable+0x18,&value,4);
        value=0xAABBCCDD;std::memcpy(memory.data()+node+0x1C,&value,4);
        const int16_t rect[4]={50,300,80,100};std::memcpy(memory.data()+node+8,rect,sizeof rect);
        const auto owner=bf_hud_trace_begin(4,frame,node);
        if(std::strcmp(argv[1],"--trace-disabled")==0){CHECK(!owner);return 0;}
        CHECK(owner);
        const auto sprite=bf_hud_trace_begin(1,frame,node);CHECK(sprite);
        bf_hud_trace_rect(sprite,50,100,BF_HUD_LEFT);bf_hud_trace_end(sprite);bf_hud_trace_end(owner);
        CHECK(!bf_hud_trace_begin(4,frame,node)); // Five-line process budget exhausted.
        return 0;
    }
    constexpr uint32_t node=0x20000;
    const auto component=[&](uint32_t vtable,uint32_t draw) {
        std::memcpy(memory.data()+node,&vtable,4);std::memcpy(memory.data()+vtable+0x18,&draw,4);
        return bf_hud_owner_anchor(node,0);
    };
    CHECK(component(0x39FC84,0xB22A0)==BF_HUD_LEFT);
    CHECK(component(0x39F5B8,0xC2D10)==BF_HUD_LEFT);
    CHECK(component(0x39F004,0xB22A0)==BF_HUD_RIGHT);
    CHECK(component(0x3A019C,0xB22A0)==BF_HUD_RIGHT);
    CHECK(component(0x39F0F8,0xB22A0)==BF_HUD_LEFT);
    CHECK(bf_hud_owner_anchor(node,1)==BF_HUD_CENTER);
    CHECK(component(0x39FC84,0xB22A1)==BF_HUD_CENTER); // Wrong draw method rejects a known class.
    CHECK(component(0x39F368,0xB22A0)==BF_HUD_CENTER); // Reticle owner.
    CHECK(bf_hud_owner_anchor(0,0)==BF_HUD_CENTER&&bf_hud_owner_anchor(0x7ffffff0,0)==BF_HUD_CENTER);
    BfHudOwnerScope scope{0,BF_HUD_CENTER};
    const auto health=bf_hud_scope_enter(&scope,component(0x39FC84,0xB22A0));
    // Actual health cap's child class is unknown and its sprite crosses the
    // old central threshold. It must inherit the complete health owner.
    const auto cap=bf_hud_scope_enter(&scope,component(0x39FC40,0xB84D0));CHECK(scope.anchor==BF_HUD_LEFT);
    bf_hud_scope_leave(&scope,cap);CHECK(scope.anchor==BF_HUD_LEFT);
    bf_hud_scope_leave(&scope,health);CHECK(!scope.depth&&scope.anchor==BF_HUD_CENTER);
    const auto radar=bf_hud_scope_enter(&scope,component(0x39F5B8,0xC2D10));
    const auto mesh=bf_hud_scope_enter(&scope,component(0x3A006C,0xAEFB0));CHECK(scope.anchor==BF_HUD_LEFT);
    // Font/glyph and raw draw operations do not enter a new component scope.
    bf_ui_prompt_anchor(scope.anchor);CHECK(scope.anchor==BF_HUD_LEFT);
    bf_hud_scope_leave(&scope,mesh);bf_hud_scope_leave(&scope,radar);
    const auto unknown=bf_hud_scope_enter(&scope,BF_HUD_CENTER);
    const auto known_child=bf_hud_scope_enter(&scope,BF_HUD_RIGHT);CHECK(scope.anchor==BF_HUD_CENTER);
    bf_hud_scope_leave(&scope,known_child);bf_hud_scope_leave(&scope,unknown);CHECK(!scope.depth);
    bf_ui_mouse_activity();
    const auto center=paint(BF_HUD_CENTER,1,true),left=paint(BF_HUD_LEFT,2,true),right=paint(BF_HUD_RIGHT,3,true);
    for(unsigned y=0;y<480;y++)for(unsigned x=80;x<560;x++) {
        CHECK(left[y*640+x-80]==center[y*640+x]);
        CHECK(right[y*640+x+80]==center[y*640+x]);
    }
    // The selected source owns its anchor even after a future call changes it.
    bf_ui_prompt_anchor(BF_HUD_LEFT);bf_ui_prompts_select(1);
    std::vector<uint32_t> retained(640*480,0xff102030);bf_ui_paint(retained.data(),640,480);CHECK(retained==center);
    const auto ordinary=paint(BF_HUD_CENTER,4,false);
    CHECK(paint(BF_HUD_LEFT,5,false)==ordinary&&paint(BF_HUD_RIGHT,6,false)==ordinary);
    bf_ui_prompt_anchor(BF_HUD_CENTER);
    std::puts("Authenticated whole-owner classes, split health cap/raw radar inheritance, unknown/menu center and source-owned keycaps passed.");
}
