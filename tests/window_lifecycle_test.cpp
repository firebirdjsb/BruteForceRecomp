#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#include <cstdio>
static DWORD WINAPI fb_thread(void *);
#include "fb-window-lifecycle.inc"
static volatile LONG started,finished,unmapped,unsafe_access;
static DWORD WINAPI fb_thread(void *) {
    InterlockedIncrement(&started);
    while(InterlockedCompareExchange(&s_fb_running,0,0))Sleep(1);
    // Model a presentation operation still owning guest/GPU resources after
    // stop was requested. Shutdown must join before unmapping those resources.
    Sleep(40);
    if(InterlockedCompareExchange(&unmapped,0,0))InterlockedIncrement(&unsafe_access);
    InterlockedIncrement(&finished);return 0;
}
int main(){
    _putenv_s("RECOMP_FB_WINDOW","");xbox_FramebufferWindowStart();
    if(s_fb_thread||started)return 1;
    DWORD before{},after{};if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 2;
    _putenv_s("RECOMP_FB_WINDOW","1");xbox_FramebufferWindowStart();xbox_FramebufferWindowStart();
    for(unsigned i=0;i<1000&&!started;++i)Sleep(1);
    if(started!=1)return 3;
    xbox_FramebufferWindowStop();InterlockedExchange(&unmapped,1);
    if(finished!=1||unsafe_access||s_fb_thread||s_fb_running)return 4;
    xbox_FramebufferWindowStop();xbox_FramebufferWindowStart();
    if(started!=1||s_fb_thread)return 5;
    if(!GetProcessHandleCount(GetCurrentProcess(),&after)||after>before)return 6;
    std::puts("Window worker joins before resource teardown, rejects duplicate/restart and releases its thread handle.");return 0;
}
