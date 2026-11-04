/* Included only by the authenticated build-local copy of generated chunk 0023.
 * The generated header provides the exact register types used below. */
#include <windows.h>
#include <fenv.h>
#include "graphics/native_aspect.h"
#ifndef BF_NATIVE_ASPECT_HELPER_FIXTURE
extern int bf_guest_aspect_marker(int active);
#endif

typedef struct BFAspectGuestState {
    uint32_t integer[8]; /* EAX, ECX, EDX, ESP, EBX, ESI, EDI, EBP: 8 x 4 bytes. */
    uint32_t fs_base, seh_frame, eflags, mxcsr;
    int fp_top, fp_compare, direction;
    uint16_t fp_control, fp_condition;
    double fp[8]; /* Exact 8 x double emulated x87 storage, not host x87 slots. */
    RecompXmm xmm[8]; /* Exact 8 x 16-byte generated SSE register union. */
    RecompMmx mmx[8]; /* Exact 8 x 8-byte generated MMX register union. */
} BFAspectGuestState;
_Static_assert(sizeof(RecompXmm) == 16, "Unexpected generated XMM layout");
_Static_assert(sizeof(RecompMmx) == 8, "Unexpected generated MMX layout");
_Static_assert(sizeof(g_fp_stack) == 64, "Unexpected generated x87 layout");

static void bf_aspect_save_state(BFAspectGuestState *state)
{
    /* Zero padding so byte comparison checks every declared state value. */
    memset(state, 0, sizeof(*state));
    state->integer[0]=g_eax; state->integer[1]=g_ecx;
    state->integer[2]=g_edx; state->integer[3]=g_esp;
    state->integer[4]=g_ebx; state->integer[5]=g_esi;
    state->integer[6]=g_edi; state->integer[7]=g_ebp;
    state->fs_base=g_fs_base; state->seh_frame=g_seh_ebp;
    state->eflags=g_eflags; state->mxcsr=g_mxcsr;
    state->fp_top=g_fp_top; state->fp_compare=g_fp_cmp;
    state->direction=g_df; state->fp_control=g_fp_control_word;
    state->fp_condition=g_fp_cc;
    memcpy(state->fp,g_fp_stack,sizeof(state->fp));
    state->xmm[0]=g_xmm0; state->xmm[1]=g_xmm1;
    state->xmm[2]=g_xmm2; state->xmm[3]=g_xmm3;
    state->xmm[4]=g_xmm4; state->xmm[5]=g_xmm5;
    state->xmm[6]=g_xmm6; state->xmm[7]=g_xmm7;
    state->mmx[0]=g_mm0; state->mmx[1]=g_mm1;
    state->mmx[2]=g_mm2; state->mmx[3]=g_mm3;
    state->mmx[4]=g_mm4; state->mmx[5]=g_mm5;
    state->mmx[6]=g_mm6; state->mmx[7]=g_mm7;
}

static void bf_aspect_load_state(const BFAspectGuestState *state)
{
    g_eax=state->integer[0]; g_ecx=state->integer[1];
    g_edx=state->integer[2]; g_esp=state->integer[3];
    g_ebx=state->integer[4]; g_esi=state->integer[5];
    g_edi=state->integer[6]; g_ebp=state->integer[7];
    g_fs_base=state->fs_base; g_seh_ebp=state->seh_frame;
    g_eflags=state->eflags; g_mxcsr=state->mxcsr;
    g_fp_top=state->fp_top; g_fp_cmp=state->fp_compare;
    g_df=state->direction; g_fp_control_word=state->fp_control;
    g_fp_cc=state->fp_condition;
    memcpy(g_fp_stack,state->fp,sizeof(state->fp));
    g_xmm0=state->xmm[0]; g_xmm1=state->xmm[1];
    g_xmm2=state->xmm[2]; g_xmm3=state->xmm[3];
    g_xmm4=state->xmm[4]; g_xmm5=state->xmm[5];
    g_xmm6=state->xmm[6]; g_xmm7=state->xmm[7];
    g_mm0=state->mmx[0]; g_mm1=state->mmx[1];
    g_mm2=state->mmx[2]; g_mm3=state->mmx[3];
    g_mm4=state->mmx[4]; g_mm5=state->mmx[5];
    g_mm6=state->mmx[6]; g_mm7=state->mmx[7];
}

static int bf_aspect_call_original_helpers(const BFAspectTransaction *transaction)
{
    /* Both exact translated bodies contain no further guest calls. Their
     * deepest write is entry ESP-0x2C, inside the validated saved 0x40 bytes.
     * Synthetic returns use an authenticated existing caller return address;
     * generated RET advances ESP and never transfers control through it. */
    __try {
        g_eax=transaction->projection;
        PUSH32(g_esp, transaction->return_address);
        RECOMP_ABI_CALL(0x0006AD00u,sub_0006AD00);
        if(g_esp != transaction->entry_stack) return 0;
        PUSH32(g_esp, transaction->camera);
        PUSH32(g_esp, transaction->return_address);
        RECOMP_ABI_CALL(0x0006C840u,sub_0006C840);
        return g_esp == transaction->entry_stack;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

#ifndef BF_NATIVE_ASPECT_HELPER_FIXTURE
static void bf_native_aspect_dispatch(void)
{
    BFAspectGuestState incoming, restored, actual_return, final_return;
    BFAspectTransaction transaction;
    fenv_t incoming_host_environment, return_host_environment;
    int helpers_ok, armed;
    memset(&transaction,0,sizeof(transaction));
    bf_aspect_save_state(&incoming);
    fegetenv(&incoming_host_environment);
    /* The controller samples the shared request/support state once. */
    armed=bf_native_aspect_prepare(g_esp,&transaction);
    if(armed) {
        helpers_ok=bf_aspect_call_original_helpers(&transaction);
        armed=bf_native_aspect_helpers_done(&transaction,helpers_ok);
    }
    /* Original D9F00 receives the exact original args, GPR/frame/flag state,
     * x87/MMX/XMM image and host floating environment, whether armed or not. */
    bf_aspect_load_state(&incoming);
    fesetenv(&incoming_host_environment);
    bf_aspect_save_state(&restored);
    if(memcmp(&incoming,&restored,sizeof(incoming)) != 0) {
        bf_native_aspect_state_failure("entry_tls");
        bf_native_aspect_restore(&transaction);
        bf_aspect_load_state(&incoming);
        armed=0;
    }
#ifndef BF_NATIVE_ASPECT_HELPER_FIXTURE
    /* A failed ordered BEGIN must leave the actual draw in authored 4:3. */
    if(armed && !bf_guest_aspect_marker(1)) {
        bf_native_aspect_restore(&transaction);
        bf_aspect_load_state(&incoming);
        fesetenv(&incoming_host_environment);
        armed=0;
    }
    if(armed) {
        bf_aspect_save_state(&restored);
        if(memcmp(&incoming,&restored,sizeof(incoming)) != 0) {
            bf_guest_aspect_marker(0);
            bf_native_aspect_state_failure("begin_marker_tls");
            bf_native_aspect_restore(&transaction);
            bf_aspect_load_state(&incoming);
            armed=0;
        }
    }
#endif
    fesetenv(&incoming_host_environment);
    /* This is the actual unchanged translated renderer; no render stub. */
    sub_000D9F00();
    bf_aspect_save_state(&actual_return);
    fegetenv(&return_host_environment);
    if((uint64_t)g_esp != (uint64_t)incoming.integer[3]+12u)
        bf_native_aspect_state_failure("original_dispatch_ret8_stack");
#ifndef BF_NATIVE_ASPECT_HELPER_FIXTURE
    if(armed && !bf_guest_aspect_marker(0)) bf_native_aspect_state_failure("ordered_end_marker");
#endif
    if(armed) bf_native_aspect_restore(&transaction);
    bf_aspect_save_state(&final_return);
    if(memcmp(&actual_return,&final_return,sizeof(actual_return)) != 0)
        bf_native_aspect_state_failure("return_tls");
    /* Preserve D9F00's real return state, including its ESP += 12 (RET 8).
     * Never restore pre-dispatch scratch here: D9F00 owns its stack writes. */
    bf_aspect_load_state(&actual_return);
    fesetenv(&return_host_environment);
}
#endif
