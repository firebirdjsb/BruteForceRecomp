#include "host_ui.h"
#include "ui_draw.h"
#include "pad_pulses.h"
#include "graphics_options.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
namespace {
using Clock=std::chrono::steady_clock;
std::mutex guard;
bool opened{},keyboard=true,mouse_menus=true,prompts_on=true,fsr{},fsr_supported{},muted{};
int volume=100,selected{};
int page{};
bool gpu_available{};
std::string active_scene="CPU REFERENCE";
bf::GraphicsOptions preferences;
std::filesystem::path settings_path;
std::string settings_error;
std::atomic<float> audio_gain{1.0f};
std::atomic<float> mouse_sensitivity{1.0f};
std::atomic<unsigned> interpolation_rate{0};
std::atomic<unsigned> texture_scale{1};
constexpr int rows=8,row_y=132,row_step=28;
unsigned scale_index{},resolution_index{};
bool resize_pending{};
bool sensitivity_dragging{};
constexpr unsigned scales[]={0,2,4,8},heights[]={0,720,1080,1440,2160};
Clock::time_point menu_seen{};
bf::ui::PadPulses pulses;
std::deque<BfUiPointer> pointers;
uint64_t serial{};
struct Prompt { BfUiRect rect;std::string key;Clock::time_point time; };
std::array<Prompt,32> prompts;
struct Tutorial { BfUiRect root{};Clock::time_point seen{};bool verified{}; } tutorial;
// Stable object tags/classes from the authenticated Basic Controls hierarchy.
// Keep the title and original accept/skip prompt objects outside this set.
constexpr uint32_t tutorial_text[]={0xE4BA1ECE,0xF1E4D812,0x00AAB5C6,0x1BA3E47C,0xF6B4200F,
    0xE2E50FBE,0x03445ADC,0x09D15D91,0xE84B1D21,0xE4ABC6F1};
constexpr uint32_t tutorial_images[]={0xEE52EBAF,0x07B09CCB,0xE6C60D0A,0x07900EF7,0xE525F3DB};
bool tutorial_node(uint32_t tag,uint32_t vtable) {
    if(vtable==0x0039E0C0)return std::find(std::begin(tutorial_text),std::end(tutorial_text),tag)!=std::end(tutorial_text);
    if(vtable==0x0039E290)return std::find(std::begin(tutorial_images),std::end(tutorial_images),tag)!=std::end(tutorial_images);
    return false;
}
bool tutorial_active(){return tutorial.verified&&keyboard&&prompts_on&&Clock::now()-tutorial.seen<std::chrono::milliseconds(250);}
void paint_tutorial(bf::ui::Canvas &c) {
    const int x=int(std::lround(tutorial.root.x))+15,y=int(std::lround(tutorial.root.y))+35;
    const uint32_t panel=0xff0b2c48,edge=0xff32bfe8,ink=0xffe1f5ff,muted_ink=0xffa9cada;
    c.box(x,y,440,290,0xff08223b);
    c.text5x7(x+9,y+8,"KEYBOARD / MOUSE",ink,2);
    c.box(x+8,y+29,202,180,panel);c.box(x+220,y+29,212,180,panel);
    c.text5x7(x+18,y+40,"MOVE",edge,2);c.text5x7(x+230,y+40,"LOOK / AIM",edge,2);
    const auto key=[&](int kx,int ky,const char *label){
        c.box(kx,ky,34,30,edge);c.box(kx+2,ky+2,30,26,0xff173e59);c.text5x7(kx+12,ky+8,label,ink,2);
    };
    key(x+86,y+65,"W");key(x+48,y+99,"A");key(x+86,y+99,"S");key(x+124,y+99,"D");
    c.text5x7(x+18,y+145,"W/S: FORWARD / BACK",ink);
    c.text5x7(x+18,y+160,"A/D: STRAFE",ink);
    c.text5x7(x+18,y+185,"RELATIVE TO YOUR CAMERA",muted_ink);
    // Authored mouse silhouette and distinct left/right buttons, drawn with
    // the same code-native primitives as the F1 controls page.
    c.box(x+280,y+64,92,78,edge);c.box(x+282,y+66,88,74,0xff173e59);
    c.box(x+284,y+68,40,27,0xff245978);c.box(x+328,y+68,40,27,0xff245978);
    c.box(x+325,y+67,2,30,edge);c.box(x+322,y+73,8,13,ink);
    c.text5x7(x+299,y+76,"L",ink,2);c.text5x7(x+343,y+76,"R",ink,2);
    c.text5x7(x+297,y+107,"MOUSE",ink,2);
    c.text5x7(x+230,y+150,"MOUSE: LOOK AROUND",ink);
    c.text5x7(x+230,y+166,"LMB: FIRE",ink);
    c.text5x7(x+230,y+182,"RMB: TOGGLE ZOOM",ink);
    c.text5x7(x+18,y+223,"G: GRENADE / ITEM",ink,2);
    c.text5x7(x+18,y+244,"F1: SETTINGS / MOUSE SENSITIVITY",ink);
    c.text5x7(x+18,y+264,"TIP: ENEMIES CAN HEAR YOUR MOVEMENT.",muted_ink);
}
bool menu(){return mouse_menus&&Clock::now()-menu_seen<std::chrono::milliseconds(500);}
int row_count(){return page==2?1:page==1?5:rows;}
void persist() {
    preferences.fsr1=fsr;preferences.scale=scales[scale_index];preferences.height=heights[resolution_index];
    preferences.mouse_menus=mouse_menus;preferences.prompts=prompts_on;preferences.muted=muted;preferences.volume=volume;
    if(settings_path.empty())return;
    try {bf::save_graphics(settings_path,preferences);settings_error.clear();}
    catch(const std::exception &e){settings_error="SETTINGS SAVE FAILED";std::fprintf(stderr,"[UI] %s\n",e.what());}
}
void adjust(int row,int direction=1) {
    if(page==2) {
        preferences.mouse_sensitivity=std::clamp(preferences.mouse_sensitivity+direction*0.1f,0.1f,10.0f);
        mouse_sensitivity.store(preferences.mouse_sensitivity,std::memory_order_relaxed);
        persist();return;
    }
    if(page) {
        if(row==0&&gpu_available) {
            const char *scenes[]={"cpu","d3d11","d3d12"};int index=0;
            for(int i=0;i<3;++i)if(preferences.scene==scenes[i])index=i;
            preferences.scene=scenes[(index+3+direction)%3];
        }
        if(row==1&&gpu_available)preferences.gpu_programs=!preferences.gpu_programs;
        if(row==3&&fsr_supported) {
            preferences.interpolation=preferences.interpolation==0?60:preferences.interpolation==60?120:0;
            interpolation_rate.store(preferences.interpolation,std::memory_order_relaxed);
        }
        if(row==4&&gpu_available) {
            preferences.texture_scale=preferences.texture_scale==1?2:preferences.texture_scale==2?4:1;
            texture_scale.store(preferences.texture_scale,std::memory_order_relaxed);
        }
#ifdef _WIN32
        if(row==2) {
            const char *backends[]={"auto","dx12","dx11","gdi"};int index=0;
            for(int i=0;i<4;++i)if(preferences.backend==backends[i])index=i;
            preferences.backend=backends[(index+4+direction)%4];
        }
#endif
        persist();return;
    }
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
    if(row!=7)persist();
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
        if(action==3){sensitivity_dragging=false;return;}
        if(action==0)sensitivity_dragging=false;
        if(page==2 && ((action==1 && bf_ui_contains({128,160,385,22},gx,gy)) || (action==4 && sensitivity_dragging))) {
            sensitivity_dragging=true;
            const float position=std::clamp((gx-128.0f)/384.0f,0.0f,1.0f);
            preferences.mouse_sensitivity=std::clamp(std::round(std::pow(10.0f,position*2.0f-1.0f)*100.0f)/100.0f,0.1f,10.0f);
            mouse_sensitivity.store(preferences.mouse_sensitivity,std::memory_order_relaxed);
            persist();return;
        }
        if(action==1)sensitivity_dragging=false;
        for(int tab=0;tab<3;++tab)if(bf_ui_contains({float(112+tab*140),105,136,22},gx,gy)) {
            if(action==1){page=tab;selected=0;}return;
        }
        for(int i=0;i<row_count();++i)if(bf_ui_contains({112,float(row_y+i*row_step),416,24},gx,gy)) {
            selected=i;if(action==1)adjust(i);else if(action==2)adjust(i,-1);break;
        }
        return;
    }
    if(action==3)return;
    if(action==4)action=0;
    if(!menu())return;
    BfUiPointer event{gx,gy,action,++serial};
    if(!action&&!pointers.empty()&&!pointers.back().action)pointers.back()=event;
    else if(pointers.size()<64)pointers.push_back(event);
} catch(const std::exception &e) {
    std::fprintf(stderr,"[UI] Mouse event dropped: %s\n",e.what());
}
extern "C" int bf_ui_key(unsigned key,int repeat) try {
    std::lock_guard lock(guard);keyboard=true;
    if(key==112) {if(!repeat){opened=!opened;sensitivity_dragging=false;pointers.clear();pulses.clear();}return 1;}
    if(!opened)return 0;
    if(!repeat) {
        if(key==27)opened=false;
        else if(key==9||key==33||key==34){page=(page+(key==33?2:1))%3;selected=0;}
        else if(key==38)selected=(selected+row_count()-1)%row_count();
        else if(key==40)selected=(selected+1)%row_count();
        else if(key==13||key==32||key==39)adjust(selected);
        else if(key==37)adjust(selected,-1);
    }
    return 1;
} catch(const std::exception &e) {
    std::fprintf(stderr,"[UI] Key event released: %s\n",e.what());return 0;
}
extern "C" void bf_ui_focus_lost(){std::lock_guard lock(guard);sensitivity_dragging=false;pointers.clear();pulses.clear();}
extern "C" int bf_ui_menu_active(){std::lock_guard lock(guard);return menu();}
extern "C" int bf_ui_capture(){std::lock_guard lock(guard);return opened||menu();}
extern "C" int bf_ui_open(){std::lock_guard lock(guard);return opened;}
extern "C" int bf_ui_keyboard_mode(){std::lock_guard lock(guard);return keyboard&&prompts_on;}
extern "C" int bf_ui_keyboard_active(){std::lock_guard lock(guard);return keyboard;}
extern "C" void bf_ui_controller_activity(){std::lock_guard lock(guard);keyboard=false;}
extern "C" void bf_ui_mouse_activity(){std::lock_guard lock(guard);keyboard=true;}
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
extern "C" int bf_ui_tutorial_prepare(uint32_t root_tag,BfUiRect root,const BfUiTutorialNode *nodes,unsigned count) {
    std::lock_guard lock(guard);tutorial.verified=false;
    if(root_tag!=0xF0EA44DC||!nodes||count>128||root.w!=470||root.h!=360||
       !std::isfinite(root.x)||!std::isfinite(root.y)||root.x<0||root.y<0||root.x+root.w>640||root.y+root.h>480)return 0;
    bool title=false,art=false;unsigned texts=0,images=0;
    for(unsigned i=0;i<count;i++) {
        const auto &node=nodes[i];if(!(node.flags&4))continue;
        if(node.vtable==0x0039E0C0&&node.tag==0x0BB9C4AF)title=true;
        if(node.vtable==0x0039E290&&node.tag==0xEE52EBAF&&node.rect.w==256&&node.rect.h==256&&
           std::abs(node.rect.x-root.x-202)<0.01f&&std::abs(node.rect.y-root.y-13)<0.01f)art=true;
    }
    // Require each observed instruction/icon tag, rather than applying a
    // generic-screen rule to other tutorial pages or similarly named images.
    for(auto tag:tutorial_text)for(unsigned i=0;i<count;i++)if(nodes[i].tag==tag&&nodes[i].vtable==0x0039E0C0&&(nodes[i].flags&4)){texts++;break;}
    for(auto tag:tutorial_images)for(unsigned i=0;i<count;i++)if(nodes[i].tag==tag&&nodes[i].vtable==0x0039E290&&(nodes[i].flags&4)){images++;break;}
    if(!title||!art||texts!=std::size(tutorial_text)||images!=std::size(tutorial_images))return 0;
    tutorial={root,Clock::now(),true};return keyboard&&prompts_on;
}
extern "C" int bf_ui_tutorial_hide(uint32_t tag,uint32_t vtable) {
    std::lock_guard lock(guard);return tutorial_active()&&tutorial_node(tag,vtable);
}
extern "C" void bf_ui_tutorial_clear(){std::lock_guard lock(guard);tutorial.verified=false;}
extern "C" int bf_ui_fsr(){std::lock_guard lock(guard);return fsr;}
extern "C" void bf_ui_fsr_initial(int value){std::lock_guard lock(guard);fsr=value!=0;}
extern "C" void bf_ui_fsr_supported(int value){std::lock_guard lock(guard);fsr_supported=value!=0;}
extern "C" void bf_ui_settings_initial(int available) {
    std::lock_guard lock(guard);gpu_available=available!=0;
    if(const auto *path=std::getenv("BF_UI_SETTINGS");path&&*path)settings_path=std::filesystem::u8path(path);
    preferences=settings_path.empty()?bf::GraphicsOptions{}:bf::load_graphics(settings_path);
    active_scene="CPU REFERENCE";
    if(const auto *scene=std::getenv("RECOMP_GPU")) {
        auto candidate=preferences;candidate.scene=scene;if(bf::valid_graphics(candidate))preferences.scene=scene;
        active_scene=preferences.scene=="d3d12"?"DX12 GPU [11ON12]":preferences.scene=="d3d11"?"D3D11 GPU":"CPU REFERENCE";
    }
    if(const auto *programs=std::getenv("RECOMP_GPU_VSH"))preferences.gpu_programs=std::string(programs)!="0";
    interpolation_rate.store(preferences.interpolation,std::memory_order_relaxed);
    texture_scale.store(preferences.texture_scale,std::memory_order_relaxed);
    if(const auto *backend=std::getenv("BF_RENDER_BACKEND")) {
        auto candidate=preferences;candidate.backend=backend;if(bf::valid_graphics(candidate))preferences.backend=backend;
    }
    fsr=preferences.fsr1;mouse_menus=preferences.mouse_menus;prompts_on=preferences.prompts;
    muted=preferences.muted;volume=preferences.volume;
    mouse_sensitivity.store(preferences.mouse_sensitivity,std::memory_order_relaxed);
    audio_gain.store(muted?0.0f:float(volume)/100.0f,std::memory_order_relaxed);
    for(unsigned i=0;i<4;++i)if(preferences.scale==scales[i])scale_index=i;
    for(unsigned i=0;i<5;++i)if(preferences.height==heights[i])resolution_index=i;
    resize_pending=resolution_index!=0;
}
extern "C" unsigned bf_ui_scale(){std::lock_guard lock(guard);return scales[scale_index];}
extern "C" int bf_ui_take_size(unsigned *width,unsigned *height) {
    std::lock_guard lock(guard);if(!resize_pending||!width||!height)return 0;
    resize_pending=false;*height=heights[resolution_index];*width=*height?*height*16/9:1280;
    if(!*height)*height=960;return 1;
}
extern "C" float bf_ui_audio_gain(){return audio_gain.load(std::memory_order_relaxed);}
extern "C" float bf_ui_mouse_sensitivity(){return mouse_sensitivity.load(std::memory_order_relaxed);}
extern "C" unsigned bf_ui_interpolation(){return interpolation_rate.load(std::memory_order_relaxed);}
extern "C" unsigned bf_ui_texture_scale(){return texture_scale.load(std::memory_order_relaxed);}
extern "C" int bf_ui_interpolation_allowed(){std::lock_guard lock(guard);return !opened&&!tutorial_active()&&Clock::now()-menu_seen>std::chrono::milliseconds(500);}
extern "C" void bf_ui_paint(uint32_t *pixels,unsigned width,unsigned height) try {
    if(!pixels||width!=640||height!=480)return;
    std::lock_guard lock(guard);bf::ui::Canvas c{pixels,width,height};
    const auto now=Clock::now();
    if(tutorial_active())paint_tutorial(c);
    if(keyboard&&prompts_on)for(auto &p:prompts)if(now-p.time<std::chrono::milliseconds(250)) {
        const int x=int(p.rect.x),y=int(p.rect.y);
        c.box(x,y+2,17,15,0xff59bfd9);c.box(x+1,y+3,15,13,0xff112d41);
        c.text(x+2,y+7,p.key,0xffe1f5ff);
    }
    if(!opened)return;
    c.box(92,60,456,374,0xff020e19);c.box(94,62,452,370,0xff102a3d);
    c.box(94,62,452,4,0xff55c5e8);c.text(112,82,"BRUTE FORCE",0xffe1f5ff,3);
    const char *tabs[]={"LIVE SETTINGS","RENDERER","CONTROLS"};
    for(int tab=0;tab<3;++tab){c.box(112+tab*140,105,136,22,page==tab?0xff245978:0xff17394e);c.text(120+tab*140,111,tabs[tab],0xff69cae9,2);}
    const char *names[]={"MOUSE MENU SELECTION","KEYBOARD / MOUSE PROMPTS","AMD FSR 1 (DX12)","FSR OUTPUT SCALE","WINDOW RESOLUTION","MUTE AUDIO","MASTER VOLUME","RETURN TO GAME"};
    const char *renderer_names[]={"SCENE RENDERER","GPU VERTEX PROGRAMS","PRESENTATION","VISUAL INTERPOLATION","TEXTURE RECONSTRUCTION"};
    for(int i=0;i<row_count();++i) {
        const int y=row_y+i*row_step;
        c.box(112,y,416,24,i==selected?0xff245978:0xff17394e);
        c.text(122,y+7,page==2?"MOUSE SENSITIVITY":page==1?renderer_names[i]:names[i],0xffd9efff,2);
        std::string value;
        if(page==2) {
            char sensitivity_text[16];std::snprintf(sensitivity_text,sizeof sensitivity_text,"%.2fX",preferences.mouse_sensitivity);
            c.text(454,y+7,sensitivity_text,0xff79dbb7,2);continue;
        }
        if(page) {
            if(i==0)value=preferences.scene=="d3d12"?"DX12 GPU":preferences.scene=="d3d11"?"DX11 GPU":"CPU";
            if(i==1)value=gpu_available?(preferences.gpu_programs?"ON":"OFF"):"N/A";
            if(i==2)value=preferences.backend;
            if(i==3)value=fsr_supported?(preferences.interpolation?std::to_string(preferences.interpolation):"OFF"):"N/A";
            if(i==4)value=gpu_available?(preferences.texture_scale==1?"OFF":std::to_string(preferences.texture_scale)+"X"):"N/A";
#ifndef _WIN32
            if(i==2)value="SDL";
#endif
            c.text(446,y+7,value,0xff79dbb7,2);continue;
        }
        if(i<3)value=(i==0?mouse_menus:i==1?prompts_on:fsr)?"ON":"OFF";
        if(i==3)value=scale_index?std::to_string(scales[scale_index])+"X":"AUTO";
        if(i==4)value=resolution_index?std::to_string(heights[resolution_index])+"P":"AUTO";
        if(i==5)value=muted?"ON":"OFF";
        if(i==6)value=std::to_string(volume)+"%";
        if(i==2&&!fsr_supported)value="N/A";
        if(i==3&&!fsr_supported)value="N/A";
        c.text(458,y+7,value,0xff79dbb7,2);
    }
    if(page==2) {
        const int slider_x=128+int((std::log10(preferences.mouse_sensitivity)+1.0f)*0.5f*384.0f);
        c.box(128,167,384,5,0xff17394e);c.box(128,167,slider_x-128,5,0xff55c5e8);
        c.box(slider_x-3,162,7,15,0xffe1f5ff);
        c.text(128,182,"DRAG TO SET / LEFT-RIGHT TO ADJUST",0xff9bc0d5);
        c.text(122,201,"KEYBOARD / MOUSE GUIDE",0xffe1f5ff,2);
        const auto key=[&](int x,int y,const char *label){c.box(x,y,30,28,0xff245978);c.text(x+11,y+9,label,0xffe1f5ff,2);};
        key(164,225,"W");key(130,257,"A");key(164,257,"S");key(198,257,"D");
        c.text(126,301,"W/S: FORWARD / BACK",0xffd9efff);
        c.text(126,315,"A/D: STRAFE",0xffd9efff);
        c.text(126,329,"G: GRENADE / ITEM",0xffd9efff);
        c.box(336,228,110,75,0xff17394e);c.box(336,228,53,29,0xff245978);c.box(393,228,53,29,0xff245978);
        c.text(346,239,"LMB",0xffe1f5ff,2);c.text(403,239,"RMB",0xffe1f5ff,2);
        c.text(351,274,"MOUSE",0xff69cae9,2);
        c.text(322,315,"MOUSE: CAMERA",0xffd9efff);
        c.text(322,329,"HOLD LMB: FIRE",0xff79dbb7);
        c.text(322,341,"RMB: TOGGLE ZOOM",0xffd9efff);
        c.text(122,357,"F1 SETTINGS RELEASES THE MOUSE",0xff9bc0d5);
    }
    if(page==1) {
        c.text(122,279,"DEVICE CHANGES: NEXT LAUNCH",0xffe6b47e,2);
        c.text(122,301,"ACTIVE: "+active_scene,0xffd9efff,2);
        c.text(122,323,"GAME TIMING: ORIGINAL 30 FPS",0xff79dbb7);
        c.text(122,335,"INTERPOLATION ADDS ONE FRAME LATENCY",0xff9bc0d5);
        c.text(122,347,"TEXTURES: EXPERIMENTAL / ORIGINAL MIPS",0xff9bc0d5);
        c.text(122,359,settings_path.empty()?"DIRECT RUN: SESSION SETTINGS ONLY":"SETTINGS SHARED WITH LAUNCHER",0xff9bc0d5);
    }
    c.text(112,373,"MOUSE OR ARROWS / ENTER TO CHANGE",0xff9bc0d5);
    c.text(112,388,"TAB: PAGE   F1 / ESC: CLOSE",0xff9bc0d5);
    c.text(112,407,"ORIGINAL 4:3 VIEW / NATIVE 16:9 PENDING",0xffe6b47e);
    c.text(112,419,settings_error.empty()?"CHEATS UNAVAILABLE":settings_error,0xffe6b47e);
} catch(const std::exception &e) {
    std::fprintf(stderr,"[UI] Overlay frame skipped: %s\n",e.what());
}
