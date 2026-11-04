// Native 16:9 camera transaction. This TU cannot access emulated registers.
#include "native_aspect.h"
#include "native_aspect_state.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <windows.h>
extern "C" {
#include "xbox_memory_layout.h"
}

namespace {
std::atomic<bool> disabled{false};
std::atomic_flag transaction_active=ATOMIC_FLAG_INIT;
std::atomic<DWORD> render_thread{0};
std::atomic<unsigned> emitted{0};
constexpr uint32_t offsets[5]={0x14,0x24,0x2C,0x38,0x40};
constexpr size_t sizes[5]={4,4,4,4,12};

bool address(uint64_t guest,size_t bytes,uintptr_t &host,bool writable) noexcept {
    const uint64_t ram=g_xbox_total_ram;
    if(guest>0xFFFFFFFFull || bytes>0x100000000ull-guest) return false;
    const bool low=guest>=0x10000 && guest<ram && bytes<=ram-guest;
    const bool high=guest>=0x80000000ull && guest-0x80000000ull<ram &&
                    bytes<=ram-(guest-0x80000000ull);
    if(!low&&!high) return false;
    const uintptr_t base=static_cast<uintptr_t>(xbox_GetMemoryOffset());
    if(guest>(std::numeric_limits<uintptr_t>::max)()-base) return false;
    host=base+static_cast<uintptr_t>(guest);
    if(bytes>(std::numeric_limits<uintptr_t>::max)()-host) return false;
    uintptr_t cursor=host;
    const uintptr_t end=host+bytes;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<const void *>(cursor),&region,sizeof(region)) ||
           region.State!=MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const DWORD protection=region.Protect&0xFFu;
        if(writable && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
           protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t region_base=reinterpret_cast<uintptr_t>(region.BaseAddress);
        if(region.RegionSize>(std::numeric_limits<uintptr_t>::max)()-region_base) return false;
        const uintptr_t next=region_base+region.RegionSize;
        if(next<=cursor) return false;
        cursor=next;
    }
    return true;
}

bool read(uint64_t guest,void *output,size_t bytes) noexcept {
    uintptr_t host=0; SIZE_T copied=0;
    return address(guest,bytes,host,false) &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void *>(host),output,bytes,&copied) && copied==bytes;
}

bool write(uint64_t guest,const void *input,size_t bytes) noexcept {
    uintptr_t host=0; SIZE_T copied=0;
    return address(guest,bytes,host,true) &&
        WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void *>(host),input,bytes,&copied) && copied==bytes;
}

bool writable(uint64_t guest,size_t bytes) noexcept {
    uintptr_t unused=0; return address(guest,bytes,unused,true);
}

uint64_t physical(uint32_t guest) noexcept {
    return guest>=0x80000000u ? uint64_t(guest)-0x80000000ull : guest;
}

bool overlaps(uint32_t a,size_t a_bytes,uint32_t b,size_t b_bytes) noexcept {
    const uint64_t x=physical(a),y=physical(b);
    return x<y+b_bytes && y<x+a_bytes;
}

float number(uint32_t bits) noexcept {
    float value=0; std::memcpy(&value,&bits,4); return value;
}

bool diagnostics() noexcept {
    const char *value=std::getenv("BF_NATIVE_ASPECT_DIAGNOSTICS");
    return value && !std::strcmp(value,"1");
}

bool enabled() noexcept {
    // Sample request/support once at entry. Completed transactions always
    // restore and close their render scope even if F1 changes the request.
    return bf_aspect_ready()!=0;
}

void release(BFAspectTransaction *transaction) noexcept {
    if(transaction->lock_held) {
        transaction->lock_held=0;
        transaction_active.clear(std::memory_order_release);
    }
}

void failure(const char *phase) noexcept {
    const bool first=!disabled.exchange(true,std::memory_order_relaxed);
    bf_aspect_scene_supported(0);
    if(first)
        std::fputs("[ASPECT] Camera invariant failed; widescreen is unavailable until restart.\n",stderr);
    if(diagnostics() && emitted.fetch_add(1,std::memory_order_relaxed)<64)
        std::fprintf(stderr,"{\"kind\":\"bf_native_aspect\",\"phase\":\"failure\",\"invariant\":\"%s\",\"disabled\":true}\n",phase);
}

bool restore_owned(BFAspectTransaction *transaction) noexcept {
    bool okay=true;
    size_t word=0;
    for(size_t i=0;i<5;++i) {
        // Each size/offset is an exact authenticated helper write range.
        if(!write(uint64_t(transaction->projection)+offsets[i],transaction->owned_projection+word,sizes[i])) okay=false;
        word+=sizes[i]/4;
    }
    if(!write(uint64_t(transaction->camera)+0x58,transaction->frustum,sizeof(transaction->frustum))) okay=false;
    if(okay) {
        uint32_t words[7]{}; unsigned char planes[0x60]{};
        word=0;
        for(size_t i=0;i<5;++i) {
            if(!read(uint64_t(transaction->projection)+offsets[i],words+word,sizes[i])) okay=false;
            word+=sizes[i]/4;
        }
        if(!read(uint64_t(transaction->camera)+0x58,planes,sizeof(planes)) ||
           std::memcmp(words,transaction->owned_projection,sizeof(words)) ||
           std::memcmp(planes,transaction->frustum,sizeof(planes))) okay=false;
    }
    if(okay) transaction->changed=0;
    return okay;
}
}

extern "C" int bf_native_aspect_prepare(uint32_t stack,BFAspectTransaction *transaction) noexcept try {
    if(!transaction || disabled.load(std::memory_order_relaxed) || !enabled()) return 0;
    uint32_t args[3]{};
    // Original untouched guest call frame: return, view context, camera.
    if((stack&3u) || !read(stack,args,sizeof(args)) || args[0]!=0x0006FCE5u || stack<0x10040u) return 0;
    const uint32_t camera=args[2];
    uint32_t projection=0,context_word=0; unsigned char flags=0;
    if((args[1]&3u) || !read(args[1],&context_word,4)) return 0;
    if((camera&3u) || camera<0x10000u || !read(uint64_t(camera)+0x4C,&flags,1) || (flags&1u) ||
       !read(uint64_t(camera)+0x50,&projection,4) || (projection&3u) || projection<0x10000u) return 0;
    uint32_t words[17]{};
    // +14 through +57 includes all projection floats and derived plane normals.
    if(!read(uint64_t(projection)+0x14,words,sizeof(words))) return 0;
    for(uint32_t bits:words) if(!std::isfinite(number(bits))) return 0;
    const float tangent_x=number(words[4]),tangent_y=number(words[5]);
    const float aspect=number(words[6]);
    if(number(words[7])!=320.0f || number(words[8])!=240.0f ||
       !(number(words[2])>0.0f && number(words[3])>number(words[2])) ||
       !(tangent_y>0.01f && tangent_y<10.0f) ||
       std::fabs(aspect-4.0f/3.0f)>0.0001f ||
       std::fabs(tangent_x/tangent_y-aspect)>0.0001f) return 0;
    if(!writable(uint64_t(camera)+0x58,0x60) || !writable(uint64_t(stack)-0x40,0x40)) return 0;
    for(size_t i=0;i<5;++i) if(!writable(uint64_t(projection)+offsets[i],sizes[i])) return 0;
    if(overlaps(camera,0xB8,projection,0x58) ||
       overlaps(stack-0x40,0x4C,camera,0xB8) ||
       overlaps(stack-0x40,0x4C,projection,0x58)) return 0;
    DWORD owner=render_thread.load(std::memory_order_relaxed);
    const DWORD current=GetCurrentThreadId();
    if(!owner) render_thread.compare_exchange_strong(owner,current,std::memory_order_relaxed);
    if(render_thread.load(std::memory_order_relaxed)!=current) { failure("render_thread_changed"); return 0; }
    if(transaction_active.test_and_set(std::memory_order_acquire)) { failure("nested_dispatch"); return 0; }
    transaction->lock_held=1;
    transaction->camera=camera; transaction->projection=projection;
    transaction->entry_stack=stack; transaction->return_address=args[0];
    size_t word=0;
    for(size_t i=0;i<5;++i) {
        if(!read(uint64_t(projection)+offsets[i],transaction->owned_projection+word,sizes[i])) { release(transaction); return 0; }
        word+=sizes[i]/4;
    }
    transaction->vertical[0]=words[1]; transaction->vertical[1]=words[5]; transaction->vertical[2]=words[10];
    transaction->clip[0]=words[2]; transaction->clip[1]=words[3];
    transaction->viewport[0]=words[7]; transaction->viewport[1]=words[8];
    if(!read(uint64_t(camera)+0x58,transaction->frustum,sizeof(transaction->frustum)) ||
       !read(uint64_t(stack)-0x40,transaction->scratch,sizeof(transaction->scratch))) { release(transaction); return 0; }
    const float target_aspect=16.0f/9.0f;
    const float target_tangent=tangent_y*target_aspect;
    transaction->changed=1;
    if(!write(uint64_t(projection)+0x24,&target_tangent,4) ||
       !write(uint64_t(projection)+0x2C,&target_aspect,4)) {
        restore_owned(transaction); release(transaction); failure("initial_write"); return 0;
    }
    return 1;
} catch(...) {
    if(transaction) { if(transaction->changed) restore_owned(transaction); release(transaction); }
    failure("prepare_exception"); return 0;
}

extern "C" int bf_native_aspect_helpers_done(BFAspectTransaction *transaction,int helpers_ok) noexcept try {
    if(!transaction || !transaction->lock_held) return 0;
    bool okay=helpers_ok!=0;
    // Restore helper scratch BEFORE entering the real renderer. Its return
    // address and 8-byte argument block begin at entry_stack and were untouched.
    if(!write(uint64_t(transaction->entry_stack)-0x40,transaction->scratch,sizeof(transaction->scratch))) okay=false;
    unsigned char scratch[0x40]{};
    if(!read(uint64_t(transaction->entry_stack)-0x40,scratch,sizeof(scratch)) ||
       std::memcmp(scratch,transaction->scratch,sizeof(scratch))) okay=false;
    uint32_t words[17]{}; float planes[24]{};
    if(!read(uint64_t(transaction->projection)+0x14,words,sizeof(words)) ||
       !read(uint64_t(transaction->camera)+0x58,planes,sizeof(planes))) okay=false;
    for(uint32_t bits:words) if(!std::isfinite(number(bits))) okay=false;
    for(float value:planes) if(!std::isfinite(value)) okay=false;
    if(words[1]!=transaction->vertical[0] || words[5]!=transaction->vertical[1] || words[10]!=transaction->vertical[2] ||
       words[2]!=transaction->clip[0] || words[3]!=transaction->clip[1] ||
       words[7]!=transaction->viewport[0] || words[8]!=transaction->viewport[1]) okay=false;
    const float expected=number(transaction->vertical[1])*(16.0f/9.0f);
    if(std::fabs(number(words[4])-expected)>0.00001f ||
       std::fabs(number(words[6])-16.0f/9.0f)>0.00001f ||
       std::fabs(number(words[9])-320.0f/expected)>0.0001f ||
       std::memcmp(transaction->frustum,planes,sizeof(planes))==0) okay=false;
    if(!okay) {
        restore_owned(transaction); release(transaction); failure("helper_projection_or_scratch"); return 0;
    }
    if(diagnostics() && emitted.fetch_add(1,std::memory_order_relaxed)<64) {
        std::fprintf(stderr,
            "{\"kind\":\"bf_native_aspect\",\"phase\":\"applied_before_original_dispatch\",\"thread\":%lu,"
            "\"camera\":\"%08X\",\"projection\":\"%08X\",\"before_tangent_x\":%.9g,\"tangent_x\":%.9g,"
            "\"tangent_y\":%.9g,\"aspect\":%.9g,\"vertical_bits_unchanged\":true,\"viewport_unchanged\":true,"
            "\"scratch_restored\":true,\"frustum_changed\":true}\n",
            GetCurrentThreadId(),transaction->camera,transaction->projection,
            number(transaction->owned_projection[1]),number(words[4]),number(words[5]),number(words[6]));
    }
    return 1;
} catch(...) {
    if(transaction) { restore_owned(transaction); release(transaction); }
    failure("helper_validation_exception"); return 0;
}

extern "C" int bf_native_aspect_restore(BFAspectTransaction *transaction) noexcept try {
    if(!transaction || !transaction->lock_held) return 1;
    const bool okay=!transaction->changed || restore_owned(transaction);
    release(transaction);
    if(!okay) failure("owned_projection_or_frustum_restore");
    else if(diagnostics() && emitted.fetch_add(1,std::memory_order_relaxed)<64)
        std::fprintf(stderr,"{\"kind\":\"bf_native_aspect\",\"phase\":\"owned_restore_verified\",\"camera\":\"%08X\",\"projection\":\"%08X\",\"restored_bytes\":124}\n",transaction->camera,transaction->projection);
    return okay?1:0;
} catch(...) {
    if(transaction) release(transaction);
    failure("restore_exception"); return 0;
}

extern "C" void bf_native_aspect_state_failure(const char *phase) noexcept {
    failure(phase?phase:"unknown_state");
}
