#include "ui/host_ui.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
namespace {
std::vector<uint8_t> memory(8u<<20);
constexpr uint32_t frame=0x10000,font=0x20000,text=0x30000;
void word(uint32_t at,uint32_t value){std::memcpy(memory.data()+at,&value,4);}
void scalar(uint32_t offset,float value){std::memcpy(memory.data()+frame+offset,&value,4);}
void setup(unsigned glyph) {
    std::fill(memory.begin(),memory.end(),0);
    // Synthetic replica of only the audited original CCD20 stack fields.
    word(frame+0x54,0xC9AF3);word(frame+0x58,font);word(font,0x0E38A4CA);
    word(frame+0x68,text+2);word(text,glyph);word(frame+0x64,0xFF123456);
    scalar(0x40,100);scalar(0x44,100);scalar(0x4C,118);scalar(0x3C,117);
}
std::vector<uint32_t> paint() {
    std::vector<uint32_t> pixels(640*480,0xFF102030);bf_ui_paint(pixels.data(),640,480);return pixels;
}
}
extern "C" {
ptrdiff_t xbox_GetMemoryOffset(){return reinterpret_cast<ptrdiff_t>(memory.data());}
size_t g_xbox_total_ram=8u<<20;
int bf_gameplay_glyph(uint32_t);
}
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"Gameplay glyph failed line %d\n",__LINE__);return 1;}}while(0)
int main() {
#ifdef _WIN32
    _putenv_s("RECOMP_MKB","1");
#endif
    setup('D');const auto original=memory;
    bf_ui_controller_activity();CHECK(!bf_gameplay_glyph(frame));
    bf_ui_mouse_activity();
    word(font,0x0C626FD5);CHECK(!bf_gameplay_glyph(frame)); // Normal text font.
    setup('Z');CHECK(!bf_gameplay_glyph(frame)); // Unknown icon: original retained.
    setup('D');scalar(0x40,std::numeric_limits<float>::quiet_NaN());CHECK(!bf_gameplay_glyph(frame));
    setup('D');scalar(0x4C,700);CHECK(!bf_gameplay_glyph(frame));
    setup('D');word(frame+0x68,1);CHECK(!bf_gameplay_glyph(frame));
    CHECK(!bf_gameplay_glyph(0)&&!bf_gameplay_glyph(0x70000000));
    setup('D');word(frame+0x54,0xC9A88);CHECK(bf_gameplay_glyph(frame));
    auto untouched=paint();CHECK(std::all_of(untouched.begin(),untouched.end(),[](auto p){return p==0xFF102030;}));
    setup('D');word(frame+0x54,0x10A572);CHECK(bf_gameplay_glyph(frame)&&paint()==untouched);
    setup('D');word(frame+0x64,0x00123456);CHECK(bf_gameplay_glyph(frame)&&paint()==untouched);
    setup('D');CHECK(bf_gameplay_glyph(frame)&&memory==original);
    const auto actual=paint();CHECK(actual!=untouched);
#ifdef _WIN32
    bf_ui_prompt({100,100,18,17},"R");
#else
    bf_ui_prompt({100,100,18,17},"C");
#endif
    CHECK(paint()==actual); // Compare real pixels, not just a label lookup.
    bf_ui_controller_activity();CHECK(paint()==untouched&&!bf_gameplay_glyph(frame));
    std::puts("Gameplay font identity, actual binding pixels, shadows, controller fallback and guest-memory preservation passed.");
}
