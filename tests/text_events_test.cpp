#include "text_events.h"
#include <cstdio>
#include <array>
int main() {
    BfTextEvent value{};
    bf_text_reset();
    if(bf_text_character('A') || bf_text_key(13))return 1;
    bf_text_seen(0x123400);
    if(!bf_text_active() || !bf_text_key('Z') || bf_text_key(38))return 2;
    bf_text_character('z');bf_text_character(8);bf_text_character(13);
    if(bf_text_take(0x999900,&value))return 3;
    for(uint32_t expected : std::array<uint32_t,3>{'z',8u,13u})
        if(!bf_text_take(0x123400,&value)||value.click||value.codepoint!=expected)return 4;
    bf_text_character('A');bf_text_seen(0x567800);
    if(bf_text_take(0x567800,&value))return 5;
    bf_text_key(46);bf_text_key(27);
    if(!bf_text_take(0x567800,&value)||value.codepoint!=8)return 6;
    if(!bf_text_take(0x567800,&value)||value.codepoint!=27)return 7;
    bf_text_character(0xd800);bf_text_character(1);
    if(bf_text_take(0x567800,&value))return 8;
    for(int i=0;i<300;++i)bf_text_character('A');
    int count=0;while(bf_text_take(0x567800,&value))++count;
    if(count!=256)return 9;
    bf_text_character('B');bf_text_reset();bf_text_seen(0x567800);
    if(bf_text_take(0x567800,&value))return 10;
    bf_text_utf8("A\xc3\xa9");
    if(!bf_text_take(0x567800,&value)||value.codepoint!='A')return 11;
    if(!bf_text_take(0x567800,&value)||value.codepoint!=0xe9)return 12;
    bf_text_utf8("\xc0\x80");bf_text_utf8("\xed\xa0\x80");
    if(bf_text_take(0x567800,&value))return 13;
    if(bf_text_click(0x111100,0,0)||bf_text_click(0x567800,32,0)||bf_text_click(0x567800,0,16))return 14;
    bf_text_click(0x567800,0,1);bf_text_character('Z');bf_text_click(0x567800,1,1);
    if(!bf_text_take(0x567800,&value)||!value.click||value.column!=0||value.row!=1)return 15;
    if(!bf_text_take(0x567800,&value)||value.click||value.codepoint!='Z')return 16;
    if(!bf_text_take(0x567800,&value)||!value.click||value.column!=1||value.row!=1)return 17;
    bf_text_click(0x567800,2,1);bf_text_seen(0x123400);
    if(bf_text_take(0x123400,&value))return 18;
    std::puts("Native text ownership, ordering, focus reset and queue bounds passed.");
}
