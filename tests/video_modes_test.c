/* Execute the original selector against original private mode/format tables.
 * Only the encoder call is relayed to the actual patched host AV function. */
#include "kernel.h"
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo=0x10000,g_xbox_code_hi=0x380000;
volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_trace_idx;
volatile uint64_t g_icall_count;
static unsigned char memory[4*1024*1024];
static ULONG capabilities;
void sub_00378439(void);
void bf_video_fixture_data(unsigned char *);
void sub_00374D57(void) { abort(); } /* No 50-Hz timing branch in these NTSC cases. */
#define WORD(at) (*(uint32_t*)(memory+(at)))
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static void encoder(void) {
    if(WORD(g_esp+8)!=AV_OPTION_QUERY_AVPACK)abort();
    WORD(WORD(g_esp+16))=capabilities;
    g_esp+=20; /* Original stdcall: four DWORD arguments plus return address. */
}
recomp_func_t recomp_lookup(uint32_t va) { return va==0xFE000100u?encoder:NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_kernel(uint32_t va) { (void)va; return NULL; }
void recomp_icall_fail_log(uint32_t va) { fprintf(stderr,"Unexpected target %08X\n",va);abort(); }
void recomp_icall_not_code_log(uint32_t va) { fprintf(stderr,"Unexpected code target %08X\n",va);abort(); }
static int select_mode(unsigned w,unsigned h,unsigned flags,unsigned expected) {
    memset(memory,0,sizeof memory);bf_video_fixture_data(memory);
    WORD(0x38635C)=0xFE000100u; /* Original encoder thunk slot. */
    const unsigned stack=0x20000,device=0x30000;
    WORD(stack)=0xBEEFF00Du;WORD(stack+4)=w;WORD(stack+8)=h;WORD(stack+12)=0;
    WORD(stack+16)=flags;WORD(stack+20)=0x12;WORD(stack+24)=0;WORD(stack+28)=0x12345678u;
    g_esi=device;g_ebx=11;g_edi=22;g_ebp=g_seh_ebp=0x40000;g_esp=stack;
    sub_00378439();
    if(g_eax!=expected||g_esp!=stack+32||g_ebx!=11||g_edi!=22)
        fprintf(stderr,"mode %ux%u flags=%08X caps=%08lX: eax=%08X expected=%08X esp=%08X ebx=%u edi=%u\n",w,h,flags,(unsigned long)capabilities,g_eax,expected,g_esp,g_ebx,g_edi);
    CHECK(g_eax==expected&&g_esp==stack+32&&g_ebx==11&&g_edi==22);
    if(!expected)CHECK(WORD(device+8)!=0&&WORD(device+0xC)==0x12);
    else CHECK(WORD(device+8)==0); /* Failure must not publish a display mode. */
    return 0;
}
int main(void) {
    g_xbox_mem_offset=(ptrdiff_t)memory;
    ULONG fixed;
    xbox_AvSendTVEncoderOption(NULL,AV_OPTION_QUERY_AVPACK,0,&fixed);
    capabilities=fixed&~0x000A0000u;
    CHECK(!select_mode(640,480,0,0));
    CHECK(!select_mode(1280,720,0,0x80004005u));
    CHECK(!select_mode(1280,720,0x10,0x80004005u));
    capabilities=fixed;
    CHECK(!select_mode(640,480,0,0));
    /* This profile does not advertise option-6 widescreen bit16. The
     * original 480p path also requires it; preserve that rejection. */
    CHECK(!select_mode(640,480,0x10,0x80004005u));
    CHECK(!select_mode(1280,720,0,0x80004005u));
    CHECK(!select_mode(1280,720,0x10,0));
    CHECK(!select_mode(1920,1080,0x10,0x80004005u));
    CHECK(!select_mode(1024,768,0x10,0x80004005u));
    puts("Original retail selector: 480 unchanged; 720p needs both HD caps and progressive request; invalid modes remain rejected.");
    return 0;
}
