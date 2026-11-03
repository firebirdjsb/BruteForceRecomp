#include "ui/host_ui.h"
#include "input/control_labels.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
extern "C" {
void *bf_game_tutorial_begin(uint32_t);
void bf_game_tutorial_end(void *);
void bf_game_menu_tick(uint32_t);
}
namespace {
// A synthetic mutable guest heap, never a retail title/process or save file.
std::vector<uint8_t> memory(8u<<20);
constexpr uint32_t view=0x100000,table=0x110000,heap=0x200000;
unsigned unexpected_focus{};
struct Fixture { uint32_t tag,vtable;float x,y,w,h; };
constexpr Fixture fixtures[]={
    {0x0BB9C4AF,0x39E0C0,53,2,246,24},
    {0xEE52EBAF,0x39E290,202,13,256,256},
    {0x07B09CCB,0x39E290,244,92,32,32},{0xE6C60D0A,0x39E290,88,184,32,32},
    {0x07900EF7,0x39E290,347,133,32,32},{0xE525F3DB,0x39E290,107,31,32,32},
    {0xE4BA1ECE,0x39E0C0,15,32,354,42},{0xF1E4D812,0x39E0C0,15,59,178,28},
    {0x00AAB5C6,0x39E0C0,15,114,173,20},{0x1BA3E47C,0x39E0C0,15,262,185,22},
    {0xF6B4200F,0x39E0C0,233,260,100,94},{0xE2E50FBE,0x39E0C0,15,136,207,26},
    {0x03445ADC,0x39E0C0,15,185,354,27},{0x09D15D91,0x39E0C0,15,213,229,103},
    {0xE84B1D21,0x39E0C0,15,284,178,59},{0xE4ABC6F1,0x39E0C0,233,284,232,37},
    {0x0499CE0B,0x39E020,12,332,84,20},{0x0E8204A4,0x39E020,95,332,150,20},
    {0xFA210D4C,0x39E290,0,0,500,360}
};
void word(uint32_t at,uint32_t value){std::memcpy(memory.data()+at,&value,4);}
uint32_t word(uint32_t at){uint32_t value;std::memcpy(&value,memory.data()+at,4);return value;}
void number(uint32_t at,float value){std::memcpy(memory.data()+at,&value,4);}
uint32_t object(unsigned i){return heap+i*0x400;}
unsigned index(uint32_t tag){for(unsigned i=0;i<std::size(fixtures);i++)if(fixtures[i].tag==tag)return i;return 99;}
void text(uint32_t tag,const char16_t *value) {
    const unsigned i=index(tag);const uint32_t address=object(i),buffer=0x300000+i*0x400;
    const size_t length=std::char_traits<char16_t>::length(value);
    word(address+0x64,buffer);word(address+0x74,uint32_t(length));word(address+0x78,uint32_t(length+8));
    std::memcpy(memory.data()+buffer,value,(length+1)*2);
}
void setup() {
    std::fill(memory.begin(),memory.end(),0);
    word(0x469B58,0x18000);word(0x18008,0x19000);word(0x195C4,0x1A000);
    word(0x1A008,0x1B000);word(0x1B000,0x1B100);word(0x1B100,0x1B000);word(0x1B108,view);
    word(view,0x39E3D8);word(view+0x24,0xF0EA44DC);word(view+0x28,0xFFFFFF16);
    number(view+0xC,85);number(view+0x10,60);number(view+0x14,470);number(view+0x18,360);
    word(view+0x74,table);word(view+0x78,table+unsigned(std::size(fixtures))*4);
    for(unsigned i=0;i<std::size(fixtures);i++) {
        const auto &f=fixtures[i];const auto n=object(i);
        word(table+i*4,n);word(n,f.vtable);word(n+8,view);word(n+0x24,f.tag);word(n+0x28,0xFFFFFF16);
        number(n+0xC,f.x);number(n+0x10,f.y);number(n+0x14,f.w);number(n+0x18,f.h);
    }
    text(0x0BB9C4AF,u"Basic Controls");
    text(0xF1E4D812,u"Use Left thumbstick to move your character.");
    text(0x09D15D91,u"Use Right thumbstick to look around.");
    word(object(index(0x0499CE0B))+0xC4,'A');word(object(index(0x0E8204A4))+0xC4,'C');
    bf_ui_tutorial_clear();bf_ui_focus_lost();bf_ui_mouse_activity();
}
bool untouched() { const auto before=memory;void *token=bf_game_tutorial_begin(view);bf_game_tutorial_end(token);return !token&&before==memory; }
bool save(const char *path,const std::vector<uint32_t> &pixels) {
    std::array<unsigned char,54> header{};header[0]='B';header[1]='M';
    const auto field=[&](unsigned at,uint32_t value){std::memcpy(header.data()+at,&value,4);};
    field(2,54+640*480*4);field(10,54);field(14,40);field(18,640);field(22,480);header[26]=1;header[28]=32;
    FILE *file=std::fopen(path,"wb");if(!file)return false;
    bool ok=std::fwrite(header.data(),1,header.size(),file)==header.size();
    for(int row=479;row>=0;--row)ok&=std::fwrite(pixels.data()+row*640,4,640,file)==640;
    return std::fclose(file)==0&&ok;
}
}
extern "C" {
ptrdiff_t xbox_GetMemoryOffset(){return reinterpret_cast<ptrdiff_t>(memory.data());}
size_t g_xbox_total_ram=8u<<20;
// Focus callbacks are deliberately observable test doubles: this fixture's
// two original prompt nodes must use ordinary controller pulses instead.
int bf_guest_menu_focus(uint32_t,uint32_t){unexpected_focus++;return 1;}
int bf_guest_list_focus(uint32_t,uint32_t){unexpected_focus++;return 1;}
int bf_guest_keyboard_focus(uint32_t,uint32_t,uint32_t){unexpected_focus++;return 1;}
}
#define CHECK(condition) do { if(!(condition)){std::fprintf(stderr,"Tutorial fixture failed at line %d\n",__LINE__);return 1;} } while(0)
int main(int argc,char **argv) {
    setup();const auto before=memory;
    void *token=bf_game_tutorial_begin(view);CHECK(token);
    unsigned hidden=0;
    for(unsigned i=0;i<std::size(fixtures);i++) {
        const auto &f=fixtures[i];const bool preserve=f.tag==0x0BB9C4AF||f.vtable==0x39E020||f.tag==0xFA210D4C;
        CHECK(bool(word(object(i)+0x28)&4)==preserve);hidden+=!preserve;
    }
    CHECK(hidden==15);
    std::vector<uint32_t> painted(640*480,0xff102030),unpainted=painted;
    bf_ui_paint(painted.data(),640,480);
    CHECK(painted!=unpainted&&painted[95*640+100]==0xff08223b);
    // The actual W key must retain its five-column diagonals at 2x scale.
    // This specifically rejects the old three-column glyph that read as H.
    const uint8_t w_rows[7]={17,17,17,21,21,21,10};
    for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)
        CHECK((painted[(168+row*2)*640+198+col*2]==0xffe1f5ff)==bool(w_rows[row]&(1u<<(4-col))));
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        if(x<100||x>=540||y<95||y>=385)CHECK(painted[y*640+x]==unpainted[y*640+x]);
    if(argc==2)CHECK(save(argv[1],painted));
    bf_game_tutorial_end(token);CHECK(memory==before);
    bf_ui_controller_activity();CHECK(untouched());
    painted=unpainted;bf_ui_paint(painted.data(),640,480);CHECK(painted==unpainted);
    bf_ui_mouse_activity();token=bf_game_tutorial_begin(view);CHECK(token);bf_game_tutorial_end(token);CHECK(memory==before);
    word(view+0x28,0xFFFFFF10);CHECK(untouched());
    painted=unpainted;bf_ui_paint(painted.data(),640,480);CHECK(painted==unpainted);
    setup();word(object(index(0x0BB9C4AF))+0x64,0x70000000);CHECK(untouched());
    setup();
    // Same generic view, class and tag layout with another lesson's text.
    text(0x0BB9C4AF,u"Squad Controls");CHECK(untouched());
    setup();text(0xF1E4D812,u"Another lesson without that controller instruction.");CHECK(untouched());
    // All eight authored non-basic help pages continue through ordinary A
    // input. Their root is hidden only for Draw; flags/data restore afterward.
    for(uint32_t tag:{0xEBE31566u,0xE2E425F0u,0x0680B053u,0x0F8780C5u,
                      0x148ED17Fu,0x1D89E1E9u,0x0736FC78u,0x0E31CCEEu}) {
        setup();word(view+0x24,tag);const auto original=memory;
        bf_ui_controller_activity();CHECK(untouched());bf_ui_mouse_activity();
        token=bf_game_tutorial_begin(view);CHECK(token&&!(word(view+0x28)&4));
        uint16_t bits=0;std::array<uint8_t,8> pad{};
        CHECK(bf_ui_pad(&bits,pad.data())&&pad[0]==255);
        bf_game_tutorial_end(token);CHECK(memory==original);
        bf_ui_focus_lost();token=bf_game_tutorial_begin(view);CHECK(token);
        pad={};CHECK(!bf_ui_pad(&bits,pad.data())); // No duplicate Continue queued.
        bf_game_tutorial_end(token);
        word(view+0x28,0xFFFFFF10);CHECK(untouched()); // Reset dismissal identity.
    }
    setup();word(view+0x24,0xE83F2E8A);CHECK(untouched()); // Mission briefing.
    setup();word(view+0x24,0x12345678);CHECK(untouched()); // Unknown modal.
    using bf::input::control_label;
    CHECK(!std::strcmp(control_label(0,true,true),"ENTER"));
    CHECK(!std::strcmp(control_label(0,false,true),"SPACE"));
    CHECK(!std::strcmp(control_label(2,false,true),"R"));
    CHECK(!std::strcmp(control_label(7,false,true),"LMB"));
    CHECK(!std::strcmp(control_label(13,false,true),"TAB"));
    CHECK(!std::strcmp(control_label(2,false,false),"C"));
    CHECK(!control_label(16,false,true));
    setup();word(object(index(0xEE52EBAF))+0x24,0x12345678);CHECK(untouched());
    setup();word(0x1B108,0x12345);CHECK(untouched());
    setup();word(view+0x28,0xFFFFFF10);CHECK(untouched());
#ifdef _WIN32
    // An otherwise valid art node whose DWORD visibility field straddles
    // two native pages must retain original rendering when page two is read
    // only. Protect only this fixture's private scratch pages, never a title.
    setup();
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const uintptr_t base=reinterpret_cast<uintptr_t>(memory.data());
    const uintptr_t page=(base+0x400000+info.dwPageSize-1)&~uintptr_t(info.dwPageSize-1);
    const uint32_t crossing=uint32_t(page+info.dwPageSize-3-0x28-base);
    std::memcpy(memory.data()+crossing,memory.data()+object(index(0xEE52EBAF)),0xB4);
    word(table+index(0xEE52EBAF)*4,crossing);
    DWORD old{};CHECK(VirtualProtect(reinterpret_cast<void *>(page+info.dwPageSize),info.dwPageSize,PAGE_READONLY,&old));
    const bool safe=untouched();DWORD discarded{};
    const bool restored=VirtualProtect(reinterpret_cast<void *>(page+info.dwPageSize),info.dwPageSize,old,&discarded)!=0;
    CHECK(safe&&restored);
#endif
    // Active and hidden states preserve original accept/skip object flags and
    // controller-poll contracts. No replacement navigation callback is used.
    setup();token=bf_game_tutorial_begin(view);CHECK(token);bf_game_tutorial_end(token);
    bf_ui_menu_seen();bf_ui_mouse(100,395,640,480,1);bf_game_menu_tick(view);
    uint16_t digital=0;std::array<uint8_t,8> analog{};
    CHECK(bf_ui_pad(&digital,analog.data())&&analog[0]==255&&unexpected_focus==0);
    bf_ui_focus_lost();bf_ui_mouse(190,395,640,480,1);bf_game_menu_tick(view);analog={};digital=0;
    CHECK(bf_ui_pad(&digital,analog.data())&&analog[3]==255&&unexpected_focus==0);
    bf_ui_tutorial_clear();painted=unpainted;bf_ui_paint(painted.data(),640,480);CHECK(painted==unpainted);
    std::puts("Basic Controls semantic recognition, original draw masking/restoration, device switching, unchanged accept/skip and unrelated-view preservation passed.");
}
