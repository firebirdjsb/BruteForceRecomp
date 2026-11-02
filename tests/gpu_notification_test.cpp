#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
static uint8_t *memory;
static uint32_t signaled,subroutine;
extern "C" {
size_t g_xbox_total_ram=64u*1024u*1024u;
ptrdiff_t xbox_GetMemoryOffset(){return reinterpret_cast<ptrdiff_t>(memory);}
// OS event-table behavior is exercised by the bounded game run. This
// collecting callback checks the production handler's precise event address.
int bf_gpu_signal_event(uint32_t address){signaled=address;return 1;}
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset){return offset==0x324C?&subroutine:nullptr;}
int bf_gpu_software_method(uint32_t,uint32_t);
}
static void write(uint32_t at,uint32_t value){std::memcpy(memory+at,&value,4);}
static uint32_t read(uint32_t at){uint32_t value;std::memcpy(&value,memory+at,4);return value;}
int check(){
    const uint32_t device=0x0037BD40;
    write(0x0037DD88,device);
    for(uint32_t parameter:{0u,1u,8u,9u,11u})
        if(bf_gpu_software_method(parameter,0)||signaled)return 1;
    if(!bf_gpu_software_method(5,0)||signaled!=device+0x196C)return 2;
    signaled=0;
    if(!bf_gpu_software_method(0xE0000025,0)||signaled!=device+0x196C)return 3;
    // A packed seven-byte record exercises REP MOVSD + REP MOVSB and the
    // deliberately unaligned next-list terminator from the original format.
    constexpr uint32_t record=0x80001000,buffer=0x80002000,list=0x00400000,next=0x80003000;
    write(record,list);write(record+4,buffer);write(record+8,32);write(record+12,next);
    write(list,7);write(list+4,12);
    for(unsigned i=0;i<7;++i)memory[list+8+i]=uint8_t(0xA0+i);
    write(list+15,UINT32_MAX);write(buffer+32,0x20000);
    const auto command=[&](unsigned op){return bf_gpu_software_method(((record&0x0FFFFFFFu)<<5)|op,0);};
    if(!command(4)||read(buffer+32)!=0x3001||subroutine||read(device+0x1FE0))return 4;
    for(unsigned i=0;i<7;++i)if(memory[buffer+12+i]!=uint8_t(0xA0+i))return 5;
    if(memory[buffer+11]||memory[buffer+19])return 6;
    if(!command(3)||subroutine!=0x3000||read(device+0x1FE0)!=32)return 7;
    if(!bf_gpu_software_method((80u<<5)|10u,0x80004000)||subroutine!=0x4000||read(device+0x1FE0)!=112)return 8;
    // A missing optional fixup list still installs the original return jump.
    write(record,0);write(record+12,0x80005000);
    if(!command(3)||read(buffer+32)!=0x5001)return 9;
    std::puts("GPU notifications, packed fixup bytes, jump continuation and CALL-state accounting passed.");
    return 0;
}
int main(){
    // Reserve the guest virtual shape; commit only the two 64-MB RAM windows.
    // No external process, executable code or permanent data is accessed.
    constexpr size_t span=0x84000000ull;
#ifdef _WIN32
    memory=static_cast<uint8_t *>(VirtualAlloc(nullptr,span,MEM_RESERVE,PAGE_NOACCESS));
    if(!memory)return 10;
    if(!VirtualAlloc(memory,g_xbox_total_ram,MEM_COMMIT,PAGE_READWRITE)||
       !VirtualAlloc(memory+0x80000000ull,g_xbox_total_ram,MEM_COMMIT,PAGE_READWRITE)){
        VirtualFree(memory,0,MEM_RELEASE);return 11;
    }
#else
    void *region=mmap(nullptr,span,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(region==MAP_FAILED)return 10;
    memory=static_cast<uint8_t *>(region);
#endif
    const int result=check();
#ifdef _WIN32
    VirtualFree(memory,0,MEM_RELEASE);
#else
    munmap(memory,span);
#endif
    return result;
}
