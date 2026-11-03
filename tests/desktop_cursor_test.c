#include <windows.h>
#include <stdint.h>
#include <stdio.h>
static HWND foreground=(HWND)(uintptr_t)1;
static RECT clipped;
static int released,hidden,iconic;
static HWND test_foreground(void){return foreground;}
static BOOL test_iconic(HWND window){return iconic;}
static BOOL test_client(HWND window,LPRECT r){r->left=r->top=0;r->right=1280;r->bottom=720;return TRUE;}
static BOOL test_screen(HWND window,LPPOINT p){p->x+=100;p->y+=200;return TRUE;}
static BOOL test_clip(const RECT *r){if(r)clipped=*r;else released++;return TRUE;}
static HCURSOR test_set(HCURSOR cursor){hidden=cursor==NULL;return NULL;}
static HCURSOR test_load(HINSTANCE instance,LPCSTR name){return (HCURSOR)(uintptr_t)2;}
#define GetForegroundWindow test_foreground
#define IsIconic test_iconic
#define GetClientRect test_client
#define ClientToScreen test_screen
#define ClipCursor test_clip
#define SetCursor test_set
#define LoadCursorA test_load
#include "input/desktop_cursor.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void){
    HWND window=(HWND)(uintptr_t)1;BfDesktopCursor state={0};
    CHECK(bf_desktop_cursor_update(&state,window,1)&&hidden);
    CHECK(clipped.left==100&&clipped.top==200&&clipped.right==1380&&clipped.bottom==920);
    CHECK(!bf_desktop_cursor_update(&state,window,0)&&!hidden&&released==1);
    CHECK(bf_desktop_cursor_update(&state,window,1));
    foreground=(HWND)(uintptr_t)3;
    CHECK(!bf_desktop_cursor_update(&state,window,1)&&released==2);
    foreground=window;iconic=1;CHECK(!bf_desktop_cursor_update(&state,window,1));
    iconic=0;CHECK(bf_desktop_cursor_update(&state,window,1));
    CHECK(!bf_desktop_cursor_update(&state,window,0)&&released==3);
    puts("Window-thread hiding, client-area confinement, F1/menu release, focus loss and minimization passed.");return 0;
}
