// Native mouse handoff after the authenticated title's controller clamp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
extern "C" uintptr_t xbox_GetMemoryOffset(void);
extern "C" int bf_mkb_native_aim(float *,float *);
namespace {
bool address(uint32_t guest,size_t size,uintptr_t &native) {
    const auto offset=xbox_GetMemoryOffset();
    if(!guest||uint64_t(guest)+size>0x100000000ull||offset>UINTPTR_MAX-guest)return false;
    native=offset+guest;return native<=UINTPTR_MAX-size;
}
bool read32(uint32_t guest,uint32_t &value) {
    uintptr_t native{};SIZE_T read{};
    return address(guest,4,native)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(native),&value,4,&read)&&read==4;
}
bool writable(uintptr_t native,size_t size) {
    const auto end=native+size;
    while(native<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(reinterpret_cast<void*>(native),&info,sizeof info)!=sizeof info||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
           !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto begin=reinterpret_cast<uintptr_t>(info.BaseAddress);
        if(native<begin||native-begin>=info.RegionSize)return false;
        native+=std::min(size_t(end-native),info.RegionSize-(native-begin));
    }
    return true;
}
}
extern "C" void bf_mkb_apply_native_aim(unsigned controller) {
    // Exact retail 0x001F33B0: controller+0xBC is the port DWORD;
    // controller+0xB4 points to processed state. Right X/Y are floats at
    // state+0x0C/+0x10, clamped by original 0x001F2CF0 before this callback.
    uint32_t port{},state{};uintptr_t target{};
    if(!controller||controller>UINT32_MAX-0xBC||!read32(controller+0xBC,port)||port||
       !read32(controller+0xB4,state)||state>UINT32_MAX-0x14||!address(state+0xC,8,target)||!writable(target,8))return;
    float axes[2]{};
    if(!bf_mkb_native_aim(&axes[0],&axes[1])||!std::isfinite(axes[0])||!std::isfinite(axes[1]))return;
    SIZE_T written{};
    WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(target),axes,sizeof axes,&written);
}
