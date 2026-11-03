#include "ui/host_ui.h"
#include "text_events.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
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
bool write_flags(uint32_t object,uint32_t flags) {
    // Only a validated live node's four-byte +0x28 UI flag field is written.
    // These are ordinary mutable guest UI data, never executable code pages.
    const uint64_t physical=object&0x0FFFFFFFu;
    const uintptr_t offset=uintptr_t(xbox_GetMemoryOffset());
    if(object<0x10000||uint64_t(object)+0x2C>UINT32_MAX||physical+0x2C>g_xbox_total_ram||
       offset>UINTPTR_MAX-object-0x28)return false;
    const uintptr_t address=offset+object+0x28;
    if(address>UINTPTR_MAX-sizeof flags)return false;
    auto *p=reinterpret_cast<void *>(address);
#ifdef _WIN32
    // Validate every region touched by this four-byte field, including a
    // possible page boundary. Never alter protection to make a write succeed.
    uintptr_t cursor=address;const uintptr_t end=address+sizeof flags;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(reinterpret_cast<void *>(cursor),&info,sizeof info)!=sizeof info||
           info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
           !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY)))return false;
        const uintptr_t base=reinterpret_cast<uintptr_t>(info.BaseAddress);
        if(cursor<base||cursor-base>=info.RegionSize)return false;
        const uintptr_t available=info.RegionSize-(cursor-base);
        cursor+=(std::min)(available,end-cursor);
    }
    SIZE_T written{};return WriteProcessMemory(GetCurrentProcess(),p,&flags,sizeof flags,&written)&&written==sizeof flags;
#else
    if(uint64_t(object)+0x2C>g_xbox_total_ram)return false;
    std::memcpy(p,&flags,sizeof flags);return true;
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
bool text_content(uint32_t object,std::string &out) {
    // Original text class uses the same 28-byte UTF-16 wstring as the native
    // keyboard adapter: allocator at +0x60, pointer/inline buffer at +0x64,
    // length at +0x74, capacity at +0x78. sub_0010A630 reads these same fields.
    uint32_t length{},capacity{},pointer{};
    if(object>UINT32_MAX-0x7C||!read(object+0x74,&length,4)||!read(object+0x78,&capacity,4)||
       !length||length>256||capacity<length||capacity>65536)return false;
    pointer=object+0x64;
    if(capacity>=8&&!read(pointer,&pointer,4))return false;
    const uint64_t end=uint64_t(pointer)+length*2u;
    constexpr uint32_t contiguous_alias=0x80000000u;
    // Text storage must be wholly inside mapped guest RAM or its contiguous
    // 0x80000000 alias; a corrupt wstring must never read arbitrary host data.
    if(pointer<0x10000||!(end<=g_xbox_total_ram||
       (pointer>=contiguous_alias&&end<=uint64_t(contiguous_alias)+g_xbox_total_ram)))return false;
    std::array<uint16_t,256> units{};
    if(!read(pointer,units.data(),length*2))return false;
    out.clear();out.reserve(length);
    for(unsigned i=0;i<length;i++) {
        const auto ch=units[i];
        if(ch>127)return false; // Unverified/localized text keeps original art.
        out.push_back(char(ch>='a'&&ch<='z'?ch-'a'+'A':ch));
    }
    return true;
}
struct TutorialMask {
    struct Item { uint32_t object,tag,vtable,flags; };
    std::array<Item,32> items{};
    unsigned count{};
    ~TutorialMask() {
        for(unsigned i=0;i<count;i++) {
            const auto &item=items[i];Node current;
            // Resolve identity again before undoing the temporary visibility
            // bit. Preserve any other flags changed by original draw code.
            if(node(item.object,current)&&current.at(0)==item.vtable&&current.at(0x24)==item.tag) {
                const uint32_t restored=(current.at(0x28)&~4u)|(item.flags&4u);
                if(!write_flags(item.object,restored))bf_ui_tutorial_clear();
            } else bf_ui_tutorial_clear();
        }
    }
};
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
extern "C" void *bf_game_tutorial_begin(uint32_t view) try {
    static uint32_t active_tutorial_view{};
    Node root;BfUiRect root_rect;
    if(!node(view,root)||root.at(0)!=0x0039E3D8||(root.flags()&6)!=6||!managed_view(view)) {
        // Nested unregistered panels must not disarm their enclosing dialog.
        // A later draw of the actual dialog after it closes clears at once.
        if(view==active_tutorial_view){bf_ui_tutorial_clear();active_tutorial_view=0;}
        return nullptr;
    }
    std::vector<uint32_t> children;
    if(!position(view,root_rect)||!vector(view,0x74,children)||children.size()>128){bf_ui_tutorial_clear();return nullptr;}
    std::vector<BfUiTutorialNode> descriptors;descriptors.reserve(children.size());
    bool basic_title=false,left_thumbstick=false,right_thumbstick=false;
    for(auto child:children) {
        Node n;BfUiRect rect;
        if(!node(child,n)||!position(child,rect)){bf_ui_tutorial_clear();return nullptr;}
        descriptors.push_back({n.at(0x24),n.at(0),n.at(0x28),rect});
        if(n.at(0)==0x0039E0C0) {
            std::string text;
            if(n.at(0x24)==0x0BB9C4AF&&text_content(child,text)&&text=="BASIC CONTROLS")basic_title=true;
            if(n.at(0x24)==0xF1E4D812&&text_content(child,text)&&text.find("LEFT THUMBSTICK")!=std::string::npos)left_thumbstick=true;
            if(n.at(0x24)==0x09D15D91&&text_content(child,text)&&text.find("RIGHT THUMBSTICK")!=std::string::npos)right_thumbstick=true;
        }
    }
    if(!basic_title||!left_thumbstick||!right_thumbstick){bf_ui_tutorial_clear();return nullptr;}
    active_tutorial_view=view;
    if(!bf_ui_tutorial_prepare(root.at(0x24),root_rect,descriptors.data(),unsigned(descriptors.size())))return nullptr;
    auto mask=std::make_unique<TutorialMask>();
    for(size_t i=0;i<children.size();i++) {
        const auto &item=descriptors[i];
        if(!bf_ui_tutorial_hide(item.tag,item.vtable))continue;
        if(mask->count==mask->items.size()||!write_flags(children[i],item.flags&~4u)) {
            bf_ui_tutorial_clear();return nullptr; // RAII restores earlier nodes.
        }
        mask->items[mask->count++]={children[i],item.tag,item.vtable,item.flags};
    }
    // Only original image/text Draw calls see the temporary bit. No object,
    // event, input callback, prompt or tutorial-progression state is replaced.
    return mask.release();
} catch(const std::exception &e) {
    bf_ui_tutorial_clear();std::fprintf(stderr,"[UI] Original tutorial retained: %s\n",e.what());return nullptr;
}
extern "C" void bf_game_tutorial_end(void *token) {
    delete static_cast<TutorialMask *>(token);
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
