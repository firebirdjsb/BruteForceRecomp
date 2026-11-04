#include "prompt_text.h"
#include "host_ui.h"
#include "../input/control_labels.h"
#include <array>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
}
namespace {
char16_t lower(char16_t c){return c>=u'A'&&c<=u'Z'?c+u'a'-u'A':c;}
bool word(char16_t c){c=lower(c);return (c>=u'a'&&c<=u'z')||(c>=u'0'&&c<=u'9')||c==u'_';}
bool matches(std::u16string_view text,size_t at,std::u16string_view pattern) {
    if(at+pattern.size()>text.size()||(at&&word(text[at-1]))||
       (at+pattern.size()<text.size()&&word(text[at+pattern.size()])))return false;
    for(size_t i=0;i<pattern.size();i++)if(lower(text[at+i])!=pattern[i])return false;
    return true;
}
std::u16string label(unsigned control,bool desktop) {
    const char *key=bf::input::control_label(control,false,desktop);std::u16string result;
    for(;key&&*key;key++)result.push_back(char16_t(*key));return result;
}
bool range(uint32_t va,size_t bytes) {
    const uint64_t end=uint64_t(va)+bytes;
    return va>=0x10000&&(end<=g_xbox_total_ram||
        (va>=0x80000000u&&end<=0x80000000ull+g_xbox_total_ram));
}
bool read(uint32_t va,void *out,size_t bytes) {
    if(!range(va,bytes))return false;
    auto *p=reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),p,out,bytes,&got)&&got==bytes;
#else
    std::memcpy(out,p,bytes);return true;
#endif
}
bool write(uint32_t va,const void *data,size_t bytes) {
    if(!range(va,bytes))return false;
    auto *p=reinterpret_cast<void *>(uintptr_t(xbox_GetMemoryOffset())+va);
#ifdef _WIN32
    uintptr_t cursor=reinterpret_cast<uintptr_t>(p),end=cursor+bytes;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void *>(cursor),&info,sizeof info)||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY)))return false;
        const auto next=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
        if(next<=cursor)return false;cursor=next;
    }
    SIZE_T done{};return WriteProcessMemory(GetCurrentProcess(),p,data,bytes,&done)&&done==bytes;
#else
    std::memcpy(p,data,bytes);return true;
#endif
}
}
namespace bf::ui {
std::u16string desktop_prompt_text(std::u16string_view input,bool desktop) {
    std::u16string result;result.reserve(input.size());
    constexpr std::u16string_view starts[]={u"press the start button",u"press start button",u"start button",u"press start"};
    constexpr std::u16string_view pulls[]={u"pull the left trigger",u"pull left trigger",
        u"pull the right trigger",u"pull right trigger"};
    for(size_t at=0;at<input.size();) {
        bool replaced=false;
        for(const auto phrase:starts)if(matches(input,at,phrase)) {
            if(phrase.substr(0,5)==u"press")result+=u"Press ";
            result+=label(12,desktop);at+=phrase.size();replaced=true;break;
        }
        if(replaced)continue;
        for(size_t i=0;i<std::size(pulls);i++)if(matches(input,at,pulls[i])) {
            unsigned control=i<2?6:7; // Original default; contextual fire overrides handedness.
            const size_t next=at+pulls[i].size();
            for(size_t j=next;j<input.size()&&j<next+64;j++) {
                if(input[j]==u'\n'||input[j]==u'.'||input[j]==u',')break;
                if(matches(input,j,u"fire")){control=7;break;}
                if(matches(input,j,u"use")||matches(input,j,u"throw")){control=6;break;}
            }
            result+=control==7?u"Click ":u"Press ";result+=label(control,desktop);
            at=next;replaced=true;break;
        }
        if(!replaced)result.push_back(input[at++]);
    }
    return result;
}
}
extern "C" uint32_t bf_prompt_text_frame(uint32_t frame) try {
    if(!bf_ui_keyboard_mode()||frame<0x10800u)return 0;
    // Audited CCD20 entry: return+10 DWORD arguments (44 bytes), string at
    // +0x14. Reserve 2048 unused stack bytes; copied arguments/string are
    // above the relayed callee's downward-growing locals, never caller data.
    std::array<uint32_t,11> args{};
    if(!read(frame,args.data(),sizeof args))return 0;
    std::array<char16_t,384> source{};size_t length=0;
    if(read(args[5],source.data(),sizeof source)) {
        while(length<source.size()&&source[length])length++;
    } else {
        for(;length<source.size();length++) {
            if(!read(args[5]+uint32_t(length*2),&source[length],2))return 0;
            if(!source[length])break;
        }
    }
    if(!length||length==source.size())return 0;
    bool desktop=false;
#ifdef _WIN32
    const auto *mode=std::getenv("RECOMP_MKB");desktop=mode&&*mode&&*mode!='0';
#endif
    const auto input=std::u16string_view(source.data(),length);
    const auto changed=bf::ui::desktop_prompt_text(input,desktop);
    if(changed==input||changed.size()>=source.size())return 0;
    const uint32_t relay=frame-2048u,string=relay+uint32_t(sizeof args);
    std::array<uint8_t,sizeof args+sizeof source> bytes{};
    args[5]=string;std::memcpy(bytes.data(),args.data(),sizeof args);
    std::memcpy(bytes.data()+sizeof args,changed.c_str(),(changed.size()+1)*2);
    return write(relay,bytes.data(),sizeof args+(changed.size()+1)*2)?relay:0;
} catch(...) {return 0;}
