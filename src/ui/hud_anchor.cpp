#include "hud_anchor.h"
#include "host_ui.h"
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
}
namespace {
bool read_guest(uint32_t address,void *output,size_t bytes) {
    const uint64_t end=uint64_t(address)+bytes;
    if(address<0x10000||!(end<=g_xbox_total_ram||
       (address>=0x80000000u&&end<=0x80000000ull+g_xbox_total_ram)))return false;
    const uintptr_t base=uintptr_t(xbox_GetMemoryOffset());
    if(base>UINTPTR_MAX-address||base+address>UINTPTR_MAX-bytes)return false;
    const void *source=reinterpret_cast<const void *>(base+address);
#ifdef _WIN32
    SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),source,output,bytes,&got)&&got==bytes;
#else
    std::memcpy(output,source,bytes);return true;
#endif
}
struct Trace {
    const char *path=std::getenv("BF_HUD_OWNER_TRACE"),*trigger=std::getenv("BF_HUD_OWNER_TRIGGER");
    FILE *file{};bool armed{},failed{};unsigned lines{},limit=2048,next{},depth{};
    struct Owner {uint32_t token,node,vtable,draw;};std::array<Owner,64> owners{};
    Trace(){if(const char *s=std::getenv("BF_HUD_OWNER_LIMIT")){char *end{};const auto n=std::strtoul(s,&end,10);if(!*end&&n<=8192)limit=unsigned(n);}}
    ~Trace(){if(file)std::fclose(file);}
    bool enabled(){
        if(!path||!*path||failed||lines>=limit)return false;
        if(!armed){if(trigger&&*trigger&&!std::filesystem::exists(trigger))return false;armed=true;}
        if(!file){file=std::fopen(path,"ab");if(!file){failed=true;return false;}}
        return true;
    }
};
Trace &trace(){static Trace value;return value;}
}
extern "C" unsigned bf_hud_owner_anchor(uint32_t component,int menu) try {
    if(menu)return BF_HUD_CENTER;
    uint32_t vtable{},draw{};
    if(!read_guest(component,&vtable,4)||vtable>UINT32_MAX-0x18||!read_guest(vtable+0x18,&draw,4))return BF_HUD_CENTER;
    // Retail class/draw pairs authenticated by the generated-chunk hashes.
    // Actual owner-trace-c records prove these complete native component
    // draws enclose the frames, caps/fills, raw radar mesh and text together.
    // No transient object address, sprite extent or font pivot identifies them.
    if((vtable==0x0039FC84u&&draw==0x000B22A0u)|| // health + shield
       (vtable==0x0039F5B8u&&draw==0x000C2D10u)|| // radar incl. AEFB0 raw mesh
       (vtable==0x0039F0F8u&&draw==0x000B22A0u))  // complete co-op prompt
        return BF_HUD_LEFT;
    if((vtable==0x0039F004u&&draw==0x000B22A0u)|| // ammo + weapon
       (vtable==0x003A019Cu&&draw==0x000B22A0u))  // inventory item/name/count
        return BF_HUD_RIGHT;
    return BF_HUD_CENTER;
} catch(...) {return BF_HUD_CENTER;}
extern "C" uint32_t bf_hud_trace_begin(unsigned stage,uint32_t frame,uint32_t component) try {
    auto &t=trace();if(!t.enabled()||stage<1||stage>4||t.depth==t.owners.size())return 0;
    std::array<uint32_t,11> args{};read_guest(frame,args.data(),sizeof args);
    if(stage==2&&!component)component=args[1];
    // Read the native component's common 0x2C-byte header only. The diagnostic
    // field named uid is the raw +1C DWORD, not a proven resource identifier.
    // +28 is also reported raw because its link semantics differ. The four
    // signed 16-bit rectangle values begin at +8 in this common header.
    std::array<uint32_t,11> words{};read_guest(component,words.data(),sizeof words);
    uint32_t draw{};if(words[0]<=UINT32_MAX-0x18)read_guest(words[0]+0x18,&draw,4);
    int16_t rect[4]{};std::memcpy(rect,words.data()+2,sizeof rect);
    float x{},y{};std::memcpy(&x,&args[2],4);std::memcpy(&y,&args[3],4);
    const uint32_t token=++t.next;
    std::fprintf(t.file,"{\"event\":\"enter\",\"token\":%u,\"stage\":%u,\"return\":%u,\"node\":%u,\"vtable\":%u,\"draw\":%u,\"uid\":%u,\"rect\":[%d,%d,%d,%d],\"link28\":%u,\"font_x\":%.9g,\"font_y\":%.9g,\"font_flags\":%u,\"owners\":[",
        token,stage,stage==4?0xB2761u:args[0],component,words[0],draw,words[7],rect[0],rect[1],rect[2],rect[3],words[10],std::isfinite(x)?x:0,std::isfinite(y)?y:0,args[6]);
    for(unsigned i=0;i<t.depth;i++)std::fprintf(t.file,"%s[%u,%u,%u,%u]",i?",":"",t.owners[i].token,t.owners[i].node,t.owners[i].vtable,t.owners[i].draw);
    std::fprintf(t.file,"]}\n");++t.lines;std::fflush(t.file);
    t.owners[t.depth++]={token,component,words[0],draw};return token;
} catch(...) {return 0;}
extern "C" int bf_hud_trace_configured(void) {auto &t=trace();return t.path&&*t.path&&!t.failed&&t.lines<t.limit;}
extern "C" void bf_hud_trace_end(uint32_t token) try {
    if(!token)return;auto &t=trace();
    if(t.depth&&t.owners[t.depth-1].token==token)--t.depth;
    if(t.enabled()){std::fprintf(t.file,"{\"event\":\"leave\",\"token\":%u}\n",token);++t.lines;std::fflush(t.file);}
} catch(...) {}
extern "C" void bf_hud_trace_rect(uint32_t token,float x,float width,unsigned anchor) try {
    if(!token||!std::isfinite(x)||!std::isfinite(width))return;auto &t=trace();if(!t.enabled())return;
    std::fprintf(t.file,"{\"event\":\"rect\",\"token\":%u,\"x\":%.9g,\"width\":%.9g,\"anchor\":%u}\n",token,x,width,anchor);++t.lines;std::fflush(t.file);
} catch(...) {}
