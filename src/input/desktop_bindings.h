#pragma once
/* Primary VKs and labels for the actual desktop profile. Mouse buttons use
 * their Win32 VK values here; the raw reader still handles held buttons and
 * the wheel through its existing packet path. Zero means no primary key. */
typedef struct BfDesktopBinding { unsigned key;const char *label; } BfDesktopBinding;
static const BfDesktopBinding bf_desktop_bindings[16]={
    {0x20,"SPACE"},{'E',"E"},{'R',"R"},{0x04,"MMB"},
    {'F',"F"},{'Q',"Q"},{'G',"G"},{0x01,"LMB"},
    {0x26,"UP"},{0x28,"DOWN"},{0x25,"LEFT"},{0x27,"RIGHT"},
    {0x1B,"ESC"},{0x09,"TAB"},{0xA0,"SHIFT"},{0x02,"RMB"}
};
static const BfDesktopBinding bf_menu_bindings[4]={{13,"ENTER"},{27,"ESC"},{'C',"C"},{'V',"V"}};
