#ifndef BF_DESKTOP_CURSOR_H
#define BF_DESKTOP_CURSOR_H
#include <windows.h>
/* Called exclusively on the framebuffer window's thread. SetCursor is owned
 * by that thread; no ShowCursor counter is changed from the USB poll thread.
 * Raw mouse deltas remain relative when the Windows pointer reaches an edge. */
typedef struct { int confined; } BfDesktopCursor;
static int bf_desktop_cursor_update(BfDesktopCursor *state, HWND window, int requested)
{
    RECT client;
    POINT origin, end;
    if(requested && GetForegroundWindow()==window && !IsIconic(window) &&
       GetClientRect(window,&client) && client.right>client.left && client.bottom>client.top) {
        origin.x=client.left;origin.y=client.top;end.x=client.right;end.y=client.bottom;
        if(ClientToScreen(window,&origin)&&ClientToScreen(window,&end)) {
            client.left=origin.x;client.top=origin.y;client.right=end.x;client.bottom=end.y;
            if(ClipCursor(&client)){
                state->confined=1;SetCursor(NULL);return 1;
            }
        }
    }
    if(state->confined) {
        ClipCursor(NULL);state->confined=0;
        if(GetForegroundWindow()==window)SetCursor(LoadCursorA(NULL,IDC_ARROW));
    }
    return 0;
}
#endif
