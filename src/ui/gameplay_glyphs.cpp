#include "host_ui.h"
#include "../input/control_labels.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
}
namespace {
bool read(uint32_t va,void *out,size_t size) {
    const uint64_t end=uint64_t(va)+size;
    if(va<0x10000||!(end<=g_xbox_total_ram||
       (va>=0x80000000u&&end<=0x80000000ull+g_xbox_total_ram)))return false;
    auto *p=reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),p,out,size,&got)&&got==size;
#else
    std::memcpy(out,p,size);return true;
#endif
}
}
extern "C" int bf_gameplay_glyph(uint32_t frame) try {
    if(!bf_ui_keyboard_mode())return 0;
    // Original 000CCD20, after computing one glyph's quad and Begin(QUADS).
    // Stack +58 resolved font, +68 UTF-16 cursor after this character,
    // +40/+44/+4C/+3C left/top/right/bottom, +64 ARGB. No guest writes.
    std::array<uint32_t,0x6C/4> words{};
    if(!read(frame,words.data(),sizeof words))return 0;
    uint32_t font_id{};uint16_t character{};
    if(!read(words[0x58/4],&font_id,4)||font_id!=0x0E38A4CA||
       words[0x68/4]<2||!read(words[0x68/4]-2,&character,2))return 0;
    const auto control=bf::input::hud_glyph_control(character);
    if(control<0)return 0; // Never replace ordinary letters or an unknown icon.
    bool desktop=false;
#ifdef _WIN32
    const auto *mode=std::getenv("RECOMP_MKB");desktop=mode&&*mode&&*mode!='0';
#endif
    const char *key=bf::input::control_label(unsigned(control),bf_ui_menu_active()!=0,desktop);
    const auto scalar=[&](unsigned offset){float f;auto bits=words[offset/4];std::memcpy(&f,&bits,4);return f;};
    const float left=scalar(0x40),top=scalar(0x44),right=scalar(0x4C),bottom=scalar(0x3C);
    if(!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(right)||!std::isfinite(bottom)||
       left<0||top<0||right>640||bottom>480||right<=left||bottom<=top||right-left>80||bottom-top>80)return 0;
    // Shadow/fully transparent passes must not leave duplicate keycaps. The
    // original End still runs when the caller omits this glyph's four vertices.
    const bool shadow=words[0x54/4]==0x000C9A88u||words[0x54/4]==0x0010A572u;
    if(!shadow&&(words[0x64/4]&0xFF000000u))
        bf_ui_prompt({left,top,right-left,bottom-top},key);
    return 1;
} catch(...) {return 0;}
