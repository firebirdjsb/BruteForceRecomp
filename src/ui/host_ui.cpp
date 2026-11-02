#include "host_ui.h"
#include "ui_draw.h"
#include "pad_pulses.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
namespace {
using Clock=std::chrono::steady_clock;
std::mutex guard;
bool opened{},keyboard=true,mouse_menus=true,prompts_on=true,fsr{},fsr_supported{},muted{};
int volume=100,selected{};
std::atomic<float> audio_gain{1.0f};
constexpr int rows=8,row_y=132,row_step=28;
unsigned scale_index{},resolution_index{};
bool resize_pending{};
constexpr unsigned scales[]={0,2,4,8},heights[]={0,720,1080,1440,2160};
Clock::time_point menu_seen{};
bf::ui::PadPulses pulses;
std::deque<BfUiPointer> pointers;
uint64_t serial{};
struct Prompt { BfUiRect rect;std::string key;Clock::time_point time; };
std::array<Prompt,32> prompts;
bool menu(){return mouse_menus&&Clock::now()-menu_seen<std::chrono::milliseconds(500);}
void adjust(int row,int direction=1) {
    switch(row) {
    case 0:mouse_menus=!mouse_menus;break;
    case 1:prompts_on=!prompts_on;break;
    case 2:if(fsr_supported)fsr=!fsr;break;
    case 3:if(fsr_supported){scale_index=(scale_index+4+direction)%4;fsr=true;}break;
    case 4:resolution_index=(resolution_index+5+direction)%5;resize_pending=true;break;
    case 5:muted=!muted;break;
    case 6:volume=std::clamp(volume+direction*10,0,100);break;
    case 7:opened=false;break;
    }
    audio_gain.store(muted?0.0f:float(volume)/100.0f,std::memory_order_relaxed);
}
extern "C" int bf_ui_menu_key(unsigned key,int repeat) {
    std::lock_guard lock(guard);
    if(opened||!menu())return 0;
    unsigned control;
    switch(key) {
    case 13:control=0;break;case 27:control=1;break;
    case 'C':control=2;break;case 'V':control=3;break;
    case 38:control=8;break;case 40:control=9;break;
    case 37:control=10;break;case 39:control=11;break;
    default:return 0;
    }
    if(!repeat)pulses.push(control);
    return 1;
}
}
extern "C" int bf_ui_contains(BfUiRect r,float x,float y) {
    return r.w>0&&r.h>0&&x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;
}
extern "C" int bf_ui_point(int x,int y,int w,int h,float *gx,float *gy) {
    if(!gx||!gy||w<=0||h<=0)return 0;
    const float scale=std::min(float(w)/640.0f,float(h)/480.0f);
    *gx=(x-(w-640*scale)*0.5f)/scale;*gy=(y-(h-480*scale)*0.5f)/scale;
    return *gx>=0&&*gx<640&&*gy>=0&&*gy<480;
}
extern "C" void bf_ui_mouse(int x,int y,int w,int h,int action) try {
    float gx{},gy{};if(!bf_ui_point(x,y,w,h,&gx,&gy))return;
    std::lock_guard lock(guard);keyboard=true;
    if(opened) {
        for(int i=0;i<rows;++i)if(bf_ui_contains({112,float(row_y+i*row_step),416,24},gx,gy)) {
            selected=i;if(action==1)adjust(i);else if(action==2)adjust(i,-1);break;
        }
        return;
    }
    if(!menu())return;
    BfUiPointer event{gx,gy,action,++serial};
    if(!action&&!pointers.empty()&&!pointers.back().action)pointers.back()=event;
    else if(pointers.size()<64)pointers.push_back(event);
} catch(const std::exception &e) {
    std::fprintf(stderr,"[UI] Mouse event dropped: %s\n",e.what());
}
extern "C" int bf_ui_key(unsigned key,int repeat) try {
    std::lock_guard lock(guard);keyboard=true;
    if(key==112) {if(!repeat){opened=!opened;pointers.clear();pulses.clear();}return 1;}
    if(!opened)return 0;
    if(!repeat) {
        if(key==27)opened=false;
        else if(key==38)selected=(selected+rows-1)%rows;
        else if(key==40)selected=(selected+1)%rows;
        else if(key==13||key==32||key==39)adjust(selected);
        else if(key==37)adjust(selected,-1);
    }
    return 1;
} catch(const std::exception &e) {
    std::fprintf(stderr,"[UI] Key event released: %s\n",e.what());return 0;
}
extern "C" void bf_ui_focus_lost(){std::lock_guard lock(guard);pointers.clear();pulses.clear();}
extern "C" int bf_ui_menu_active(){std::lock_guard lock(guard);return menu();}
extern "C" int bf_ui_capture(){std::lock_guard lock(guard);return opened||menu();}
extern "C" int bf_ui_open(){std::lock_guard lock(guard);return opened;}
extern "C" int bf_ui_keyboard_mode(){std::lock_guard lock(guard);return keyboard&&prompts_on;}
extern "C" void bf_ui_controller_activity(){std::lock_guard lock(guard);keyboard=false;}
extern "C" void bf_ui_menu_seen(){std::lock_guard lock(guard);menu_seen=Clock::now();}
extern "C" int bf_ui_pointer(BfUiPointer *p) {
    std::lock_guard lock(guard);
    if(!p||opened||!menu()||pointers.empty())return 0;
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    if(pulses.busy(uint64_t(now)))return 0;
    *p=pointers.front();pointers.pop_front();return 1;
}
extern "C" void bf_ui_pad_press(unsigned control) {
    std::lock_guard lock(guard);if(opened||control>15)return;
    pulses.push(control);
}
extern "C" int bf_ui_pad(uint16_t *digital,uint8_t analog[8]) {
    std::lock_guard lock(guard);
    if(opened)return 1; // The caller starts with a zeroed gamepad.
    unsigned control{};
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    if(!pulses.poll(uint64_t(now),control))return 0;
    if(control<8)analog[control]=255;
    else *digital|=uint16_t(1u<<(control-8));
    return 1;
}
extern "C" void bf_ui_prompt(BfUiRect rect,const char *key) {
    if(!key)return;
    std::lock_guard lock(guard);const auto now=Clock::now();
    for(auto &p:prompts)if((p.rect.x==rect.x&&p.rect.y==rect.y)||now-p.time>std::chrono::milliseconds(300)) {
        p={rect,key,now};break;
    }
}
extern "C" int bf_ui_fsr(){std::lock_guard lock(guard);return fsr;}
extern "C" void bf_ui_fsr_initial(int value){std::lock_guard lock(guard);fsr=value!=0;}
extern "C" void bf_ui_fsr_supported(int value){std::lock_guard lock(guard);fsr_supported=value!=0;}
extern "C" unsigned bf_ui_scale(){std::lock_guard lock(guard);return scales[scale_index];}
extern "C" int bf_ui_take_size(unsigned *width,unsigned *height) {
    std::lock_guard lock(guard);if(!resize_pending||!width||!height)return 0;
    resize_pending=false;*height=heights[resolution_index];*width=*height?*height*16/9:1280;
    if(!*height)*height=960;return 1;
}
extern "C" float bf_ui_audio_gain(){return audio_gain.load(std::memory_order_relaxed);}
extern "C" void bf_ui_paint(uint32_t *pixels,unsigned width,unsigned height) try {
    if(!pixels||width!=640||height!=480)return;
    std::lock_guard lock(guard);bf::ui::Canvas c{pixels,width,height};
    const auto now=Clock::now();
    if(keyboard&&prompts_on)for(auto &p:prompts)if(now-p.time<std::chrono::milliseconds(250)) {
        const int x=int(p.rect.x),y=int(p.rect.y);
        c.box(x,y+2,17,15,0xff59bfd9);c.box(x+1,y+3,15,13,0xff112d41);
        c.text(x+2,y+7,p.key,0xffe1f5ff);
    }
    if(!opened)return;
    c.box(92,60,456,374,0xff020e19);c.box(94,62,452,370,0xff102a3d);
    c.box(94,62,452,4,0xff55c5e8);c.text(112,82,"BRUTE FORCE",0xffe1f5ff,3);
    c.text(112,108,"RECOMP / LIVE SETTINGS",0xff69cae9,2);
    const char *names[]={"MOUSE MENU SELECTION","KEYBOARD / MOUSE PROMPTS","AMD FSR 1 (DX12)","FSR OUTPUT SCALE","WINDOW RESOLUTION","MUTE AUDIO","MASTER VOLUME","RETURN TO GAME"};
    for(int i=0;i<rows;++i) {
        const int y=row_y+i*row_step;
        c.box(112,y,416,24,i==selected?0xff245978:0xff17394e);
        c.text(122,y+7,names[i],0xffd9efff,2);
        std::string value;
        if(i<3)value=(i==0?mouse_menus:i==1?prompts_on:fsr)?"ON":"OFF";
        if(i==3)value=scale_index?std::to_string(scales[scale_index])+"X":"AUTO";
        if(i==4)value=resolution_index?std::to_string(heights[resolution_index])+"P":"AUTO";
        if(i==5)value=muted?"ON":"OFF";
        if(i==6)value=std::to_string(volume)+"%";
        if(i==2&&!fsr_supported)value="N/A";
        if(i==3&&!fsr_supported)value="N/A";
        c.text(458,y+7,value,0xff79dbb7,2);
    }
    c.text(112,373,"MOUSE OR ARROWS / ENTER TO CHANGE",0xff9bc0d5);
    c.text(112,388,"F1 / ESC TO CLOSE",0xff9bc0d5);
    c.text(112,407,"ORIGINAL 4:3 VIEW / NATIVE 16:9 PENDING",0xffe6b47e);
    c.text(112,419,"CHEATS UNAVAILABLE",0xffe6b47e);
} catch(const std::exception &e) {
    std::fprintf(stderr,"[UI] Overlay frame skipped: %s\n",e.what());
}
