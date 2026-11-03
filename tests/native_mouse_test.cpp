// Original controller/rotation code with the native relative-count handoff.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <initializer_list>
extern "C" {
#include "recomp_types.h"
void sub_001F33B0(void);
void sub_0011E5E0(void);
void sub_0011E840(void);
void bf_mkb_camera_frame(unsigned,unsigned,unsigned,unsigned);
static unsigned char *memory;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word=0x37F,g_fp_cc;
RECOMP_TLS RecompXmm g_xmm0;
ptrdiff_t g_xbox_mem_offset;
uintptr_t xbox_GetMemoryOffset(void){return reinterpret_cast<uintptr_t>(memory);}
static bool enabled=true;
static float mouse_x=40,mouse_y=-25;
int bf_mkb_raw_delta(float *x,float *y){*x=mouse_x;*y=mouse_y;return enabled;}
recomp_func_t recomp_lookup(uint32_t){std::abort();}
recomp_func_t recomp_lookup_manual(uint32_t){std::abort();}
// Controlled frame/report sources for the original physical update.
void sub_001E80E0(void){uint32_t state;std::memcpy(&state,memory+g_ecx+0xB4,4);std::memset(memory+state,0,0x54);g_esp+=8;}
void sub_001F3250(void){std::memset(memory+g_eax,0,0x24);const float x=4,y=-3;std::memcpy(memory+g_eax+0x1C,&x,4);std::memcpy(memory+g_eax+0x20,&y,4);g_eax=1;g_esp+=4;}
// The real pitch setter clamps/stores pitch, then calls this pose routine.
// Its two-argument contract is verified; pose geometry is outside this test.
void sub_00072010(void){uint32_t pose;std::memcpy(&pose,memory+g_esp+4,4);if(pose!=0x605BC)std::abort();g_esp+=12;}
}
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static void word(uint32_t va,uint32_t value){std::memcpy(memory+va,&value,4);}
static void scalar(uint32_t va,float value){std::memcpy(memory+va,&value,4);}
static float number(uint32_t va){float value;std::memcpy(&value,memory+va,4);return value;}
int main(){
    memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,16*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(memory);g_xbox_mem_offset=reinterpret_cast<ptrdiff_t>(memory);
    word(0x300B4,0x31000);word(0x30004,1);scalar(0x3A4F78,1);scalar(0x3A5000,-1);
    for(unsigned port:{0u,1u}){
        word(0x300BC,port);word(0x20000,0x12345678);word(0x20004,0);g_esp=0x20000;g_ecx=0x30000;
        sub_001F33B0();CHECK(g_esp==0x20008&&number(0x3100C)==1&&number(0x31010)==-1);
    }
    word(0x50020,17);word(0x6000C,0x61000);scalar(0x612D0,2);
    word(0x40040,0x71000);word(0x70080,0x71000);word(0x70084,0x30000);word(0x30000,0x396840);word(0x300BC,0);
    scalar(0x40074,1.f/30);bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);
    CHECK(number(0x4001C)<0&&number(0x40018)>0);
    // Real gameplay uses logical ID1 through a virtual pad proxy. Validate
    // the original +0xC4 ownership chain as well as the direct physical path.
    word(0x50020,1);word(0x70084,0x32000);word(0x32000,0x396868);word(0x320C4,0x30000);
    for(float seconds:{1.f/30,1.f/60,.1f}){
        scalar(0x40074,seconds);scalar(0x605F4,0);
        bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);
        const float pitch=number(0x40018),yaw=number(0x4001C);
        word(0x20000,0x12345678);scalar(0x20004,yaw);g_esp=0x20000;g_eax=0x60000;sub_0011E5E0();
        CHECK(g_esp==0x20008&&std::fabs(number(0x605F8)*seconds+.040f)<1e-6);
        g_fp_top=(g_fp_top+1)&7; // Original camera consumes the returned x87 value.
        word(0x20000,0x12345678);scalar(0x20004,pitch*2*seconds*.5f);g_esp=0x20000;g_eax=0x60000;sub_0011E840();
        CHECK(g_esp==0x20008&&std::fabs(number(0x605F4)-.025f)<1e-6);
        g_fp_top=(g_fp_top+1)&7;
    }
    mouse_x=mouse_y=0;bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);CHECK(!number(0x40018)&&!number(0x4001C));
    mouse_x=10000;bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);CHECK(number(0x4001C)<-32);
    word(0x300BC,1);scalar(0x4001C,7);bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);CHECK(number(0x4001C)==7);
    word(0x300BC,0);enabled=false;bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);CHECK(number(0x4001C)==7);
    enabled=true;scalar(0x40074,0);bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);CHECK(number(0x4001C)==7);
    scalar(0x40074,1.f/30);DWORD previous{};CHECK(VirtualProtect(memory+0x40000,4096,PAGE_READONLY,&previous));
    bf_mkb_camera_frame(0x50000,0x60000,0x40000,0x70000);CHECK(number(0x4001C)==7);
    CHECK(VirtualProtect(memory+0x40000,4096,previous,&previous));
    bf_mkb_camera_frame(0,0,0,0);bf_mkb_camera_frame(0xFFFFFFF0,0xFFFFFFF0,0xFFFFFFF0,0xFFFFFFF0);
    // Original pitch limit remains, even for a huge native displacement.
    g_esp=0x20000;g_eax=0x60000;scalar(0x20004,1000);sub_0011E840();CHECK(number(0x605F4)>1&&number(0x605F4)<1.05f);
    VirtualFree(memory,0,MEM_RELEASE);
    puts("Raw counts retain constant angular displacement across frame times, stop immediately, preserve original controller/pitch gates and reject invalid memory.");return 0;
}
