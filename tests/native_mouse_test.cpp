// Drive the actual private original report/clamp functions plus native handoff.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "recomp_types.h"
extern void sub_001F33B0(void);
extern void bf_mkb_apply_native_aim(unsigned);
static unsigned char *memory;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word=0x37F,g_fp_cc;
ptrdiff_t g_xbox_mem_offset;
uintptr_t xbox_GetMemoryOffset(void) {return reinterpret_cast<uintptr_t>(memory);}
static bool enabled=true;
static float mouse_x=4,mouse_y=-3;
int bf_mkb_native_aim(float *x,float *y) {*x=mouse_x;*y=mouse_y;return enabled;}
recomp_func_t recomp_lookup(uint32_t) {std::abort();}
recomp_func_t recomp_lookup_manual(uint32_t) {std::abort();}
// Controlled frame preparation: start with neutral axes in the selected test
// state instead of simulating the controller's double-buffer ownership.
void sub_001E80E0(void) {
    uint32_t state;std::memcpy(&state,memory+g_ecx+0xB4,4);
    std::memset(memory+state,0,0x54);g_esp+=8; // Original RET4 pops return + argument.
}
// Controlled successful report source: the original update passes its 36-byte
// temporary buffer in EAX. Simulate a full right-stick report, including axes
// above one to demonstrate the downstream original clamp before replacement.
void sub_001F3250(void) {
    std::memset(memory+g_eax,0,0x24);
    const float x=4,y=-3;std::memcpy(memory+g_eax+0x1C,&x,4);std::memcpy(memory+g_eax+0x20,&y,4);
    g_eax=1;g_esp+=4;
}
}
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static void word(uint32_t va,uint32_t value){std::memcpy(memory+va,&value,4);}
static float axis(uint32_t va){float value;std::memcpy(&value,memory+va,4);return value;}
static void update() {
    word(0x20000,0x12345678);word(0x20004,0);g_esp=0x20000;g_ecx=0x30000;
    sub_001F33B0();
}
int main() {
    memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,16*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(memory);g_xbox_mem_offset=reinterpret_cast<ptrdiff_t>(memory);
    word(0x30000+0xB4,0x31000);word(0x30000+0xBC,0);
    word(0x30000+4,1); // Initialized, active controller: successful original path.
    const float one=1,minus_one=-1;std::memcpy(memory+0x3A4F78,&one,4);std::memcpy(memory+0x3A5000,&minus_one,4);
    enabled=false;update();CHECK(g_esp==0x20008&&axis(0x3100C)==1&&axis(0x31010)==-1);
    enabled=true;update();CHECK(g_esp==0x20008&&axis(0x3100C)==4&&axis(0x31010)==-3);
    mouse_x=mouse_y=0;update();CHECK(!axis(0x3100C)&&!axis(0x31010));
    word(0x30000+0xBC,1);mouse_x=4;update();CHECK(axis(0x3100C)==1); // Physical/other port retained.
    word(0x30000+0xBC,0);word(0x30000+0xB4,0xFFFFFFFF);bf_mkb_apply_native_aim(0x30000);
    bf_mkb_apply_native_aim(0);bf_mkb_apply_native_aim(0xFFFFFFF0);
    DWORD previous{};CHECK(VirtualProtect(memory+0x31000,4096,PAGE_READONLY,&previous));
    word(0x30000+0xB4,0x31000);bf_mkb_apply_native_aim(0x30000);CHECK(axis(0x3100C)==1);
    CHECK(VirtualProtect(memory+0x31000,4096,previous,&previous));VirtualFree(memory,0,MEM_RELEASE);
    std::puts("Actual title clamp retains controller limits; post-clamp mouse is linear, stops immediately and rejects invalid/read-only destinations.");return 0;
}
