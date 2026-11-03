// Native relative-count handoff after the retail camera's stick response.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
extern "C" uintptr_t xbox_GetMemoryOffset(void);
extern "C" int bf_mkb_raw_delta(float *,float *);
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
bool scalar(uint32_t guest,float &value) {
    uint32_t bits{};if(!read32(guest,bits))return false;
    static_assert(sizeof bits==sizeof value);std::memcpy(&value,&bits,sizeof value);
    return std::isfinite(value);
}
bool local_input(uint32_t manager,uint32_t logical) {
    // Original 0x001E7600 iterates 33 twelve-byte mapping records at +0x80.
    // 0x001E55E0 records logical device, physical device and binding profile.
    // Validate the dynamic source instead of confusing logical ID17 with
    // physical port0. Original 0x001F2B00 identifies a pad at vtable0x396840;
    // its +0xBC DWORD is assigned port0..3 by 0x001E7450. The game's
    // 0x396868 proxy resolves its physical pad through +0xC4, exactly as
    // original 0x001F0BB0 does before it polls and applies that pad.
    uint32_t mappings[33*3]{};uintptr_t native{};SIZE_T copied{};
    if(!manager||manager>UINT32_MAX-0x20C||!logical||!address(manager+0x80,sizeof mappings,native)||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(native),mappings,sizeof mappings,&copied)||copied!=sizeof mappings)return false;
    for(unsigned i=0;i<33;i++)if(mappings[i*3]==logical) {
        uint32_t pad=mappings[i*3+1],type{},port{};
        if(!pad||pad>UINT32_MAX-0xC4||!read32(pad,type))continue;
        if(type==0x396868) {
            if(!read32(pad+0xC4,pad)||!pad||pad>UINT32_MAX-0xBC||!read32(pad,type))continue;
        }
        if(type==0x396840&&read32(pad+0xBC,port)&&port==0)return true;
    }
    return false;
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
extern "C" void bf_mkb_camera_frame(unsigned component,unsigned actor,unsigned frame,unsigned manager) {
    // Retail 0x001332A0 / handoff 0x00133EA3: component+0x20 is the local
    // logical input ID; frame+0x40 retains that logical device, +0x74 is
    // seconds, +0x18/+0x1C are pitch/yaw rates.
    // Original 0x00134069 scales pitch by model+0x2D0 * seconds * 0.5;
    // original 0x0011E5E0 scales yaw by the model rate, integrated by the actor.
    // Cancel velocity units so each count is 0.001 radians times sensitivity.
    uint32_t input_id{},logical{},model{};uintptr_t target{};float seconds{},rate{};
    if(!component||component>UINT32_MAX-0x20||!actor||actor>UINT32_MAX-0xC||
       !frame||frame>UINT32_MAX-0x78||!read32(component+0x20,input_id)||input_id<1||input_id>=25||
       !read32(frame+0x40,logical))return;
    const bool local=local_input(manager,logical);
    if(std::getenv("BF_TRACE_NATIVE_MOUSE")) {
        static unsigned candidates;
        if(++candidates<=4)std::fprintf(stderr,"[MOUSE-CAMERA] candidate ID=%u logical=%08X manager=%08X local=%d\n",input_id,logical,manager,int(local));
    }
    if(!local||
       !read32(actor+0xC,model)||!model||model>UINT32_MAX-0x2D0||
       !scalar(frame+0x74,seconds)||seconds<=0||seconds>1||
       !scalar(model+0x2D0,rate)||rate<=0||!address(frame+0x18,8,target)||!writable(target,8))return;
    if(std::getenv("BF_TRACE_NATIVE_MOUSE")) {
        static unsigned frames;
        if(++frames<=4)std::fprintf(stderr,"[MOUSE-CAMERA] local input ID=%u logical=%08X model=%08X dt=%.6f\n",input_id,logical,model,seconds);
    }
    float dx{},dy{};
    if(!bf_mkb_raw_delta(&dx,&dy)||!std::isfinite(dx)||!std::isfinite(dy))return;
    constexpr double radians_per_count=0.001;
    const double denominator=double(seconds)*rate;
    const float axes[]={float(-dy*radians_per_count*2/denominator),float(-dx*radians_per_count/denominator)};
    if(!std::isfinite(axes[0])||!std::isfinite(axes[1]))return;
    SIZE_T written{};
    WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(target),axes,sizeof axes,&written);
    if(written==sizeof axes && std::getenv("BF_TRACE_NATIVE_MOUSE")) {
        static unsigned samples;
        if(++samples<=12 || (samples%300)==0)
            std::fprintf(stderr,"[MOUSE-CAMERA] input=%08X actor=%08X dt=%.6f counts=%.3f,%.3f rates=%.6f,%.6f\n",component,actor,seconds,dx,dy,axes[0],axes[1]);
    }
}
