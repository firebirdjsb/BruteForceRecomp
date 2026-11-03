/* Execute the privately translated combat thunk against a synthetic object.
 * This test contains no original code bytes, title vtable or title asset. */
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
ptrdiff_t g_xbox_mem_offset;
static unsigned char memory[65536];
static unsigned calls;
void sub_0001D8F0(void);
void sub_0002E490(void);
static unsigned script_calls[4];
/* Controlled callees at the recovered function's real call boundaries.
 * Unexpected paths fail immediately; these fixtures are never linked into
 * the player. Each simulated RET retains its original argument byte count. */
void sub_000120F0(void){if(g_eax!=0xFC0)abort();script_calls[0]++;g_eax=0x5000;g_esp+=4;}
void sub_0004E8F0(void){if(g_edi!=0x18F0)abort();script_calls[1]++;g_esp+=4;}
void sub_00023EE0(void){if(g_eax||g_esi!=0xFC0)abort();script_calls[2]++;g_eax=0;g_esp+=4;}
void sub_00140A60(void){
    uint32_t *args=(uint32_t*)(memory+g_esp);
    if(args[1]!=0x123456||args[2]!=0x5000||args[3]!=0x0F79CAE7||args[4])abort();
    script_calls[3]++;g_esp+=20; /* RET16: return DWORD plus four arguments. */
}
#define UNEXPECTED(name) void name(void){fprintf(stderr,"Unexpected fixture path: " #name "\n");abort();}
UNEXPECTED(sub_00012320) UNEXPECTED(sub_00012440) UNEXPECTED(sub_00023B20)
UNEXPECTED(sub_00023D00) UNEXPECTED(sub_00025730) UNEXPECTED(sub_0002E800)
UNEXPECTED(sub_000388B0) UNEXPECTED(sub_000416A0) UNEXPECTED(sub_0004D520)
UNEXPECTED(sub_0004DFF0) UNEXPECTED(sub_00055A60) UNEXPECTED(sub_00055AA0)
UNEXPECTED(sub_0008F6E0) UNEXPECTED(sub_0011DFA0)

static void method(void) {
    /* A tail jump retains ECX and the caller's return/argument slots. Only
     * the target method's RET pops the original four-byte return address. */
    if(g_ecx!=0x100||g_eax!=0x400||g_esp!=0x1800||
       *(uint32_t*)(memory+g_esp)!=0x12345678||
       *(uint32_t*)(memory+g_esp+4)!=0xCAFEBABE)abort();
    ++calls;g_eax=0xAABBCCDD;g_esp+=4;
}
recomp_func_t recomp_lookup(uint32_t va) {return va==0x123400?method:NULL;}
recomp_func_t recomp_lookup_manual(uint32_t va) {(void)va;return NULL;}
recomp_func_t recomp_lookup_kernel(uint32_t va) {(void)va;return NULL;}
void recomp_icall_fail_log(uint32_t va) {fprintf(stderr,"Unexpected unresolved target %08X\n",va);abort();}

int main(void) {
    g_xbox_mem_offset=(ptrdiff_t)memory;
    /* Synthetic 32-bit object -> vtable -> method at original thunk's +0x6DC.
     * 0x400+0x6DC remains inside the bounded arena; all slots are four bytes. */
    *(uint32_t*)(memory+0x100)=0x400;
    *(uint32_t*)(memory+0x400+0x6DC)=0x123400;
    *(uint32_t*)(memory+0x1800)=0x12345678;
    *(uint32_t*)(memory+0x1804)=0xCAFEBABE;
    g_ecx=0x100;g_esp=0x1800;g_ebx=11;g_esi=22;g_edi=33;g_ebp=44;g_seh_ebp=44;
    sub_0001D8F0();
    if(calls!=1||g_eax!=0xAABBCCDD||g_esp!=0x1804||g_ecx!=0x100||
       g_ebx!=11||g_esi!=22||g_edi!=33||g_ebp!=44||g_seh_ebp!=44)return 1;
    for(unsigned alignment=0;alignment<2;++alignment){
        memset(memory,0,sizeof memory);memset(script_calls,0,sizeof script_calls);
        /* A populated original ring with one neutral record exercises the
         * complete recovered entry, aligned locals and original epilogue. */
        *(uint32_t*)(memory+0x1000+0x3E0)=0x6000;
        *(uint32_t*)(memory+0x1000+0x3EC)=1;
        *(uint32_t*)(memory+0x6000)=0x7000;
        *(uint32_t*)(memory+0x5008)=0x5100;
        *(uint32_t*)(memory+0x5100+0xBE8)=0x123456;
        uint32_t stack=0xD000+alignment*4;
        *(uint32_t*)(memory+stack)=0x12345678;
        *(uint32_t*)(memory+stack+4)=0xCAFEBABE;
        memory[0x1350]=0xFF;
        g_ecx=0x1000;g_esp=stack;g_ebp=g_seh_ebp=0xBEEF;
        g_ebx=11;g_esi=22;g_edi=33;sub_0002E490();
        if(g_eax!=1||g_esp!=stack+4||g_ebx!=11||g_esi!=22||g_edi!=33||
           *(uint32_t*)(memory+stack+4)!=0xCAFEBABE||memory[0x1350]!=0xFA||
           *(uint32_t*)(memory+0x14A8)!=2)return 1;
        for(unsigned i=0;i<4;i++)if(script_calls[i]!=1)return 1;
    }
    puts("Recovered combat entries preserve dispatch, script output, aligned stack and callee-saved registers.");
    return 0;
}
