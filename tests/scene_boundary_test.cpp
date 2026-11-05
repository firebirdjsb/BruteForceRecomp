#include "ui/scene_boundary.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace {
// Entirely synthetic guest memory; no retail executable, process or saves.
std::vector<uint32_t> memory((8u<<20)/4,0xCCCCCCCCu);
constexpr uint32_t device=0x10000,commands=0x20000,surface=0x30000;
uint32_t &word(uint32_t address){return memory[address/4];}
void bounds(uint32_t cursor,uint32_t end){word(device)=cursor;word(device+4)=end;}
}
extern "C" {
ptrdiff_t xbox_GetMemoryOffset(){return reinterpret_cast<ptrdiff_t>(memory.data());}
size_t g_xbox_total_ram=8u<<20;
}
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"Scene boundary failed line %d\n",__LINE__);return 1;}}while(0)
int main() {
    bounds(commands,commands+8);const auto before=memory;
    CHECK(bf_ui_try_scene_marker(device)==1);
    CHECK(word(device)==commands+8&&word(device+4)==commands+8);
    CHECK(word(commands)==0x00040100u&&word(commands+4)==BF_UI_SCENE_BOUNDARY);
    for(size_t i=0;i<memory.size();i++)
        if(i!=device/4&&i!=commands/4&&i!=(commands+4)/4)CHECK(memory[i]==before[i]);
    bounds(commands,commands+4);auto saved=memory;
    CHECK(bf_ui_try_scene_marker(device)==2&&memory==saved);
    bounds(commands+1,commands+32);saved=memory;
    CHECK(bf_ui_try_scene_marker(device)==0&&memory==saved);
    bounds(commands,commands-4);saved=memory;
    CHECK(bf_ui_try_scene_marker(device)==2&&memory==saved);
    bounds(commands,commands);saved=memory;
    CHECK(bf_ui_try_scene_marker(device)==2&&memory==saved);
    bounds(0x70000000,0x70000008);saved=memory;
    CHECK(bf_ui_try_scene_marker(device)==0&&memory==saved);
    CHECK(bf_ui_try_scene_marker(0)==0&&bf_ui_try_scene_marker(device+1)==0);
    for(const auto marker:{BF_SCENE_WIDE_BEGIN,BF_SCENE_WIDE_END,BF_UI_ANCHOR_LEFT,BF_UI_ANCHOR_CENTER,BF_UI_ANCHOR_RIGHT}) {
        bounds(commands,commands+8);
        CHECK(bf_ui_try_marker(device,marker)==1&&word(commands+4)==marker);
    }
    bounds(commands,commands+8);saved=memory;
    CHECK(bf_ui_try_marker(device,0x12345678u)==0&&memory==saved);
#ifdef _WIN32
    // A valid cursor whose destination page is read-only must not be written.
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const auto base=reinterpret_cast<uintptr_t>(memory.data());
    const auto page=(base+0x100000+info.dwPageSize-1)&~uintptr_t(info.dwPageSize-1);
    const auto at=uint32_t(page-base);bounds(at,at+8);saved=memory;
    DWORD old{},ignored{};
    CHECK(VirtualProtect(reinterpret_cast<void *>(page),info.dwPageSize,PAGE_READONLY,&old));
    const auto result=bf_ui_try_scene_marker(device);
    const bool restored=VirtualProtect(reinterpret_cast<void *>(page),info.dwPageSize,old,&ignored)!=0;
    CHECK(result==0&&restored&&memory==saved);
#endif
    constexpr unsigned w=32,h=24,pitch=(w+2)*4;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)word(surface+y*pitch+x*4)=x>y?0x7FFFFFFFu:0x7F000000u;
    const auto original=memory;
    bf_scene_aa_set_enabled(0);bf_scene_aa_cpu(surface,w,h,pitch,4,1);
    CHECK(memory==original);
    // Enabling after the first UI boundary cannot filter UI already drawn.
    bf_scene_aa_set_enabled(1);bf_scene_aa_cpu(surface,w,h,pitch,4,1);
    CHECK(memory==original);
    bf_scene_aa_cpu(surface,w,h,pitch,4,2);CHECK(memory!=original);
    const auto filtered=memory;bf_scene_aa_cpu(surface,w,h,pitch,4,2);CHECK(memory==filtered);
    for(unsigned y=0;y<h;y++) {
        CHECK(word(surface+y*pitch+w*4)==0xCCCCCCCCu&&word(surface+y*pitch+(w+1)*4)==0xCCCCCCCCu);
        for(unsigned x=0;x<w;x++)CHECK((word(surface+y*pitch+x*4)&0xFF000000u)==0x7F000000u);
    }
    bf_scene_aa_cpu(surface,w,h,pitch,2,3);CHECK(memory==filtered);
    bf_scene_aa_cpu(surface,w,h,1,4,4);CHECK(memory==filtered);
    std::puts("Ordered marker bytes, bounds, write protection, pitch/alpha and once-per-frame scene AA passed.");
}
