#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
#ifdef _MSC_VER
extern __declspec(thread) uint32_t g_esp,g_esi;
#else
extern __thread uint32_t g_esp,g_esi;
#endif
}
namespace {
bool read(uint32_t va,void *out,size_t bytes) {
    if(va<0x10000||uint64_t(va)+bytes>UINT32_MAX)return false;
    const auto *p=reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    SIZE_T count{};return ReadProcessMemory(GetCurrentProcess(),p,out,bytes,&count)&&count==bytes;
#else
    if(uint64_t(va)+bytes>g_xbox_total_ram)return false;
    std::memcpy(out,p,bytes);return true;
#endif
}
}
extern "C" void bf_trace_center(uint32_t phase,uint32_t object,uint32_t matrix) {
    std::array<float,3> center{};std::array<uint32_t,16> transform{};
    if(!read(object+0x2C,center.data(),sizeof center))return;
    bool bad=false;for(float value:center)bad|=!std::isfinite(value)||std::abs(value)>10000;
    static unsigned count;
    if(!bad&&count++>12)return;
    uint32_t caller{};
    read(g_esp+(phase?0x50:0),&caller,4);
    std::fprintf(stderr,"[PHYSICS-CENTER] phase=%u caller=%08X object=%08X matrix=%08X xyz=%.9g,%.9g,%.9g\n",phase,caller,object,matrix,center[0],center[1],center[2]);
    if(caller==0x002B370A) {
        uint32_t external{},geometry{},table{},target{};
        read(g_esi+0x44,&external,4);read(object+0x7C,&geometry,4);
        if(geometry&&read(geometry,&table,4)&&table)read(table+4,&target,4);
        std::fprintf(stderr,"[PHYSICS-CENTER] config=%08X external=%08X geometry=%08X table=%08X getter=%08X\n",g_esi,external,geometry,table,target);
    }
    if(read(matrix,transform.data(),sizeof transform)) {
        std::fprintf(stderr,"[PHYSICS-CENTER] input:");
        for(uint32_t bits:transform)std::fprintf(stderr," %08X",bits);
        std::fputc('\n',stderr);
    }
}
extern "C" void bf_trace_proxy(uint32_t phase,uint32_t object,uint32_t source,uint32_t caller) {
    // Proxy geometry is exactly two DWORDs: vtable, then a vector pointer.
    // The getter at original 0x002B97D0 copies three floats from that pointer.
    uint32_t words[3]{};
    if(!read(source,words,sizeof words))return;
    static unsigned count;
    if(count++>12 && source>=0x00380000)return;
    std::fprintf(stderr,"[PHYSICS-PROXY] phase=%u caller=%08X object=%08X source=%08X words=%08X,%08X,%08X\n",
        phase,caller,object,source,words[0],words[1],words[2]);
    if(phase==0) {
        uint32_t stack[12]{};
        if(read(g_esp,stack,sizeof stack)) {
            std::fprintf(stderr,"[PHYSICS-PROXY] stack=%08X:",g_esp);
            for(auto value:stack)std::fprintf(stderr," %08X",value);
            std::fputc('\n',stderr);
        }
    }
}
extern "C" void bf_trace_model(uint32_t entity) {
    uint32_t wrapper{},model{},geometry{},buffer{},source{};
    if(!read(entity+0x6C,&wrapper,4)||!read(wrapper,&model,4)||
       !read(model+0x24,&geometry,4)||!read(geometry+4,&buffer,4)||!read(buffer+8,&source,4))return;
    static unsigned count;
    if(count++>=8 && source!=0x000479D0)return;
    std::fprintf(stderr,"[MODEL] entity=%08X wrapper=%08X model=%08X geometry=%08X buffer=%08X source=%08X\n",
        entity,wrapper,model,geometry,buffer,source);
    const uint32_t addresses[]={wrapper,model,geometry,buffer};
    for(auto address:addresses) {
        uint32_t words[16]{};
        if(!read(address,words,sizeof words))continue;
        std::fprintf(stderr,"[MODEL] %08X:",address);
        for(auto value:words)std::fprintf(stderr," %08X",value);
        std::fputc('\n',stderr);
    }
}
extern "C" void bf_trace_resource(uint32_t stack) {
    uint32_t words[128]{};
    if(!read(stack,words,sizeof words))return;
    std::fprintf(stderr,"[RESOURCE-STACK] %08X:",stack);
    for(auto value:words)std::fprintf(stderr," %08X",value);
    std::fputc('\n',stderr);
}
extern "C" void bf_trace_ipn(uint32_t phase,uint32_t object,uint32_t size,uint32_t result) {
    static thread_local uint32_t relocating;
    if(phase==2)relocating=object;
    if(phase==3)object=relocating;
    uint32_t fields[7]{},allocator{};
    if(!read(object,fields,sizeof fields))return;
    read(0x003BC650,&allocator,4);
    std::fprintf(stderr,"[IPN] phase=%u object=%08X size=%u result=%08X allocator=%08X base=%08X vector=%08X..%08X\n",
        phase,object,size,result,allocator,fields[1],fields[4],fields[5]);
    uint32_t record[3]{};
    if(fields[4]&&read(fields[4],record,sizeof record))
        std::fprintf(stderr,"[IPN] record %08X: %08X %08X %08X\n",fields[4],record[0],record[1],record[2]);
}
