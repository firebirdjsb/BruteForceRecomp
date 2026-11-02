/* Exercise the actual privately patched raw-input poller. Only foreground,
 * cursor and clock OS calls are faked; no global desktop input is injected. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static double clock_ms;
static int focused = 1, captured;
static float sensitivity = 1.0f;
static HWND test_foreground(void) { return (HWND)(uintptr_t)1; }
static DWORD test_pid(HWND window, LPDWORD pid) { *pid = focused ? GetCurrentProcessId() : 0; return 1; }
static BOOL test_rect(HWND window, LPRECT rect) { return FALSE; }
static BOOL test_clip(const RECT *rect) { return TRUE; }
static int test_cursor(BOOL show) { return 0; }
static BOOL test_counter(LARGE_INTEGER *v) { v->QuadPart = (LONGLONG)(clock_ms * 1000); return TRUE; }
static BOOL test_frequency(LARGE_INTEGER *v) { v->QuadPart = 1000000; return TRUE; }
#define GetForegroundWindow test_foreground
#define GetWindowThreadProcessId test_pid
#define GetWindowRect test_rect
#define ClipCursor test_clip
#define ShowCursor test_cursor
#define QueryPerformanceCounter test_counter
#define QueryPerformanceFrequency test_frequency
#include "game-desktop-mkb.c"
int bf_text_active(void) { return 0; }
int bf_ui_capture(void) { return captured; }
float bf_ui_mouse_sensitivity(void) { return sensitivity; }
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } } while (0)
static XBOX_INPUT_STATE poll(void) {
    XBOX_INPUT_STATE state = {0}; mkb_merge(0, &state); return state;
}
int main(void) {
    _putenv_s("RECOMP_MKB", "1"); s_thread_up = 1;
    XBOX_INPUT_STATE state;
    const int keys[4] = {'W','S','A','D'};
    for (int i=0;i<4;i++) {
        memset((void *)s_vk_down,0,sizeof s_vk_down); s_vk_down[keys[i]]=1;
        state=poll();
        CHECK(abs(i<2?state.Gamepad.sThumbLY:state.Gamepad.sThumbLX)==32767);
        CHECK(i==0?state.Gamepad.sThumbLY>0:i==1?state.Gamepad.sThumbLY<0:i==2?state.Gamepad.sThumbLX<0:state.Gamepad.sThumbLX>0);
    }
    s_vk_down['W']=s_vk_down['S']=s_vk_down['A']=s_vk_down['D']=1;
    state=poll();CHECK(!state.Gamepad.sThumbLX&&!state.Gamepad.sThumbLY);
    s_vk_down['S']=s_vk_down['A']=0;
    state=poll();CHECK(state.Gamepad.sThumbLX==23170&&state.Gamepad.sThumbLY==23170);
    sensitivity=4.0f;state=poll();CHECK(state.Gamepad.sThumbLX==23170&&state.Gamepad.sThumbLY==23170);
    memset((void *)s_vk_down,0,sizeof s_vk_down);sensitivity=1.0f;
    InterlockedExchange(&s_mouse_btn,3);
    for (int i=0;i<200;i++) {state=poll();CHECK(!state.Gamepad.bAnalogButtons[6]&&state.Gamepad.bAnalogButtons[7]==255&&(state.Gamepad.wButtons&XBOX_GAMEPAD_RIGHT_THUMB));}
    InterlockedAnd(&s_mouse_btn,~1);state=poll();CHECK(!state.Gamepad.bAnalogButtons[7]&&(state.Gamepad.wButtons&XBOX_GAMEPAD_RIGHT_THUMB));
    InterlockedExchange(&s_mouse_btn,0);s_vk_down['G']=1;state=poll();CHECK(state.Gamepad.bAnalogButtons[6]==255);s_vk_down['G']=0;
    mkb_reset_aim();s_dx=4;clock_ms=100;state=poll();int aim=state.Gamepad.sThumbRX;CHECK(aim>0);
    for(int i=1;i<16;i++){clock_ms=100+i;state=poll();CHECK(state.Gamepad.sThumbRX==aim);}
    clock_ms=117;state=poll();CHECK(!state.Gamepad.sThumbRX);
    s_dx=4;clock_ms=134;state=poll();CHECK(abs(state.Gamepad.sThumbRX-aim)<400);
    CHECK(bf_mkb_capture_requested());
    captured=1;CHECK(!bf_mkb_capture_requested());s_dx=100;s_mouse_btn=1;state=poll();CHECK(!state.Gamepad.sThumbRX&&!state.Gamepad.bAnalogButtons[7]);
    captured=0;state=poll();CHECK(!state.Gamepad.sThumbRX&&!state.Gamepad.bAnalogButtons[7]);
    focused=0;CHECK(!bf_mkb_capture_requested());s_dx=100;poll();focused=1;state=poll();CHECK(!state.Gamepad.sThumbRX);
    s_active=0;CHECK(!bf_mkb_capture_requested());s_active=1;CHECK(bf_mkb_capture_requested());
    puts("Full-speed WASD, normalized diagonals, held fire/aim, stable mouse reports and focus/UI release passed.");
    return 0;
}
