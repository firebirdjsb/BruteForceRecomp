/* Original translated-helper fixture. It does not define, stub, or execute
 * sub_000D9F00 and cannot establish real rendering or edge geometry. */
#define RECOMP_GENERATED_CODE
#define BF_NATIVE_ASPECT_HELPER_FIXTURE
#include "recomp_funcs.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "native_aspect_original_helpers.inc"
#include "native_aspect_guest.inc"
#include "graphics/native_aspect_state.h"

RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp;
RECOMP_TLS uint32_t g_fs_base,g_seh_ebp,g_eflags,g_mxcsr;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp,g_df;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
RECOMP_TLS RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
ptrdiff_t g_xbox_mem_offset;
size_t g_xbox_total_ram=0x400000;
ptrdiff_t xbox_GetMemoryOffset(void) { return g_xbox_mem_offset; }

#define REQUIRE(condition) do { if(!(condition)) { fprintf(stderr,"aspect helper fixture failure line %d: %s\n",__LINE__,#condition); result=1; goto cleanup; } } while(0)

int main(void)
{
    const uint32_t camera=0x10000u,projection=0x12000u,context=0x14000u,stack=0x18000u;
    unsigned char *arena=NULL;
    unsigned char original_camera[0xB8],original_projection[0x58],original_stack[0x4C];
    BFAspectGuestState before,check;
    BFAspectTransaction transaction;
    int result=0,index;
    arena=VirtualAlloc(NULL,g_xbox_total_ram,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    REQUIRE(arena!=NULL);
    g_xbox_mem_offset=(ptrdiff_t)arena;
    memset(arena,0,g_xbox_total_ram);
    bf_native_aspect_fixture_load_constants(arena);
    MEM32(camera+0x50)=projection;
    MEMF(projection+0x1C)=0.1f; MEMF(projection+0x20)=1000.0f;
    MEMF(projection+0x24)=1.0f; MEMF(projection+0x28)=0.75f;
    MEMF(projection+0x2C)=4.0f/3.0f;
    MEMF(projection+0x30)=320.0f; MEMF(projection+0x34)=240.0f;
    g_fp_control_word=0x37Fu; g_fp_top=0; g_esp=stack;
    g_eax=projection; PUSH32(g_esp,0x0006FCE5u); sub_0006AD00();
    g_eax=projection; PUSH32(g_esp,0x0006FCE5u); sub_0006AD90();
    PUSH32(g_esp,camera); PUSH32(g_esp,0x0006FCE5u); sub_0006C840();
    REQUIRE(g_esp==stack);
    /* Sentinel stack bytes are distinct from the renderer's untouched args. */
    memset(arena+stack-0x40,0xA7,0x40);
    MEM32(stack)=0x0006FCE5u; MEM32(stack+4)=context; MEM32(stack+8)=camera;
    g_eax=0x11223344u; g_ecx=0x22334455u; g_edx=0x33445566u;
    g_ebx=0x44556677u; g_esi=0x55667788u; g_edi=0x66778899u;
    g_ebp=0x778899AAu; g_fs_base=0x10203040u; g_seh_ebp=0x20304050u;
    g_eflags=0x202u; g_mxcsr=0x1F80u; g_fp_cmp=-1; g_fp_cc=0x100u;
    g_df=0; g_fp_top=3;
    for(index=0;index<8;++index) g_fp_stack[index]=1.0+index*0.25;
    bf_aspect_save_state(&before);
    for(index=0;index<8;++index) {
        before.xmm[index].q[0]=0x0102030405060708ull+index;
        before.xmm[index].q[1]=0x1020304050607080ull+index;
        before.mmx[index].q=0x1122334455667788ull+index;
    }
    bf_aspect_load_state(&before);
    memcpy(original_camera,arena+camera,sizeof(original_camera));
    memcpy(original_projection,arena+projection,sizeof(original_projection));
    memcpy(original_stack,arena+stack-0x40,sizeof(original_stack));
    memset(&transaction,0,sizeof(transaction));
    bf_aspect_request(0);
    bf_aspect_scene_supported(1); bf_aspect_presenter_supported(1);
    REQUIRE(!bf_native_aspect_prepare(stack,&transaction));
    REQUIRE(!memcmp(original_projection,arena+projection,sizeof(original_projection)));
    bf_aspect_presenter_supported(0); bf_aspect_request(1);
    REQUIRE(!bf_native_aspect_prepare(stack,&transaction));
    bf_aspect_presenter_supported(1);
    /* Reject a split/small viewport before any write. */
    MEMF(projection+0x30)=160.0f;
    REQUIRE(!bf_native_aspect_prepare(stack,&transaction));
    MEMF(projection+0x30)=320.0f;
    REQUIRE(bf_native_aspect_prepare(stack,&transaction));
    REQUIRE(bf_aspect_call_original_helpers(&transaction));
    REQUIRE(bf_native_aspect_helpers_done(&transaction,1));
    REQUIRE(MEMF(projection+0x24)>1.3333f && MEMF(projection+0x24)<1.3334f);
    REQUIRE(MEMF(projection+0x28)==0.75f);
    REQUIRE(MEMF(projection+0x30)==320.0f && MEMF(projection+0x34)==240.0f);
    REQUIRE(!memcmp(original_stack,arena+stack-0x40,sizeof(original_stack)));
    bf_aspect_load_state(&before); bf_aspect_save_state(&check);
    REQUIRE(!memcmp(&before,&check,sizeof(before)));
    REQUIRE(bf_native_aspect_restore(&transaction));
    bf_aspect_save_state(&check);
    REQUIRE(!memcmp(&before,&check,sizeof(before)));
    REQUIRE(!memcmp(original_camera,arena+camera,sizeof(original_camera)));
    REQUIRE(!memcmp(original_projection,arena+projection,sizeof(original_projection)));
    REQUIRE(!memcmp(original_stack,arena+stack-0x40,sizeof(original_stack)));
    REQUIRE(bf_aspect_ready());
    bf_native_aspect_state_failure("fixture_failure_contract");
    REQUIRE(!bf_aspect_available() && !bf_aspect_ready());
    REQUIRE(!bf_native_aspect_prepare(stack,&transaction));
    REQUIRE(!memcmp(original_camera,arena+camera,sizeof(original_camera)));
    REQUIRE(!memcmp(original_projection,arena+projection,sizeof(original_projection)));
    bf_aspect_save_state(&check);
    REQUIRE(!memcmp(&before,&check,sizeof(before)));
    puts("PASS: original 6AD00/6AD90/C840 helpers; default OFF; request/support/viewport gate; unchanged vertical/viewport; exact stack/TLS/owned projection/frustum restoration; invariant failure clears shared capability. Actual D9F00 rendering was not tested.");
cleanup:
    if(arena) VirtualFree(arena,0,MEM_RELEASE);
    return result;
}
