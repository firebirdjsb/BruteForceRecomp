#include "upscaler_state.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace {
std::mutex guard;
unsigned requested_mode=BF_UPSCALER_OFF;
unsigned actual_major=BF_UPSCALER_OFF;
bool spatial_available{},fsr3_available{},fsr4_available{};
bool initialized{};
std::string provider,reason;

bool valid_mode(unsigned mode) {
    return mode==BF_UPSCALER_OFF||mode==BF_UPSCALER_FSR1||
           mode==BF_UPSCALER_FSR3||mode==BF_UPSCALER_FSR4;
}
unsigned environment_mode(const char *value,bool &valid) {
    valid=true;
    if(!value||!*value||!std::strcmp(value,"off"))return BF_UPSCALER_OFF;
    if(!std::strcmp(value,"fsr1"))return BF_UPSCALER_FSR1;
    if(!std::strcmp(value,"fsr3"))return BF_UPSCALER_FSR3;
    if(!std::strcmp(value,"fsr4"))return BF_UPSCALER_FSR4;
    valid=false;return BF_UPSCALER_OFF;
}
void initialize_environment() {
    if(initialized)return;
    initialized=true;
    const char *value=std::getenv("BF_UPSCALER");
    if(value&&*value) {
        bool recognized=false;
        requested_mode=environment_mode(value,recognized);
        if(!recognized)reason="Unknown BF_UPSCALER request";
    }
}
bool available(unsigned mode) {
    switch(mode) {
    case BF_UPSCALER_OFF:return true;
    case BF_UPSCALER_FSR1:return spatial_available;
    case BF_UPSCALER_FSR3:return fsr3_available;
    case BF_UPSCALER_FSR4:return fsr4_available;
    default:return false;
    }
}
const char *mode_name(unsigned mode) {
    switch(mode) {
    case BF_UPSCALER_FSR1:return "FSR 1";
    case BF_UPSCALER_FSR3:return "FSR 3";
    case BF_UPSCALER_FSR4:return "FSR 4";
    default:return "OFF";
    }
}
void copy_text(char *buffer,unsigned capacity,const std::string &value) {
    if(!buffer||!capacity)return;
    const size_t count=std::min<size_t>(capacity-1,value.size());
    if(count)std::memcpy(buffer,value.data(),count);
    buffer[count]='\0';
}
}

extern "C" unsigned bf_upscaler_mode(void) {
    std::lock_guard lock(guard);initialize_environment();return requested_mode;
}

extern "C" void bf_upscaler_initial(unsigned mode) {
    std::lock_guard lock(guard);
    const char *value=std::getenv("BF_UPSCALER");
    initialized=true;
    if(value&&*value) {
        bool recognized=false;
        requested_mode=environment_mode(value,recognized);
        if(recognized)reason.clear();
        else reason="Unknown BF_UPSCALER request";
    } else {
        requested_mode=valid_mode(mode)?mode:BF_UPSCALER_OFF;
        reason.clear();
    }
}

extern "C" int bf_upscaler_select(unsigned mode) {
    std::lock_guard lock(guard);
    initialize_environment();
    if(!valid_mode(mode)||!available(mode))return 0;
    requested_mode=mode;reason.clear();return 1;
}

extern "C" void bf_upscaler_capabilities(int spatial,int fsr3,int fsr4) {
    std::lock_guard lock(guard);
    if(spatial>=0)spatial_available=spatial!=0;
    if(fsr3>=0)fsr3_available=fsr3!=0;
    if(fsr4>=0)fsr4_available=fsr4!=0;
}

extern "C" void bf_upscaler_spatial_supported(int spatial) {
    std::lock_guard lock(guard);spatial_available=spatial!=0;
}

extern "C" unsigned bf_upscaler_capabilities_mask(void) {
    std::lock_guard lock(guard);
    return 1u|(spatial_available?1u<<BF_UPSCALER_FSR1:0u)|
        (fsr3_available?1u<<BF_UPSCALER_FSR3:0u)|
        (fsr4_available?1u<<BF_UPSCALER_FSR4:0u);
}

extern "C" void bf_upscaler_status(unsigned major,const char *name,const char *fallback_reason) {
    std::lock_guard lock(guard);
    initialize_environment();
    actual_major=valid_mode(major)?major:BF_UPSCALER_OFF;
    provider=name?name:"";reason=fallback_reason?fallback_reason:"";
    if(provider.size()>128)provider.resize(128);
    if(reason.size()>128)reason.resize(128);
}

extern "C" void bf_upscaler_status_copy(char *buffer,unsigned capacity) {
    if(!buffer||!capacity)return;
    std::lock_guard lock(guard);
    initialize_environment();
    std::string text;
    const bool matched=requested_mode==actual_major;
    if(!reason.empty()) {
        if(matched) {text="ACTIVE ";text+=mode_name(actual_major);}
        else {text="REQUEST ";text+=mode_name(requested_mode);text+=" / ACTIVE ";text+=mode_name(actual_major);}
        if(!provider.empty()) {text+=" / ";text+=provider;}
        text+=" / ";text+=reason;
    } else if(matched&&!provider.empty()) {
        text="ACTIVE ";text+=mode_name(actual_major);text+=" / ";text+=provider;
    } else if(matched) {
        text="REQUEST ";text+=mode_name(requested_mode);text+=" / RUNTIME PENDING";
    } else {
        text="REQUEST ";text+=mode_name(requested_mode);text+=" / ACTIVE ";text+=mode_name(actual_major);
        if(!provider.empty()) {text+=" / ";text+=provider;}
    }
    copy_text(buffer,capacity,text);
}
