#include "text_events.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
int bf_guest_keyboard_activate(uint32_t,uint32_t,uint32_t);
int bf_guest_keyboard_cancel(uint32_t);
}
namespace {
bool read(uint32_t va,void *out,size_t bytes) {
    if(va<0x10000 || uint64_t(va)+bytes>UINT32_MAX) return false;
    const auto *p=reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    SIZE_T got{};
    return ReadProcessMemory(GetCurrentProcess(),p,out,bytes,&got) && got==bytes;
#else
    if(uint64_t(va)+bytes>g_xbox_total_ram) return false;
    std::memcpy(out,p,bytes);return true;
#endif
}
bool word(uint32_t va,uint32_t &v){return read(va,&v,4);}
bool span(uint32_t object,uint32_t field,std::vector<uint32_t> &out) {
    uint32_t begin{},end{};
    if(!word(object+field,begin)||!word(object+field+4,end)||!begin||end<begin||
       (end-begin)%4||(end-begin)/4>512) return false;
    out.resize((end-begin)/4);return read(begin,out.data(),out.size()*4);
}
uint32_t child(uint32_t object,uint32_t field,uint32_t tag) {
    std::vector<uint32_t> list;
    if(!span(object,field,list))return 0;
    for(auto node:list){uint32_t id{};if(node && word(node+0x24,id)&&id==tag)return node;}
    return 0;
}
uint32_t glyph(uint32_t key) {
    // Same traversal as original 0x00100F90/0x001098F0/0x00109A60:
    // key+4 panel -> child tag 0x0B75A0FB -> text tag 0x0025B9F2.
    auto node=child(key+4,0x74,0x0B75A0FB);
    if(!node)return 0;
    auto text=child(node,0xEC,0x0025B9F2);
    if(!text)return 0;
    // The game's 28-byte wstring has an allocator DWORD, a 16-byte inline
    // buffer at +4, length at +0x14 and capacity at +0x18 (SSO capacity 7).
    uint32_t length{},capacity{},ptr{};uint16_t value{};
    const auto string=text+0x60;
    if(!word(string+0x14,length)||!word(string+0x18,capacity)||length!=1)return 0;
    if(capacity<8)ptr=string+4;else if(!word(string+4,ptr))return 0;
    return read(ptr,&value,2)?value:0;
}
bool keyboard(uint32_t object,uint32_t &columns,uint32_t &rows) {
    uint32_t table{};
    // Exact retail vtable, verified against the authenticated XBE and its
    // constructor 0x0010D240. The object address itself is supplied dynamically.
    return object<=UINT32_MAX-0x200 && word(object,table)&&table==0x0039DFC8 &&
        word(object+0x80,columns)&&columns>0&&columns<=32&&
        word(object+0x7C,rows)&&rows>0&&rows<=16;
}
bool enabled() {
    static const bool on=[](){auto *v=std::getenv("BF_NATIVE_TEXT_INPUT");return !v||*v!='0';}();
    return on;
}
void trace_text(uint32_t object,uint32_t codepoint,bool applied) {
    if(!std::getenv("BF_TRACE_TEXT"))return;
    uint32_t length{},capacity{},ptr{};
    const auto string=object+0x128;
    std::array<uint16_t,65> value{};
    if(word(string+0x14,length)&&word(string+0x18,capacity)&&length<=64) {
        ptr=string+4;
        if(capacity>=8&&!word(string+4,ptr))length=0;
        if(length&&!read(ptr,value.data(),length*2))length=0;
    }
    std::fprintf(stderr,"[TEXT] U+%04X keyboard=%08X applied=%d length=%u value=",codepoint,object,applied,length);
    for(uint32_t i=0;i<length;++i)std::fprintf(stderr,"%c",value[i]>=32&&value[i]<127?char(value[i]):'?');
    std::fputc('\n',stderr);
}
void diagnostic_input() {
    // Purpose-built test fixture feeds the same queue as window messages.
    // No OS input injection. A file is optional and absent in normal launches.
    static const char *path=std::getenv("BF_TEXT_INPUT_LIVE");
    static long consumed=0;
    if(!path)return;
    FILE *f=std::fopen(path,"rb");if(!f)return;
    if(std::fseek(f,consumed,SEEK_SET)){std::fclose(f);return;}
    int ch;
    while((ch=std::fgetc(f))!=EOF){++consumed;bf_text_character(uint32_t(ch));}
    std::fclose(f);
}
}
extern "C" void bf_game_text_seen(uint32_t object) {
    uint32_t columns{},rows{};
    if(enabled()&&keyboard(object,columns,rows))bf_text_seen(object);
}
static void text_tick(uint32_t object) {
    uint32_t columns{},rows{};
    if(!enabled()||!bf_text_active()||!keyboard(object,columns,rows))return;
    diagnostic_input();
    BfTextEvent event{};
    // Bound work per frame; the queue preserves order between frames.
    for(int n=0;n<16 && bf_text_take(object,&event);++n) {
        if(event.click) {
            uint32_t matrix{},key{},tag{};
            if(event.column>=columns||event.row>=rows||!word(object+0xAC,matrix)||
               !word(matrix+4*(event.row*columns+event.column),key)||!word(key,tag))break;
            const bool applied=bf_guest_keyboard_activate(object,event.column,event.row)!=0;
            if(tag==0x043D9074)bf_text_reset(); // Done may destroy this object.
            else trace_text(object,0,applied);
            break; // Revalidate the original object/page before the next click.
        }
        const uint32_t codepoint=event.codepoint;
        if(codepoint==27){bf_guest_keyboard_cancel(object);bf_text_reset();break;}
        uint32_t wanted=codepoint;
        if(wanted>='a'&&wanted<='z')wanted-=32; // Original normal keyboard is uppercase.
        const uint32_t tag=wanted==8?0x04515786:wanted==13?0x043D9074:wanted==32?0xE9782CC5:0;
        std::vector<uint32_t> keys,matrix;
        if(!span(object,0xBC,keys))continue;
        uint32_t matrix_ptr{};
        if(!word(object+0xAC,matrix_ptr))continue;
        matrix.resize(columns*rows);
        if(!read(matrix_ptr,matrix.data(),matrix.size()*4))continue;
        bool applied=false;
        for(auto key:keys) {
            uint32_t id{};if(!word(key,id))continue;
            if(tag ? id!=tag : glyph(key)!=wanted)continue;
            for(size_t i=0;i<matrix.size();++i)if(matrix[i]==key) {
                applied=bf_guest_keyboard_activate(object,uint32_t(i%columns),uint32_t(i/columns))!=0;
                break;
            }
            break;
        }
        if(wanted!=13)trace_text(object,codepoint,applied); // Confirm may destroy the object.
        if(wanted==13 || !applied){if(wanted==13)bf_text_reset();break;}
    }
}
extern "C" void bf_game_text_tick(uint32_t object) {
    try { text_tick(object); }
    catch (const std::exception &error) {
        bf_text_reset();
        std::fprintf(stderr,"[TEXT] Native text input released: %s\n",error.what());
    }
}
