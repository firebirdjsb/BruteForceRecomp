/* Exact guest ABI adapter. The original keyboard methods still own editing,
 * limits, sounds and confirmation. Host input is applied after Update returns,
 * so closing the dialog cannot make us update its freed object afterward. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <string.h>
void bf_original_keyboard_draw(void);
void bf_original_keyboard_update(void);
void bf_game_text_seen(uint32_t keyboard);
void bf_game_text_tick(uint32_t keyboard);
void sub_0010D660(void);
void sub_0010D9D0(void);
void sub_0010DC20(void);

typedef struct {
    uint32_t integer[12];
    int direction, top, comparison;
    uint16_t control, status;
    double fp[8];
    RecompXmm xmm[8];
    RecompMmx mm[8];
} BfGuestRegisters;
static void save_registers(BfGuestRegisters *s) {
    uint32_t r[] = {g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_esp,g_ebp,g_seh_ebp,g_eflags,g_mxcsr,g_fs_base};
    RecompXmm x[] = {g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    RecompMmx m[] = {g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};
    memcpy(s->integer,r,sizeof r);memcpy(s->xmm,x,sizeof x);memcpy(s->mm,m,sizeof m);
    memcpy(s->fp,g_fp_stack,sizeof s->fp);
    s->direction=g_df;s->top=g_fp_top;s->comparison=g_fp_cmp;
    s->control=g_fp_control_word;s->status=g_fp_cc;
}
static void restore_registers(const BfGuestRegisters *s) {
    g_eax=s->integer[0];g_ecx=s->integer[1];g_edx=s->integer[2];g_ebx=s->integer[3];
    g_esi=s->integer[4];g_edi=s->integer[5];g_esp=s->integer[6];g_ebp=s->integer[7];
    g_seh_ebp=s->integer[8];g_eflags=s->integer[9];g_mxcsr=s->integer[10];g_fs_base=s->integer[11];
    g_xmm0=s->xmm[0];g_xmm1=s->xmm[1];g_xmm2=s->xmm[2];g_xmm3=s->xmm[3];
    g_xmm4=s->xmm[4];g_xmm5=s->xmm[5];g_xmm6=s->xmm[6];g_xmm7=s->xmm[7];
    g_mm0=s->mm[0];g_mm1=s->mm[1];g_mm2=s->mm[2];g_mm3=s->mm[3];
    g_mm4=s->mm[4];g_mm5=s->mm[5];g_mm6=s->mm[6];g_mm7=s->mm[7];
    memcpy(g_fp_stack,s->fp,sizeof s->fp);
    g_df=s->direction;g_fp_top=s->top;g_fp_cmp=s->comparison;
    g_fp_control_word=s->control;g_fp_cc=s->status;
}
void sub_0010D560(void) {
    const uint32_t keyboard=g_ecx;
    bf_original_keyboard_draw();
    bf_game_text_seen(keyboard);
}
void sub_0010D5A0(void) {
    const uint32_t keyboard=g_ecx;
    BfGuestRegisters state;
    bf_original_keyboard_update();
    save_registers(&state);
    bf_game_text_tick(keyboard);
    restore_registers(&state);
}
int bf_guest_keyboard_activate(uint32_t keyboard, uint32_t column, uint32_t row) {
    uint32_t stack=g_esp;
    // Original 0x0010D660: ESI=keyboard, EAX=column, ECX=row, plain RET.
    g_esi=keyboard;g_eax=column;g_ecx=row;
    PUSH32(g_esp,0x0010D860u);sub_0010D660();
    if (g_esp!=stack) return 0;
    // Original 0x0010D9D0: one DWORD keyboard argument, RET 4. The return
    // slot names its real controller-input caller at 0x0010DC3F.
    PUSH32(g_esp,keyboard);PUSH32(g_esp,0x0010DC3Fu);sub_0010D9D0();
    return g_esp==stack;
}
int bf_guest_keyboard_focus(uint32_t keyboard, uint32_t column, uint32_t row) {
    const uint32_t stack=g_esp;
    g_esi=keyboard;g_eax=column;g_ecx=row;
    PUSH32(g_esp,0x0010D860u);sub_0010D660();
    return g_esp==stack;
}
void bf_original_menu_draw(void);
void bf_game_menu_tick(uint32_t view);
void *bf_game_tutorial_begin(uint32_t view);
void bf_game_tutorial_end(void *token);
void sub_000FFDC0(void);
void sub_00102E60(void);
void sub_00100910(void) {
    const uint32_t view=g_ecx;
    BfGuestRegisters state;
    void *tutorial=bf_game_tutorial_begin(view);
    bf_original_menu_draw();
    bf_game_tutorial_end(tutorial);
    save_registers(&state);
    bf_game_menu_tick(view);
    restore_registers(&state);
}
int bf_guest_menu_focus(uint32_t view, uint32_t index) {
    const uint32_t stack=g_esp;
    // Original 0x000FFDC0: EDI=view, EDX=child index; plain RET. This
    // unfocuses siblings and notifies the game's original focus callbacks.
    g_edi=view;g_edx=index;PUSH32(g_esp,0x000FFCE4u);sub_000FFDC0();
    return g_esp==stack && (g_eax&255)!=0;
}
int bf_guest_list_focus(uint32_t list, uint32_t index) {
    const uint32_t stack=g_esp;
    // Original list selection 0x00102E60: ESI=list, ECX=index; plain RET.
    // It owns row highlighting, scrolling, bounds and selection callbacks.
    g_esi=list;g_ecx=index;PUSH32(g_esp,0x0010286Eu);sub_00102E60();
    return g_esp==stack;
}
int bf_guest_keyboard_cancel(uint32_t keyboard) {
    uint32_t stack=g_esp;
    // Original 0x0010DC20: ECX=this, DWORD controller mask, RET 4; bit 1=B.
    g_ecx=keyboard;PUSH32(g_esp,2u);PUSH32(g_esp,0u);sub_0010DC20();
    return g_esp==stack;
}
