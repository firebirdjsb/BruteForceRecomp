/* Exact guest ABI adapter. The original keyboard methods still own editing,
 * limits, sounds and confirmation. Host input is applied after Update returns,
 * so closing the dialog cannot make us update its freed object afterward. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <string.h>
#include "ui/scene_boundary.h"
#include "ui/hud_anchor.h"
#include "graphics/native_aspect_state.h"
#include "gpu/temporal_trace.h"
#include <fenv.h>
int bf_ui_menu_active(void);
void bf_ui_prompt_anchor(unsigned);
static unsigned s_guest_ui_anchor=BF_HUD_CENTER;
static BfHudOwnerScope s_hud_owner_scope={0,BF_HUD_CENTER};
static uint32_t s_hud_trace_token;
static int bf_guest_marker(uint32_t marker);
static void bf_guest_anchor(unsigned anchor) {
    if(anchor>BF_HUD_RIGHT)anchor=BF_HUD_CENTER;
    if(anchor!=s_guest_ui_anchor&&bf_guest_marker(BF_UI_ANCHOR_LEFT+anchor))s_guest_ui_anchor=anchor;
    bf_ui_prompt_anchor(s_guest_ui_anchor);
}
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
uint32_t bf_guest_hud_trace_begin(unsigned stage,uint32_t component) {
    if(!bf_hud_trace_configured())return 0;
    BfGuestRegisters state;save_registers(&state);
    const uint32_t token=bf_hud_trace_begin(stage,g_esp,component);
    restore_registers(&state);return token;
}
void bf_guest_hud_trace_end(uint32_t token) {
    if(!token)return;
    BfGuestRegisters state;save_registers(&state);bf_hud_trace_end(token);restore_registers(&state);
}
int bf_game_ui_scratch_writable(uint32_t,unsigned);
void sub_001012E0(void);
int bf_guest_view_origin(uint32_t view,float *x,float *y) {
    BfGuestRegisters state;save_registers(&state);
    const uint32_t stack=g_esp;
    if(!x||!y||stack<0x10008u||!bf_game_ui_scratch_writable(stack-8,8))return 0;
    const uint32_t output=stack-8;g_esp=output;
    // Exact original virtual GetPosition: ECX=view, DWORD output pointer,
    // RET 4. Original parent call returns at 0010130B. Its safe-area/player
    // viewport selection remains original, including split-screen ownership.
    g_ecx=view;PUSH32(g_esp,output);PUSH32(g_esp,0x0010130Bu);sub_001012E0();
    const int valid=g_esp==output;
    if(valid){*x=MEMF(output);*y=MEMF(output+4);}
    restore_registers(&state);return valid;
}
void sub_00371E10(void);
uint32_t bf_prompt_text_frame(uint32_t frame);
void bf_original_font_draw(void);
void sub_000CCD20(void) {
    // Original font draw has ten DWORD arguments and RET 40. A rewritten
    // prompt uses a private copy of those 44 bytes below the caller's stack;
    // source strings and caller arguments remain untouched. Relocate only
    // the resulting ESP, preserving the original body's cleanup/error delta.
    const uint32_t trace_previous=s_hud_trace_token,trace_token=bf_guest_hud_trace_begin(3,0);
    s_hud_trace_token=trace_token;
    const uint32_t original=g_esp,relay=bf_prompt_text_frame(original);
    if(relay)g_esp=relay;
    bf_original_font_draw();
    if(relay)g_esp=original+(g_esp-relay);
    bf_guest_hud_trace_end(trace_token);s_hud_trace_token=trace_previous;
}
void bf_original_hud_image(void);
void bf_original_ui_quad(void);
/* Leaf wrappers only collect optional diagnostics. The complete component
 * scope owns every sprite/font/raw draw; leaves never reclassify its anchor. */
void sub_000BA950(void) {
    const uint32_t trace_previous=s_hud_trace_token,trace_token=bf_guest_hud_trace_begin(1,g_ecx);
    s_hud_trace_token=trace_token;
    bf_original_hud_image();
    bf_guest_hud_trace_end(trace_token);s_hud_trace_token=trace_previous;
}
void sub_000EE6E0(void) {
    const uint32_t trace_previous=s_hud_trace_token,trace_token=bf_guest_hud_trace_begin(2,0);
    s_hud_trace_token=trace_token;
    bf_original_ui_quad();
    bf_guest_hud_trace_end(trace_token);s_hud_trace_token=trace_previous;
}
int bf_guest_hud_trace_active(void) {return s_hud_trace_token!=0;}
void bf_guest_hud_trace_rect(float x,float width) {
    BfGuestRegisters state;save_registers(&state);
    if(s_hud_trace_token)bf_hud_trace_rect(s_hud_trace_token,x,width,s_guest_ui_anchor);
    restore_registers(&state);
}
void bf_guest_ui_boundary(void);
uint32_t bf_guest_hud_owner_begin(uint32_t component) {
    BfGuestRegisters state;uint32_t token;unsigned anchor;
    if(!bf_aspect_ready())return 0;
    save_registers(&state);
    anchor=s_hud_owner_scope.anchor;
    if(!s_hud_owner_scope.depth) {
        // Raw geometry can precede the first sprite/font in this owner.
        bf_guest_ui_boundary();
        anchor=bf_hud_owner_anchor(component,bf_ui_menu_active());
    }
    token=bf_hud_scope_enter(&s_hud_owner_scope,anchor);
    if(token) {
        bf_guest_anchor(s_hud_owner_scope.anchor);
        // If a validated marker reservation fails, all descendants retain
        // the actual prior anchor together instead of retrying per leaf.
        s_hud_owner_scope.anchor=s_guest_ui_anchor;
    }
    restore_registers(&state);return token;
}
void bf_guest_hud_owner_end(uint32_t token) {
    BfGuestRegisters state;if(!token)return;
    save_registers(&state);bf_hud_scope_leave(&s_hud_owner_scope,token);
    bf_guest_anchor(s_hud_owner_scope.anchor);s_hud_owner_scope.anchor=s_guest_ui_anchor;
    restore_registers(&state);
}
void bf_guest_ui_boundary(void) {
    BfGuestRegisters state;save_registers(&state);
    // Authenticated retail D3D global; the device and pushbuffer allocations
    // are resolved anew. The helper validates all pages before writing.
    uint32_t device=MEM32(0x0037DD88u);
    if(bf_ui_try_scene_marker(device)==2) {
        const uint32_t bytes=MEM32(0x0037EC88u);
        if(bytes>=16&&bytes<=0x100000) {
            // Exact original 0036CD63 reserve-space ABI: two DWORD args,
            // original callee 00371E10 returns the cursor in EAX, RET 8.
            PUSH32(g_esp,bytes);PUSH32(g_esp,bytes>>1);
            PUSH32(g_esp,0x0036CD71u);sub_00371E10();
            if(g_esp==state.integer[6])bf_ui_try_scene_marker(MEM32(0x0037DD88u));
        }
    }
    restore_registers(&state);
}
/* Ordered camera/render scope. Every original register is restored, including
 * the original reserve-space call's ESP delta. The command changes only the
 * native backend's framing metadata; it is not a hardware Xbox method. */
static int bf_guest_marker(uint32_t marker) {
    BfGuestRegisters state;save_registers(&state);
    int result=bf_ui_try_marker(MEM32(0x0037DD88u),marker);
    if(result==2) {
        const uint32_t bytes=MEM32(0x0037EC88u);
        if(bytes>=16&&bytes<=0x100000&&state.integer[6]>=0x1000Cu&&
           bf_game_ui_scratch_writable(state.integer[6]-12,12)) {
            PUSH32(g_esp,bytes);PUSH32(g_esp,bytes>>1);
            PUSH32(g_esp,0x0036CD71u);sub_00371E10();
            if(g_esp==state.integer[6])result=bf_ui_try_marker(MEM32(0x0037DD88u),marker);
            else result=0;
        } else result=0;
    }
    restore_registers(&state);
    return result==1;
}
int bf_guest_aspect_marker(int active) {
    return bf_guest_marker(active?BF_SCENE_WIDE_BEGIN:BF_SCENE_WIDE_END);
}
static int bf_guest_motion_emit(const BFMotionRecord *record) {
    int result=bf_ui_try_motion_record(MEM32(0x0037DD88u),record),before=result;
    uint32_t bytes=0,minimum=0,preferred=0,stack=g_esp;
    if(result==2) {
        bytes=MEM32(0x0037EC88u);
        // The injected call block is 12 bytes: return PC, minimum BYTE window,
        // preferred BYTE window. Original 00371E10 owns its own deeper stack
        // frame and RET 8. Its watermark subtracts the exact 0x204-byte guard.
        if(bf_motion_reservation_plan(bytes,&minimum,&preferred)&&stack>=0x1000Cu&&bf_game_ui_scratch_writable(stack-12,12)) {
            PUSH32(g_esp,preferred);PUSH32(g_esp,minimum);
            PUSH32(g_esp,0x0036CD71u);sub_00371E10();
            result=g_esp==stack?bf_ui_try_motion_record(MEM32(0x0037DD88u),record):0;
        } else result=0;
    }
    if(before==2||result!=1)bf_motion_reservation_log(MEM32(0x0037DD88u),stack,bytes,minimum,preferred,before,result);
    if(result!=1)bf_motion_trace_failure("guest_pushbuffer_reservation");
    return result==1;
}
void bf_guest_motion_lifetime(uint32_t object,int create) {
    BfGuestRegisters state;fenv_t env;save_registers(&state);fegetenv(&env);
    bf_motion_lifetime(object,create);restore_registers(&state);fesetenv(&env);
}
void bf_guest_motion_scene_lifetime(uint32_t object,int create) {
    BfGuestRegisters state;fenv_t env;save_registers(&state);fegetenv(&env);
    bf_motion_scene_lifetime(object,create);restore_registers(&state);fesetenv(&env);
}
int bf_guest_motion_view_begin(void) {
    if(!bf_motion_observer_enabled())return 0;
    BfGuestRegisters state;BFMotionRecord record;int result=0;fenv_t env;
    save_registers(&state);fegetenv(&env);
    if(bf_motion_record_view(g_esp,&record))result=bf_guest_motion_emit(&record);
    restore_registers(&state);fesetenv(&env);return result;
}
void bf_guest_motion_end(unsigned kind) {
    if(!bf_motion_observer_enabled())return;
    BfGuestRegisters state;BFMotionRecord record;fenv_t env;
    save_registers(&state);fegetenv(&env);
    if(bf_motion_record_end(kind,&record))bf_guest_motion_emit(&record);
    restore_registers(&state);fesetenv(&env);
}
void bf_guest_motion_part_begin(uint32_t packet,uint32_t descriptor,uint32_t subdraw) {
    if(!bf_motion_observer_enabled())return;
    BfGuestRegisters state;BFMotionRecord record;fenv_t env;
    save_registers(&state);fegetenv(&env);
    if(bf_motion_record_part(packet,descriptor,subdraw,&record))bf_guest_motion_emit(&record);
    restore_registers(&state);fesetenv(&env);
}
unsigned bf_guest_motion_object_begin(uint32_t object,uint32_t target,uint32_t callsite) {
    if(!bf_motion_observer_enabled())return 0;
    BfGuestRegisters state;BFMotionRecord record;int active=0;fenv_t env;
    save_registers(&state);fegetenv(&env);
    if(bf_motion_record_object(object,target,callsite,&record))active=bf_guest_motion_emit(&record);
    restore_registers(&state);fesetenv(&env);return active;
}
void bf_original_motion_model_draw(void);
void bf_original_motion_scene_draw(void);
void sub_001F5600(void) {
    BfGuestRegisters state;BFMotionRecord record;int active=0;fenv_t env;
    if(bf_motion_observer_enabled()) {
        save_registers(&state);fegetenv(&env);
        if(bf_motion_record_scene(g_esp,&record))active=bf_guest_motion_emit(&record);
        restore_registers(&state);fesetenv(&env);
    }
    bf_original_motion_scene_draw();
    if(active)bf_guest_motion_end(BF_MOTION_MODEL_END);
}
void sub_00157B70(void) {
    BfGuestRegisters state;BFMotionRecord record;int active=0;fenv_t env;
    if(bf_motion_observer_enabled()) {
        save_registers(&state);fegetenv(&env);
        if(bf_motion_record_model(g_ecx,&record))active=bf_guest_motion_emit(&record);
        restore_registers(&state);fesetenv(&env);
    }
    bf_original_motion_model_draw();
    if(active)bf_guest_motion_end(BF_MOTION_MODEL_END);
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
