#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "xbox_memory_layout.h"
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t);
}
namespace {
[[noreturn]] void invalid(uint32_t address) {
    std::fprintf(stderr,"[GPU] Invalid original pushbuffer fixup at %08X\n",address);
    std::exit(26);
}
uint8_t *ram(uint64_t address,uint64_t bytes) {
    // The title uses low committed RAM and its physical contiguous alias.
    // Reject wraparound, MMIO and ranges crossing either mapped RAM boundary.
    const bool low=address>=0x10000u && address+bytes<=g_xbox_total_ram;
    const bool physical=address>=0x80000000ull && address+bytes<=0x80000000ull+g_xbox_total_ram;
    if(address>UINT32_MAX || bytes>g_xbox_total_ram || (!low&&!physical))invalid(uint32_t(address));
    return reinterpret_cast<uint8_t *>(uintptr_t(xbox_GetMemoryOffset())+uintptr_t(address));
}
uint32_t read(uint64_t address) {uint32_t value;std::memcpy(&value,ram(address,4),4);return value;}
void write(uint64_t address,uint32_t value){std::memcpy(ram(address,4),&value,4);}
void account(uint32_t bytes,uint32_t continuation,bool return_first) {
    const uint32_t device=read(0x0037DD88);
    // Original miniport is embedded at device+0x17C8. Its DWORD +0x818
    // counts submitted reusable-buffer bytes; +0 is the NV2A register base.
    ram(device,0x1FE4);
    auto *subroutine=xbox_Nv2aRegPtr(0x324C);
    // Preserve the distinct original store order in 0x00377F66 and
    // 0x003780F0; the CPU can observe these while waiting for ring space.
    if(return_first)*subroutine=continuation&0x0FFFFFFFu;
    write(uint64_t(device)+0x1FE0,read(uint64_t(device)+0x1FE0)+bytes);
    if(!return_first)*subroutine=continuation&0x0FFFFFFFu;
}
}
extern "C" int bf_gpu_apply_pushbuffer(uint32_t parameter,uint32_t return_address) {
    const uint32_t operation=parameter&31u;
    if(operation==10) {
        // 0x003780F0: the argument is a byte count; the continuation was
        // submitted through SET_ZSTENCIL_CLEAR (PGRAPH trapped data register).
        account(parameter>>5,return_address,false);
        return 1;
    }
    if(operation!=3 && operation!=4)return 0;
    // 0x003780CC -> 0x00377F00. Four DWORDs in the original ring describe
    // fixup-list VA, reusable-buffer VA, byte size, and ring continuation VA.
    const uint32_t record=(parameter>>5)|0x80000000u;
    const uint32_t list=read(record), buffer=read(uint64_t(record)+4);
    const uint32_t bytes=read(uint64_t(record)+8), next=read(uint64_t(record)+12);
    ram(buffer,uint64_t(bytes)+4);
    if((next&3u)||next<0x80000000u)invalid(next);
    ram(next,4);
    // Validate the full packed list before writing. Each entry is byte count,
    // buffer-relative byte offset, then exactly count bytes; FFFFFFFF ends it.
    // Validation does not invent relocations or alter the supplied payload.
    if(list)for(unsigned pass=0;pass<2;++pass) {
        uint64_t cursor=list;
        for(;;) {
            const uint32_t count=read(cursor);
            if(count==UINT32_MAX)break;
            const uint32_t offset=read(cursor+4);
            if(uint64_t(offset)+count>bytes)invalid(uint32_t(cursor));
            auto *source=ram(cursor+8,count);
            auto *target=ram(uint64_t(buffer)+offset,count);
            if(pass) {
                // Original REP MOVSD then REP MOVSB copies forward. Preserve
                // DWORD read-before-write behavior even if ranges overlap.
                uint32_t i=0;
                for(;count-i>=4;i+=4){uint32_t value;std::memcpy(&value,source+i,4);std::memcpy(target+i,&value,4);}
                for(;i<count;++i)target[i]=source[i];
            }
            cursor+=uint64_t(count)+8;
        }
    }
    // The original helper returns to the ring with JUMP, even when entered
    // through CALL. Software method 3 also clears the hardware CALL state.
    write(uint64_t(buffer)+bytes,(next&0x0FFFFFFFu)+1u);
    if(operation==3)account(bytes,next,true);
    return 1;
}
