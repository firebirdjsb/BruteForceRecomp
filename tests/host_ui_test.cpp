#include "ui/host_ui.h"
#include "ui/pad_pulses.h"
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
int main(int argc,char **argv) {
    bf::ui::PadPulses pulses;unsigned control{};
    pulses.push(0);pulses.push(0);pulses.push(1);
    if(!pulses.busy(0))return 27;
    if(!pulses.poll(0,control)||control!=0||pulses.poll(100,control))return 23;
    if(pulses.poll(139,control)||!pulses.poll(140,control)||control!=0)return 24;
    if(pulses.poll(240,control)||!pulses.poll(280,control)||control!=1)return 25;
    pulses.clear();if(pulses.poll(300,control))return 26;
    float x{},y{};
    if(!bf_ui_point(640,360,1280,720,&x,&y)||std::abs(x-320)>0.01||std::abs(y-240)>0.01)return 1;
    if(bf_ui_point(0,360,1280,720,&x,&y)||bf_ui_point(1,1,0,0,&x,&y))return 2;
    if(!bf_ui_contains({10,10,20,20},10,10)||bf_ui_contains({10,10,20,20},30,20))return 3;
    BfUiPointer p{};bf_ui_menu_seen();bf_ui_mouse(200,300,640,480,0);bf_ui_mouse(210,310,640,480,0);
    bf_ui_mouse(210,310,640,480,1);
    if(!bf_ui_pointer(&p)||p.x!=210||p.action!=0)return 4;
    if(!bf_ui_pointer(&p)||p.action!=1||bf_ui_pointer(&p))return 5;
    bf_ui_pad_press(0);uint16_t buttons{};std::array<uint8_t,8> analog{};
    if(!bf_ui_pad(&buttons,analog.data())||analog[0]!=255)return 6;
    bf_ui_focus_lost();analog={};if(bf_ui_pad(&buttons,analog.data()))return 7;
    bf_ui_controller_activity();if(bf_ui_keyboard_mode())return 8;
    if(!bf_ui_key(112,0)||!bf_ui_open()||!bf_ui_keyboard_mode())return 9;
    bf_ui_key(112,1);if(!bf_ui_open())return 10;
    if(bf_ui_pointer(&p))return 11;
    // Actual overlay mouse interaction toggles mute and master gain.
    bf_ui_mouse(200,132+5*28+8,640,480,1);
    if(bf_ui_audio_gain()!=0)return 12;
    bf_ui_mouse(200,132+5*28+8,640,480,1);
    if(bf_ui_audio_gain()!=1)return 13;
    bf_ui_fsr_supported(1);
    bf_ui_mouse(200,132+3*28+8,640,480,1);
    if(bf_ui_scale()!=2||!bf_ui_fsr())return 18;
    bf_ui_mouse(200,132+3*28+8,640,480,1);
    if(bf_ui_scale()!=4)return 19;
    bf_ui_mouse(200,132+3*28+8,640,480,1);
    if(bf_ui_scale()!=8)return 20;
    unsigned w{},h{};
    for(unsigned height:{720u,1080u,1440u,2160u}) {
        bf_ui_mouse(200,132+4*28+8,640,480,1);
        if(!bf_ui_take_size(&w,&h)||h!=height||w!=height*16/9)return 21;
    }
    std::vector<uint32_t> image(640*480,0xff000000);bf_ui_paint(image.data(),640,480);
    if(image[62*640+94]!=0xff55c5e8)return 14;
    if(argc==2) {
        std::array<unsigned char,54> header{};header[0]='B';header[1]='M';
        auto field=[&](unsigned offset,uint32_t value){std::memcpy(header.data()+offset,&value,4);};
        field(2,54+640*480*4);field(10,54);field(14,40);field(18,640);field(22,480);
        header[26]=1;header[28]=32;
        FILE *file=std::fopen(argv[1],"wb");if(!file)return 22;
        std::fwrite(header.data(),1,header.size(),file);
        for(int row=479;row>=0;--row)std::fwrite(image.data()+row*640,4,640,file);
        std::fclose(file);
    }
    bf_ui_key(27,0);if(bf_ui_open())return 15;
    if(!bf_ui_menu_key(13,0))return 16;
    buttons=0;analog={};if(!bf_ui_pad(&buttons,analog.data())||analog[0]!=255)return 17;
    std::puts("Menu coordinates, input ownership, click ordering, F1 focus and live audio settings passed.");
}
