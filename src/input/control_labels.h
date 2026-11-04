#pragma once
#include "desktop_bindings.h"

namespace bf::input {
// Controls 0..7 are original Xbox analog-button slots. 8..15 are digital
// bits in PadPulses order. The keyboard fallback is keyboard.c's audited map;
// the desktop profile is game-desktop-mkb.c's actual output map (not actions
// guessed from an Xbox letter). Alternate keys remain valid aliases.
inline const char *control_label(unsigned control,bool menu,bool desktop) {
    static constexpr const char *fallback[]={"Z","X","C","V","E","Q","1","3",
        "UP","DOWN","LEFT","RIGHT","ENTER","BACKSPACE","SHIFT","CTRL"};
    if(control>=16)return nullptr;
    if(menu&&control<4)return bf_menu_bindings[control].label;
    return desktop?bf_desktop_bindings[control].label:fallback[control];
}
// Both retail button-font atlas sizes show ABXY, LT/RT, START/BACK, WHITE/
// BLACK in A..J order. Atlas/metrics were read from the authenticated title.
inline int glyph_control(unsigned glyph) {
    switch(glyph) {case 'A':return 0;case 'B':return 1;case 'C':return 2;case 'D':return 3;
    case 'E':return 6;case 'F':return 7;case 'G':return 12;
    case 'H':return 13;case 'I':return 5;case 'J':return 4;
    default:return -1;}
}
}
