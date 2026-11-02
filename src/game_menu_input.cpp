#include "ui/host_ui.h"
#include "text_events.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
int bf_guest_menu_focus(uint32_t,uint32_t);
int bf_guest_list_focus(uint32_t,uint32_t);
int bf_guest_keyboard_focus(uint32_t,uint32_t,uint32_t);
}
namespace {
bool read(uint32_t va,void *out,size_t size) {
    if(va<0x10000||uint64_t(va)+size>UINT32_MAX)return false;
    auto *p=reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),p,out,size,&got)&&got==size;
#else
    if(uint64_t(va)+size>g_xbox_total_ram)return false;
    std::memcpy(out,p,size);return true;
#endif
}
struct Node {
    std::array<uint32_t,0xB4/4> data{};
    uint32_t at(unsigned offset)const{return data[offset/4];}
    unsigned flags()const{return at(0x28)&255;}
    float number(unsigned offset)const{float value{};auto bits=at(offset);std::memcpy(&value,&bits,4);return value;}
};
bool node(uint32_t va,Node &n){return read(va,n.data.data(),sizeof(n.data));}
bool managed_view(uint32_t view) {
    uint32_t game{},app{},manager{},head{},next{};
    // Authenticated retail global 0x00469B58 -> game +8 -> application.
    // Its original virtual getter 0x00078D20 returns application+0x5C4.
    // Manager+8 is the 12-byte-node std::list of registered screen views.
    // Embedded button panels share the view vtable but are absent here.
    if(!read(0x00469B58,&game,4)||!read(game+8,&app,4)||
       !read(app+0x5C4,&manager,4)||!read(manager+8,&head,4)||!read(head,&next,4))return false;
    for(unsigned count=0;next&&next!=head&&count<64;++count) {
        uint32_t links[3]{};if(!read(next,links,sizeof links))return false;
        if(links[2]==view)return true;
        next=links[0];
    }
    return false;
}
bool vector(uint32_t object,unsigned field,std::vector<uint32_t> &out) {
    uint32_t bounds[2]{};
    if(!read(object+field,bounds,8)||!bounds[0]||bounds[1]<bounds[0]||
       (bounds[1]-bounds[0])%4||bounds[1]-bounds[0]>2048)return false;
    out.resize((bounds[1]-bounds[0])/4);return read(bounds[0],out.data(),out.size()*4);
}
bool position(uint32_t object,BfUiRect &r) {
    Node n;if(!node(object,n))return false;
    r={0,0,n.number(0x14),n.number(0x18)};
    // Original GetPosition at 0x000EF0F0 follows coordinate owner +8, not
    // navigation parent +4; local XY is +0xC/+0x10 and animation offset is
    // +0x1C/+0x20 unless flag 0x20 suppresses that offset.
    for(int depth=0;object&&depth<16;++depth) {
        if(!node(object,n))return false;
        r.x+=n.number(0xC);r.y+=n.number(0x10);
        if(!(n.flags()&0x20)){r.x+=n.number(0x1C);r.y+=n.number(0x20);}
        object=n.at(8);
    }
    return !object&&std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.w)&&std::isfinite(r.h);
}
bool key_position(uint32_t key,BfUiRect &r) {
    // Original key creation 0x0010DFC0 leaves the panel at key+4 with zero
    // width/height. Its child button tagged 0x0B75A0FB receives the actual
    // rendered dimensions at 0x0010E087..0x0010E0A5, including wide keys.
    std::vector<uint32_t> children;
    if(!vector(key+4,0x74,children))return false;
    for(auto child:children) {
        Node n;
        if(node(child,n)&&n.at(0x24)==0x0B75A0FB&&(n.flags()&6)==6)
            return position(child,r);
    }
    return false;
}
void fixture() {
    static const char *path=std::getenv("BF_UI_INPUT_LIVE");static long consumed{};
    if(!path)return;
    FILE *f=std::fopen(path,"rb");if(!f)return;
    if(std::fseek(f,consumed,SEEK_SET)){std::fclose(f);return;}
    char line[128];
    while(std::fgets(line,sizeof line,f)) {
        if(!std::strchr(line,'\n'))break;
        consumed=std::ftell(f);int x{},y{},key{};
        if(std::getenv("BF_TRACE_UI"))std::fprintf(stderr,"[UI] fixture %s",line);
        if(std::sscanf(line,"move %d %d",&x,&y)==2)bf_ui_mouse(x,y,640,480,0);
        else if(std::sscanf(line,"click %d %d",&x,&y)==2)bf_ui_mouse(x,y,640,480,1);
        else if(std::sscanf(line,"key %d",&key)==1) {
            if(!bf_ui_key(unsigned(key),0)&&!bf_text_active())bf_ui_menu_key(unsigned(key),0);
        }
    }
    std::fclose(f);
}
void tick(uint32_t view) {
    Node root;
    // Only the visible, enabled original generic view owns menu hit testing.
    // Underlying stacked screens have these bits cleared by the game.
    if(!node(view,root)||root.at(0)!=0x0039E3D8||(root.flags()&6)!=6||!managed_view(view))return;
    static uint32_t last_view;
    if(std::getenv("BF_TRACE_UI")&&view!=last_view) {
        std::fprintf(stderr,"[UI] active view=%08X tag=%08X\n",view,root.at(0x24));last_view=view;
    }
    bf_ui_menu_seen();fixture();
    BfUiPointer pointer{};
    if(!bf_ui_pointer(&pointer))return;
    if(std::getenv("BF_TRACE_UI"))std::fprintf(stderr,"[UI] pointer action=%d xy=%.1f,%.1f\n",pointer.action,pointer.x,pointer.y);
    std::vector<uint32_t> children;if(!vector(view,0x74,children))return;
    if(pointer.action==2){bf_ui_pad_press(1);return;} // Xbox B via normal input polling.
    for(size_t i=0;i<children.size();++i) {
        Node item;BfUiRect rect;
        if(!node(children[i],item)||(item.flags()&6)!=6)continue;
        if(item.at(0)!=0x0039E140&&item.at(0)!=0x0039E310&&item.at(0)!=0x0039DFC8&&item.at(0)!=0x0039E020)continue;
        if(
           !position(children[i],rect)||!bf_ui_contains(rect,pointer.x,pointer.y))continue;
        if(item.at(0)==0x0039E140 && !(item.at(0x74)&255)) {
            if(!(item.flags()&1)&&!bf_guest_menu_focus(view,uint32_t(i)))return;
            if(pointer.action==1)bf_ui_pad_press(0);
        } else if(item.at(0)==0x0039E310) {
            if(!(item.flags()&1)&&!bf_guest_menu_focus(view,uint32_t(i)))return;
            std::vector<uint32_t> rows;if(!vector(children[i],0x7C,rows))continue;
            for(size_t row=0;row<rows.size();++row) {
                BfUiRect hit;Node entry;
                if(!node(rows[row],entry)||(entry.flags()&6)!=6||!position(rows[row],hit)||
                   !bf_ui_contains(hit,pointer.x,pointer.y))continue;
                if(!bf_guest_list_focus(children[i],uint32_t(row)))return;
                if(pointer.action==1)bf_ui_pad_press(0);
                break;
            }
        } else if(item.at(0)==0x0039DFC8) {
            std::vector<uint32_t> keys;
            uint32_t columns=item.at(0x80),rows=item.at(0x7C);
            if(!columns||columns>32||!rows||rows>16||!item.at(0xAC))continue;
            keys.resize(columns*rows);
            if(!read(item.at(0xAC),keys.data(),keys.size()*4))continue;
            for(size_t key=0;key<keys.size();++key) {
                BfUiRect hit;
                if(!key_position(keys[key],hit)||!bf_ui_contains(hit,pointer.x,pointer.y))continue;
                if(pointer.action==1) {
                    // Dispatch after original keyboard Update, as native typing
                    // does. A queued controller press can otherwise arrive only
                    // after a later mouse event has moved focus to another key.
                    bf_text_click(children[i],uint32_t(key%columns),uint32_t(key/columns));
                } else if(!bf_guest_keyboard_focus(children[i],uint32_t(key%columns),uint32_t(key/columns)))return;
                if(std::getenv("BF_TRACE_UI"))
                    std::fprintf(stderr,"[UI] keyboard key=%zu rect=%.1f,%.1f %.1fx%.1f\n",key,hit.x,hit.y,hit.w,hit.h);
                break;
            }
        } else if(item.at(0)==0x0039E020&&pointer.action==1) {
            // The prompt's glyph uses the game's Xbox-controller font. A/B
            // and X/Y encoding was verified against Select/Back/New Profile.
            uint16_t glyph{};
            if(read(children[i]+0xC4,&glyph,2)) {
                if(glyph=='A')bf_ui_pad_press(0);
                else if(glyph=='B')bf_ui_pad_press(1);
                else if(glyph=='D')bf_ui_pad_press(2);
                else if(glyph=='C')bf_ui_pad_press(3);
            }
        }
        if(std::getenv("BF_TRACE_UI"))std::fprintf(stderr,"[UI] %s view=%08X node=%08X xy=%.1f,%.1f flags=%02X\n",
            pointer.action?"click":"hover",view,children[i],pointer.x,pointer.y,item.flags());
        break;
    }
}
}
extern "C" void bf_game_menu_tick(uint32_t view) {
    try {tick(view);}catch(const std::exception &e){bf_ui_focus_lost();std::fprintf(stderr,"[UI] Menu input released: %s\n",e.what());}
}
extern "C" int bf_game_prompt(uint32_t object) try {
    if(!bf_ui_keyboard_mode())return 0;
    Node n;BfUiRect rect;uint16_t glyph{};
    if(!node(object,n)||n.at(0)!=0x0039E020||!(n.flags()&4)||
       !read(object+0xC4,&glyph,2)||!position(object+0x60,rect))return 0;
    const char *key=glyph=='A'?"LMB":glyph=='B'?"ESC":glyph=='D'?"C":glyph=='C'?"V":nullptr;
    if(!key)return 0; // Unknown glyph retains the original rendering.
    bf_ui_prompt(rect,key);return 1;
} catch(const std::exception &e) {
    std::fprintf(stderr,"[UI] Original prompt retained: %s\n",e.what());return 0;
}
