# Included only after frame_handoff.cmake authenticates the pinned source.
set(fb_lifetime_fields [=[static volatile LONG s_fb_running;
static volatile LONG s_fb_shutdown;
static HANDLE s_fb_thread;
static SRWLOCK s_fb_lifetime_lock = SRWLOCK_INIT;]=])
string(REPLACE "static volatile LONG s_fb_running;" "${fb_lifetime_fields}" source "${source}")
string(REPLACE "if (!getenv(\"RECOMP_CLOSE_KEEPS_RUNNING\")) {"
    "if (!s_fb_shutdown && !getenv(\"RECOMP_CLOSE_KEEPS_RUNNING\")) {" source "${source}")
set(old_start [=[void xbox_FramebufferWindowStart(void)
{
    HANDLE th;

    if (!getenv("RECOMP_FB_WINDOW"))
        return;
    if (InterlockedCompareExchange(&s_fb_running, 1, 0) != 0)
        return;
    th = CreateThread(NULL, 0, fb_thread, NULL, 0, NULL);
    if (th)
        CloseHandle(th);
    else
        InterlockedExchange(&s_fb_running, 0);
}]=])
set(fb_lifetime_functions [=[void xbox_FramebufferWindowStart(void)
{
    if (!getenv("RECOMP_FB_WINDOW")) return;
    AcquireSRWLockExclusive(&s_fb_lifetime_lock);
    if (!s_fb_shutdown && !s_fb_thread &&
        InterlockedCompareExchange(&s_fb_running, 1, 0) == 0) {
        s_fb_thread = CreateThread(NULL, 0, fb_thread, NULL, 0, NULL);
        if (!s_fb_thread) InterlockedExchange(&s_fb_running, 0);
    }
    ReleaseSRWLockExclusive(&s_fb_lifetime_lock);
}

/* Stop presentation before guest unmapping and C++ static destruction.
 * Keep the real thread handle until it has released GPU/window resources.
 * A shutdown request also prevents later producers from restarting it. */
void xbox_FramebufferWindowStop(void)
{
    HANDLE thread;
    AcquireSRWLockExclusive(&s_fb_lifetime_lock);
    InterlockedExchange(&s_fb_shutdown, 1);
    InterlockedExchange(&s_fb_running, 0);
    thread = s_fb_thread;
    s_fb_thread = NULL;
    ReleaseSRWLockExclusive(&s_fb_lifetime_lock);
    if (thread) {
        if (GetThreadId(thread) != GetCurrentThreadId())
            WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
}]=])
string(FIND "${source}" "${old_start}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Expected exact framebuffer window thread startup.")
endif()
string(REPLACE "${old_start}" "${fb_lifetime_functions}" source "${source}")
# Exercise the exact retained-handle implementation with a controlled worker.
file(WRITE "${CMAKE_BINARY_DIR}/runtime-fixes/fb-window-lifecycle.inc.in"
    "${fb_lifetime_fields}\n${fb_lifetime_functions}\n")
configure_file("${CMAKE_BINARY_DIR}/runtime-fixes/fb-window-lifecycle.inc.in"
    "${CMAKE_BINARY_DIR}/runtime-fixes/fb-window-lifecycle.inc" COPYONLY)
