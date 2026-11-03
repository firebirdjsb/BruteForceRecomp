#include "scene_boundary.h"
#include "../graphics/edge_aa.h"
#include <array>
#include <cstring>
#include <vector>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
}
namespace {
bool range(uint32_t va,size_t bytes) {
    const uint64_t end=uint64_t(va)+bytes;
    return va>=0x10000&&(end<=g_xbox_total_ram||
        (va>=0x80000000u&&end<=0x80000000ull+g_xbox_total_ram));
}
bool read(uint32_t va,void *data,size_t bytes) {
    if(!range(va,bytes))return false;
    auto *source=reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),source,data,bytes,&got)&&got==bytes;
#else
    std::memcpy(data,source,bytes);return true;
#endif
}
bool writable(uint32_t va,size_t bytes) {
    if(!range(va,bytes))return false;
#ifdef _WIN32
    uintptr_t cursor=uintptr_t(xbox_GetMemoryOffset())+va,end=cursor+bytes;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void *>(cursor),&info,sizeof info)||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY)))return false;
        auto next=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;if(next<=cursor)return false;cursor=next;
    }
#endif
    return true;
}
bool write(uint32_t va,const void *data,size_t bytes) {
    if(!writable(va,bytes))return false;
    auto *target=reinterpret_cast<void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    SIZE_T done{};return WriteProcessMemory(GetCurrentProcess(),target,data,bytes,&done)&&done==bytes;
#else
    std::memcpy(target,data,bytes);return true;
#endif
}
}
extern "C" int bf_ui_try_scene_marker(uint32_t device) {
    // Original D3D 0036CD50 uses device+0/+4 as cursor/space threshold, and
    // emits one DWORD method header followed by one DWORD parameter.
    uint32_t bounds[2]{};
    if((device&3)||!read(device,bounds,sizeof bounds)||!writable(device,4)||
       (bounds[0]&3)||bounds[1]<bounds[0])return 0;
    if(uint64_t(bounds[0])+8>bounds[1])return 2;
    const uint32_t command[2]={0x00040100u,BF_UI_SCENE_BOUNDARY},next=bounds[0]+8;
    if(!writable(bounds[0],sizeof command)||!write(bounds[0],command,sizeof command))return 0;
    return write(device,&next,4)?1:0;
}
extern "C" void bf_scene_aa_cpu(uint32_t va,uint32_t w,uint32_t h,uint32_t pitch,uint32_t bpp,uint32_t frame) try {
    struct Seen{uint32_t va{},frame{};bool valid{};};static std::array<Seen,32> seen{};
    Seen *slot=nullptr;
    for(auto &item:seen)if(item.valid&&item.va==va){if(item.frame==frame)return;slot=&item;break;}
    if(!slot)for(auto &item:seen)if(!item.valid){slot=&item;break;}
    if(!slot)slot=&seen[frame%seen.size()];*slot={va,frame,true};
    if(!bf_scene_aa_enabled()||w<3||h<3||w>4096||h>4096||bpp!=4||pitch<w*4||
       uint64_t(pitch)*h>SIZE_MAX||!writable(va,size_t(pitch)*h))return;
    static bf::graphics::EdgeAA filter;static std::vector<uint32_t> pixels;
    pixels.resize(size_t(w)*h);
    for(uint32_t y=0;y<h;y++)if(!read(va+y*pitch,pixels.data()+size_t(y)*w,w*4))return;
    if(!filter.apply(pixels.data(),w,h))return;
    for(uint32_t y=0;y<h;y++)if(!write(va+y*pitch,pixels.data()+size_t(y)*w,w*4))return;
} catch(const std::exception &e){std::fprintf(stderr,"[AA] Original CPU scene retained: %s\n",e.what());}
