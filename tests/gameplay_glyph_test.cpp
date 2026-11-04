#include "ui/host_ui.h"
#include "ui/prompt_text.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
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
    using bf::ui::desktop_prompt_text;
    CHECK(desktop_prompt_text(u"Press START button to access game options.",true)==u"Press ESC to access game options.");
    CHECK(desktop_prompt_text(u"Press the START button.",true)==u"Press ESC.");
    CHECK(desktop_prompt_text(u"Pull Left trigger to fire weapon.",true)==u"Click LMB to fire weapon.");
    CHECK(desktop_prompt_text(u"Pull the Right trigger to fire your active weapon.",true)==u"Click LMB to fire your active weapon.");
    CHECK(desktop_prompt_text(u"Pull Right trigger to use inventory item, pull Left trigger to fire weapon.",true)==
        u"Press G to use inventory item, Click LMB to fire weapon.");
    CHECK(desktop_prompt_text(u"Pull Left trigger to throw a grenade.",true)==u"Press G to throw a grenade.");
    CHECK(desktop_prompt_text(u"Restart checkpoint. START CAMPAIGN.\n\u00c9quipe",true)==u"Restart checkpoint. START CAMPAIGN.\n\u00c9quipe");
    CHECK(desktop_prompt_text(u"Press START button",false)==u"Press ENTER");
    constexpr uint32_t entry=0x50000,string=0x60000;
    const auto entry_text=u"Press START button to access game options.";
    const auto entry_bytes=(std::char_traits<char16_t>::length(entry_text)+1)*2;
    for(unsigned i=0;i<11;i++)word(entry+i*4,0xCAFE0000u+i);
    word(entry+0x14,string);std::memcpy(memory.data()+string,entry_text,entry_bytes);
    bf_ui_controller_activity();const auto no_relay=memory;
    CHECK(!bf_prompt_text_frame(entry)&&memory==no_relay);
    bf_ui_mouse_activity();const auto relay=bf_prompt_text_frame(entry);CHECK(relay==entry-2048);
    for(unsigned i=0;i<11;i++)if(i!=5)CHECK(!std::memcmp(memory.data()+entry+i*4,memory.data()+relay+i*4,4));
    CHECK(!std::memcmp(memory.data()+string,entry_text,entry_bytes));
    CHECK(!std::memcmp(memory.data()+entry,no_relay.data()+entry,44));
    uint32_t moved{};std::memcpy(&moved,memory.data()+relay+0x14,4);CHECK(moved==relay+44);
#ifdef _WIN32
    CHECK(std::u16string(reinterpret_cast<const char16_t *>(memory.data()+moved))==u"Press ESC to access game options.");
#else
    CHECK(std::u16string(reinterpret_cast<const char16_t *>(memory.data()+moved))==u"Press ENTER to access game options.");
#endif
    CHECK(!bf_prompt_text_frame(0)&&!bf_prompt_text_frame(0x70000000));
#ifdef _WIN32
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const auto base=reinterpret_cast<uintptr_t>(memory.data());
    const auto page=(base+0x100000+info.dwPageSize-1)&~uintptr_t(info.dwPageSize-1);
    const auto protected_entry=uint32_t(page-base)+2048;
    std::memcpy(memory.data()+protected_entry,memory.data()+entry,44);
    const auto protected_before=memory;DWORD old{},ignored{};
    CHECK(VirtualProtect(reinterpret_cast<void *>(page),info.dwPageSize,PAGE_READONLY,&old));
    const auto rejected=bf_prompt_text_frame(protected_entry);
    const bool restored=VirtualProtect(reinterpret_cast<void *>(page),info.dwPageSize,old,&ignored)!=0;
    CHECK(!rejected&&restored&&memory==protected_before);
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
    bf_ui_prompt({100,100,18,17},"MMB");
#else
    bf_ui_prompt({100,100,18,17},"V");
#endif
    CHECK(paint()==actual); // Compare real pixels, not just a label lookup.
    setup('C');CHECK(bf_gameplay_glyph(frame));const auto reload=paint();CHECK(reload!=actual);
#ifdef _WIN32
    bf_ui_prompt({100,100,18,17},"R");
#else
    bf_ui_prompt({100,100,18,17},"C");
#endif
    CHECK(paint()==reload);
    bf_ui_controller_activity();CHECK(paint()==untouched&&!bf_gameplay_glyph(frame));
    std::puts("Gameplay font identity, actual binding pixels, shadows, controller fallback and guest-memory preservation passed.");
}
