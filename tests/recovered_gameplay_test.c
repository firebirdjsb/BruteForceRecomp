/* Execute the privately translated combat thunk against a synthetic object.
 * This test contains no original code bytes, title vtable or title asset. */
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>

RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
ptrdiff_t g_xbox_mem_offset;
static unsigned char memory[8192];
static unsigned calls;
void sub_0001D8F0(void);

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
    puts("Recovered combat thunk dispatches once and preserves the original tail-call stack and registers.");
    return 0;
}
