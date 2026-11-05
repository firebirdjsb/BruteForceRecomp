#include "temporal_trace_backend.h"
#include "upscaler_state.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
}
namespace {
struct Configuration {
    bool enabled=false,runtime=false; unsigned frames=4;
    std::filesystem::path directory,trigger;
    Configuration() noexcept try {
        const char *render=std::getenv("BF_MOTION_RENDER_DIAGNOSTIC");
        runtime=render&&!std::strcmp(render,"1");
        const char *path=std::getenv("BF_MOTION_TRACE_DIR");
        if(!path||!*path)return;
        directory=path;enabled=true;
        if(const char *p=std::getenv("BF_MOTION_TRACE_TRIGGER"))trigger=p;
        if(const char *p=std::getenv("BF_MOTION_TRACE_FRAMES")) {
            char *end=nullptr;const auto n=std::strtoul(p,&end,10);
            if(end&&!*end&&n>=1&&n<=8)frames=unsigned(n);
        }
    } catch(...) {enabled=false;}
};
const Configuration &config(){static const Configuration c;return c;}
std::atomic<bool> failed{false};
std::atomic<bool> completed{false};
struct Lifetime {uint32_t object{},generation{};bool live{};};
std::array<Lifetime,4096> lifetimes{};
struct SceneLifetime {uint32_t object{},context{},resource{},generation{};bool live{};};
std::array<SceneLifetime,64> scenes{};
uint32_t scene_generation=0;
std::mutex lifetime_mutex;
std::atomic<uint32_t> sequence{0};
thread_local std::array<BFMotionRecord,16> producer_stack{};
thread_local unsigned producer_depth=0;
BFMotionRecord consumed{},pending{};
unsigned pending_words=0;
uint32_t checksum(const BFMotionRecord &r) {
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<BF_MO_CHECKSUM;++i)hash=(hash^r.words[i])*16777619u;
    return hash;
}
void seal(BFMotionRecord &r,unsigned kind) {
    r.words[BF_MO_VERSION]=1;r.words[BF_MO_KIND]=kind;
    if(kind!=BF_MOTION_PART_BEGIN)r.words[BF_MO_SEQUENCE]=sequence.fetch_add(1,std::memory_order_relaxed)+1;
    r.words[BF_MO_CHECKSUM]=checksum(r);
}
bool read(uint64_t va,void *out,size_t bytes) noexcept {
    const uint64_t ram=g_xbox_total_ram;
    if(va>UINT32_MAX||bytes>0x100000000ull-va)return false;
    if(!((va>=0x10000&&va<ram&&bytes<=ram-va)||
       (va>=0x80000000ull&&va-0x80000000ull<ram&&bytes<=ram-(va-0x80000000ull))))return false;
    const uintptr_t base=uintptr_t(xbox_GetMemoryOffset());
    if(va>(std::numeric_limits<uintptr_t>::max)()-base)return false;
    const uintptr_t address=base+uintptr_t(va);
    if(bytes>(std::numeric_limits<uintptr_t>::max)()-address)return false;
#ifdef _WIN32
    uintptr_t cursor=address,end=address+bytes;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void *>(cursor),&info,sizeof info)||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const uintptr_t start=reinterpret_cast<uintptr_t>(info.BaseAddress);
        if(info.RegionSize>(std::numeric_limits<uintptr_t>::max)()-start)return false;
        const uintptr_t next=start+info.RegionSize;if(next<=cursor)return false;cursor=next;
    }
    SIZE_T got{};
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void *>(address),out,bytes,&got)&&got==bytes;
#else
    std::memcpy(out,reinterpret_cast<void *>(address),bytes);return true;
#endif
}
bool push(BFMotionRecord &r,unsigned kind) {
    if(producer_depth>=producer_stack.size()){bf_motion_trace_failure("producer_scope_overflow");return false;}
    seal(r,kind);producer_stack[producer_depth++]=r;return true;
}
struct Trace {
    FILE *json=nullptr,*data=nullptr;
    bool started=false,finished=false,ui=false;unsigned frames=0;
    uint64_t bytes=0,draws=0,paths[3]{},candidates=0,identified=0,unsupported=0;
    uint32_t frame=0;unsigned records=0;
    ~Trace(){if(json)std::fclose(json);if(data)std::fclose(data);}
    bool start() {
        if(started)return true;if(finished)return false;
        std::error_code ec;
        if(!config().trigger.empty()&&!std::filesystem::exists(config().trigger,ec))return false;
        std::filesystem::create_directories(config().directory,ec);if(ec)return false;
        const auto j=config().directory/"motion.jsonl",d=config().directory/"motion-data.bin";
#ifdef _WIN32
        json=_wfopen(j.c_str(),L"wb");data=_wfopen(d.c_str(),L"wb");
#else
        json=std::fopen(j.c_str(),"wb");data=std::fopen(d.c_str(),"wb");
#endif
        if(!json||!data){finished=true;return false;}started=true;
        std::fputs("{\"kind\":\"configuration\",\"schema\":1,\"sdk_ready\":false,\"motion_vectors_produced\":false,\"data_byte_limit\":67108864}\n",json);
        std::fflush(json);return true;
    }
    bool append(const void *p,size_t n) {
        constexpr uint64_t limit=64ull<<20;
        if(!n)return true;
        if(!p||n>limit-bytes)return false;
        if(std::fwrite(p,1,n,data)!=n)return false;bytes+=n;return true;
    }
    void summary() {
        if(!started||finished)return;
        std::fprintf(json,"{\"kind\":\"frame\",\"frame\":%u,\"draws\":%llu,\"base\":%llu,\"transformed\":%llu,\"vsh\":%llu,\"depth_candidates\":%llu,\"identified_vsh_candidates\":%llu,\"unsupported_candidates\":%llu,\"sdk_ready\":false,\"previous_vertices_available\":false}\n",
            frame,(unsigned long long)draws,(unsigned long long)paths[0],(unsigned long long)paths[1],
            (unsigned long long)paths[2],(unsigned long long)candidates,(unsigned long long)identified,(unsigned long long)unsupported);
        std::fflush(json);std::fflush(data);draws=candidates=identified=unsupported=0;
        std::memset(paths,0,sizeof paths);
        if(++frames>=config().frames){finished=true;completed.store(true,std::memory_order_relaxed);std::fputs("[MOTION] Bounded diagnostic capture complete; temporal readiness remains false.\n",stderr);}
    }
};
Trace trace;
uint64_t hash_bytes(const void *p,size_t bytes) {
    const auto *b=static_cast<const unsigned char *>(p);uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<bytes;++i)h=(h^b[i])*1099511628211ull;return h;
}
}
extern "C" int bf_motion_trace_configured(){return config().enabled&&!failed.load(std::memory_order_relaxed)&&!completed.load(std::memory_order_relaxed);}
extern "C" int bf_motion_trace_active(){return bf_motion_trace_configured()&&trace.started&&!trace.finished;}
extern "C" int bf_motion_observer_enabled(){
    if(failed.load(std::memory_order_relaxed))return 0;
    if(config().runtime||bf_motion_trace_configured())return 1;
    const unsigned mode=bf_upscaler_mode();
    return (mode==3||mode==4)&&(bf_upscaler_capabilities_mask()&(1u<<mode));
}
extern "C" void bf_motion_trace_failure(const char *reason) {
    if(!failed.exchange(true,std::memory_order_relaxed))std::fprintf(stderr,"[MOTION] Diagnostic disabled: %s; sdk_ready=false.\n",reason);
}
extern "C" int bf_motion_record_valid(const BFMotionRecord *r) {
    return r&&r->words[BF_MO_VERSION]==1&&r->words[BF_MO_KIND]>=1&&r->words[BF_MO_KIND]<=6&&
        r->words[BF_MO_CHECKSUM]==checksum(*r);
}
extern "C" int bf_motion_reservation_plan(uint32_t original,uint32_t *minimum,uint32_t *preferred) {
    if(!minimum||!preferred||original<16||original>0x100000||(original&3))return 0;
    // Exact 00371E10: arg1 is the minimum end-window before wrapping; arg2
    // is the preferred end-window. 00371F3C subtracts 0x204 from that end.
    // Keep the original preferred/half-preferred policy wherever it suffices.
    constexpr uint32_t needed=(BF_MOTION_WORDS+1)*8+0x204;
    const uint32_t half=((original>>1)+3)&~3u;
    *minimum=half<needed?needed:half;*preferred=original<needed?needed:original;return 1;
}
extern "C" void bf_motion_reservation_log(uint32_t device,uint32_t stack,uint32_t original,
    uint32_t minimum,uint32_t preferred,int before,int after) {
    static std::atomic<unsigned> count{0};
    if(count.fetch_add(1,std::memory_order_relaxed)>=4&&after==1)return;
    uint32_t bounds[3]{};const bool valid=read(device,bounds,sizeof bounds);
    std::fprintf(stderr,"[MOTION-RESERVE] device=%08X stack=%08X original_bytes=%u minimum_bytes=%u preferred_bytes=%u before=%d after=%d mapped=%u cursor=%08X watermark=%08X device_flags=%08X\n",
        device,stack,original,minimum,preferred,before,after,unsigned(valid),bounds[0],bounds[1],bounds[2]);
}
extern "C" void bf_motion_lifetime(uint32_t object,int create) try {
    // Common 155340 ctor / 1553F0 dtor own the uint16 live slot at +22.
    uint16_t slot=0;if(!read(uint64_t(object)+0x22,&slot,2)||slot>=lifetimes.size())return;
    std::lock_guard<std::mutex> lock(lifetime_mutex);auto &l=lifetimes[slot];
    if(create) {
        if(l.live||l.generation==UINT32_MAX){bf_motion_trace_failure("lifetime_slot_collision_or_wrap");return;}
        l.object=object;++l.generation;l.live=true;
    } else if(l.live&&l.object==object)l.live=false;
} catch(...) {bf_motion_trace_failure("lifetime_observation_exception");}
extern "C" void bf_motion_scene_lifetime(uint32_t object,int create) try {
    if(!object)return;
    std::lock_guard<std::mutex> lock(lifetime_mutex);
    SceneLifetime *found=nullptr;
    for(auto &s:scenes)if(s.object==object){found=&s;break;}
    if(!create){if(found)found->live=false;return;}
    if(!found)for(auto &s:scenes)if(!s.live){found=&s;break;}
    if(!found||scene_generation==UINT32_MAX){bf_motion_trace_failure("scene_lifetime_capacity_or_wrap");return;}
    // Actual 1F45A0 ctor: original scene +1C is renderer context, +20 is the
    // level geometry resource. This plain 0x78-byte object has no actor slot.
    uint32_t context=0,resource=0;
    if(!read(uint64_t(object)+0x1C,&context,4)||!read(uint64_t(object)+0x20,&resource,4)||!context||!resource)return;
    *found={object,context,resource,++scene_generation,true};
} catch(...) {bf_motion_trace_failure("scene_lifetime_observation_exception");}
extern "C" int bf_motion_record_scene(uint32_t stack,BFMotionRecord *r) {
    if(!r||!bf_motion_observer_enabled())return 0;*r=producer_depth?producer_stack[producer_depth-1]:BFMotionRecord{};
    uint32_t args[3]{},resource=0,geometry=0,raw_words[2]{};
    if(!read(stack,args,sizeof args))return 0;
    const uint32_t object=args[1];uint32_t generation=0;
    {
        std::lock_guard<std::mutex> lock(lifetime_mutex);
        for(const auto &s:scenes)if(s.live&&s.object==object&&s.context==r->words[BF_MO_CONTEXT]) {
            generation=s.generation;resource=s.resource;break;
        }
    }
    // Preserve exact raw observations without interpreting a retail resource
    // span. Original 36FF10 uses resource+4 (scene+28) as Data; original 372150
    // passes resource+8 (scene+2C) into synchronization routine 371CE0. The
    // latter is NOT a proven Size, regardless of its value or alignment.
    read(uint64_t(object)+0x20,&resource,4);
    read(uint64_t(resource)+0x10,&geometry,4);
    read(uint64_t(object)+0x28,raw_words,sizeof raw_words);
    r->words[BF_MO_OBJECT]=object;r->words[BF_MO_GENERATION]=0;r->words[BF_MO_SLOT]=UINT32_MAX;
    r->words[BF_MO_RESOURCE]=resource;r->words[BF_MO_NODE]=geometry;
    r->words[BF_MO_STATIC_RAW_DATA]=raw_words[0];r->words[BF_MO_STATIC_RAW_SYNC]=raw_words[1];
    r->words[BF_MO_STATIC_OBSERVED_GENERATION]=generation;
    r->words[BF_MO_DRAW_TARGET]=0x001F5600;r->words[BF_MO_CALLSITE]=args[0]>=5?args[0]-5:0;
    return push(*r,BF_MOTION_MODEL_BEGIN);
}
extern "C" int bf_motion_record_view(uint32_t stack,BFMotionRecord *r) {
    if(!r||!bf_motion_observer_enabled())return 0;*r={};uint32_t args[3]{};
    // Original D9F00 call frame: return PC, renderer context, view object.
    if(!read(stack,args,sizeof args))return 0;
    if(args[0]!=0x0006FCE5u&&args[0]!=0x0006FE8Bu&&args[0]!=0x0006C64Bu)return 0;
    r->words[BF_MO_CONTEXT]=args[1];r->words[BF_MO_VIEW]=args[2];
    uint32_t projection=0;unsigned char flags=0;
    bool valid=read(uint64_t(args[2])+0x50,&projection,4)&&read(uint64_t(args[2])+0x4C,&flags,1)&&
        read(uint64_t(projection)+0x14,r->words+BF_MO_PROJECTION_WORDS,17*4)&&
        read(uint64_t(args[2])+0x58,r->words+BF_MO_FRUSTUM,24*4);
    // These are observed scalar fields/planes, NOT a recovered 4x4 view matrix.
    if(valid)for(unsigned i=BF_MO_PROJECTION_WORDS;i<BF_MO_FRUSTUM+24;++i) {
        float n;std::memcpy(&n,r->words+i,4);if(!std::isfinite(n))valid=false;
    }
    r->words[BF_MO_PROJECTION]=projection;r->words[BF_MO_VIEW_FLAGS]=flags;r->words[BF_MO_VIEW_VALID]=valid;
    return push(*r,BF_MOTION_VIEW_BEGIN);
}
extern "C" int bf_motion_record_model(uint32_t object,BFMotionRecord *r) {
    if(!r||!bf_motion_observer_enabled())return 0;*r=producer_depth?producer_stack[producer_depth-1]:BFMotionRecord{};
    r->words[BF_MO_OBJECT]=object;r->words[BF_MO_GENERATION]=0;r->words[BF_MO_SLOT]=UINT32_MAX;
    std::memset(r->words+BF_MO_RESOURCE,0,5*4);
    uint16_t slot=0;
    if(read(uint64_t(object)+0x22,&slot,2)&&slot<lifetimes.size()) {
        std::lock_guard<std::mutex> lock(lifetime_mutex);const auto &l=lifetimes[slot];
        if(l.live&&l.object==object){r->words[BF_MO_GENERATION]=l.generation;r->words[BF_MO_SLOT]=slot;}
    }
    read(uint64_t(object)+0xC,r->words+BF_MO_RESOURCE,4);
    read(uint64_t(object)+0x6C,r->words+BF_MO_NODE,4);
    r->words[BF_MO_DRAW_TARGET]=0x00157B70u;
    return push(*r,BF_MOTION_MODEL_BEGIN);
}
extern "C" int bf_motion_record_object(uint32_t object,uint32_t target,uint32_t callsite,BFMotionRecord *r) {
    if(!bf_motion_record_model(object,r))return 0;
    uint32_t table=0,actual=0;
    if(!read(object,&table,4)||!read(uint64_t(table)+0x64,&actual,4)||actual!=target)
        r->words[BF_MO_GENERATION]=0;
    r->words[BF_MO_DRAW_TARGET]=target;r->words[BF_MO_CALLSITE]=callsite;
    // record_model pushed the scope before these actual dispatch details were
    // available. Replace that one top record; do not introduce another scope.
    seal(*r,BF_MOTION_MODEL_BEGIN);producer_stack[producer_depth-1]=*r;return 1;
}
extern "C" int bf_motion_record_part(uint32_t packet,uint32_t descriptor,uint32_t subdraw,BFMotionRecord *r) {
    if(!r||!bf_motion_observer_enabled())return 0;*r=producer_depth?producer_stack[producer_depth-1]:BFMotionRecord{};
    uint32_t instance=0,transform=0,part=0,context=0,view=0;
    // 157BBE sets packet+990=instance; 23C232 sets descriptor+0=node+14;
    // 23C30C sets descriptor+2C=original 44-byte mesh-part record. The
    // subdraw pointer is the original 48-byte record, before material +20.
    const bool packet_read=read(uint64_t(packet)+0x990,&instance,4);
    // Generic actual virtual+64 scopes also cover derived renderers which do
    // not set packet+990. Such a zero must never manufacture a model instance;
    // the enclosing observed lifetime/dispatch provides ownership instead.
    // A child mesh explicitly named by the original packet+990 has its own
    // observed lifetime. Do not borrow the parent's generation, or invent one.
    if(packet_read&&instance&&instance!=r->words[BF_MO_OBJECT]) {
        uint16_t slot=0;uint32_t attached_context=0;
        if(read(uint64_t(instance)+0x22,&slot,2)&&slot<lifetimes.size()&&
           read(uint64_t(instance)+8,&attached_context,4)&&attached_context==r->words[BF_MO_CONTEXT]) {
            std::lock_guard<std::mutex> lock(lifetime_mutex);const auto &l=lifetimes[slot];
            if(l.live&&l.object==instance) {
                r->words[BF_MO_OBJECT]=instance;r->words[BF_MO_GENERATION]=l.generation;r->words[BF_MO_SLOT]=slot;
                read(uint64_t(instance)+0xC,r->words+BF_MO_RESOURCE,4);
            }
        }
    }
    const bool owner=r->words[BF_MO_OBJECT]&&r->words[BF_MO_GENERATION]&&r->words[BF_MO_DRAW_TARGET];
    const bool envelope=read(uint64_t(packet)+0x80,&context,4)&&read(uint64_t(packet)+0x8C,&view,4)&&
        context==r->words[BF_MO_CONTEXT]&&view==r->words[BF_MO_VIEW];
    const bool descriptor_ok=read(descriptor,&transform,4)&&transform>=0x10014u&&
        read(uint64_t(descriptor)+0x2C,&part,4);
    const bool good=packet_read&&owner&&envelope&&descriptor_ok&&(!instance||instance==r->words[BF_MO_OBJECT]);
    r->words[BF_MO_NODE]=descriptor_ok?transform-0x14:0;r->words[BF_MO_PART]=descriptor_ok?part:0;
    r->words[BF_MO_SUBDRAW]=descriptor_ok?subdraw:0;
    if(!good)r->words[BF_MO_GENERATION]=0;
    read(uint64_t(descriptor)+0x18,r->words+BF_MO_MATERIAL,4);
    r->words[BF_MO_PACKET_INSTANCE]=instance;
    return push(*r,BF_MOTION_PART_BEGIN);
}
extern "C" int bf_motion_record_end(unsigned kind,BFMotionRecord *r) {
    if(!r||!bf_motion_observer_enabled()||!producer_depth)return 0;
    const unsigned begin=producer_stack[producer_depth-1].words[BF_MO_KIND];
    if(kind!=begin+1){bf_motion_trace_failure("producer_unbalanced_scope");return 0;}
    --producer_depth;*r=producer_depth?producer_stack[producer_depth-1]:BFMotionRecord{};seal(*r,kind);return 1;
}
extern "C" int bf_motion_command_word(uint32_t word) try {
    if(!pending_words) {
        if(word!=BF_MOTION_MARKER)return 0;
        pending={};pending_words=1;return 1;
    }
    pending.words[pending_words-1]=word;
    if(++pending_words>BF_MOTION_WORDS) {
        pending_words=0;
        if(!bf_motion_record_valid(&pending)){bf_motion_trace_failure("fifo_record_checksum");return 1;}
        consumed=pending;
        if(bf_motion_trace_configured()&&(trace.started||pending.words[BF_MO_KIND]==BF_MOTION_VIEW_BEGIN)&&trace.start()&&!trace.finished&&trace.records++<32768) {
            std::fprintf(trace.json,"{\"kind\":\"scope\",\"record\":[");
            for(unsigned i=0;i<BF_MOTION_WORDS;++i)std::fprintf(trace.json,"%s%u",i?",":"",pending.words[i]);
            std::fputs("]}\n",trace.json);
        }
    }
    return 1;
} catch(...) {bf_motion_trace_failure("command_diagnostic_exception");pending_words=0;return 1;}
extern "C" int bf_motion_current_record(BFMotionRecord *record) {
    if(!record)return 0;*record={};
    if(!bf_motion_observer_enabled()||!bf_motion_record_valid(&consumed)||
       !consumed.words[BF_MO_CONTEXT]||!consumed.words[BF_MO_VIEW])return 0;
    *record=consumed;return 1;
}
extern "C" int bf_motion_draw_record(uint32_t command,BFMotionRecord *r) {
    if(!bf_motion_current_record(r))return 0;auto *w=r->words;
    (void)command; // Original origin remains diagnostic until retail ownership is proved.
    if(w[BF_MO_DRAW_TARGET]==0x001F5600||w[BF_MO_SLOT]==UINT32_MAX)return 0;
    return w[BF_MO_KIND]==BF_MOTION_PART_BEGIN&&w[BF_MO_OBJECT]&&w[BF_MO_GENERATION]&&w[BF_MO_VIEW_VALID]&&
        w[BF_MO_SLOT]<4096&&
        w[BF_MO_NODE]&&w[BF_MO_PART]&&w[BF_MO_SUBDRAW];
}
extern "C" void bf_motion_trace_draw(const Nv2aSurface *s,const Nv2aRenderState *rs,unsigned path,const Nv2aXVsBatch *v,uint32_t frame,int surface_ui) try {
    if(!bf_motion_trace_configured()||path>2||!s||!rs||!trace.started||trace.finished)return;
    trace.frame=frame;++trace.draws;++trace.paths[path];
    BFMotionRecord resolved=consumed;const bool resolved_identity=bf_motion_draw_record(v?v->command_va:0,&resolved)!=0;
    const auto *w=resolved.words;
    const bool candidate=!surface_ui&&(rs->depth_test_enable||rs->depth_write);
    const bool identity=resolved_identity;
    const bool supported=identity&&path==2&&v&&v->vertex_ids&&v->nverts&&v->nindices;
    if(candidate){++trace.candidates;if(supported)++trace.identified;else ++trace.unsupported;}
    if(trace.records++>=32768)return;
    const uint64_t offset=trace.bytes;bool saved=false;uint64_t program=0,constants=0,ids=0;
    if(v&&v->program&&v->consts&&v->vertex_ids&&v->nverts<=65536&&v->nindices<=196608&&v->ninstr<=136&&v->ninputs<=16&&v->nstreams<=16) {
        program=hash_bytes(v->program,size_t(v->ninstr)*16);constants=hash_bytes(v->consts,192*16);ids=hash_bytes(v->vertex_ids,size_t(v->nverts)*4);
        // Sidecar sections have fixed ordering/sizes described by this header.
        uint32_t header[10]={0x314D4642u,v->ninstr,v->nverts,v->nindices,v->ninputs,v->input_mask,v->nstreams,v->consts_gen,0,0};
        uint64_t need=sizeof header+size_t(v->ninstr)*16+192*16+size_t(v->nverts+v->nindices)*4+sizeof(v->in)+sizeof(v->aa);
        if(v->nstreams)for(unsigned i=0;i<v->nstreams;++i)need+=8ull+v->streams[i].bytes;
        else need+=size_t(v->nverts)*v->ninputs*16;
        if(need<=(64ull<<20)-trace.bytes) {
            saved=trace.append(header,sizeof header)&&trace.append(v->program,size_t(v->ninstr)*16)&&trace.append(v->consts,192*16)&&
                trace.append(v->vertex_ids,size_t(v->nverts)*4)&&trace.append(v->indices,size_t(v->nindices)*4)&&
                trace.append(v->in,sizeof v->in)&&trace.append(v->aa,sizeof v->aa);
            if(v->nstreams)for(unsigned i=0;i<v->nstreams;++i) {
                uint32_t span[2]={v->streams[i].bytes,v->streams[i].stride};
                saved=trace.append(span,sizeof span)&&trace.append(v->streams[i].data,v->streams[i].bytes)&&saved;
            } else saved=trace.append(v->inputs,size_t(v->nverts)*v->ninputs*16)&&saved;
        }
    }
    std::fprintf(trace.json,"{\"kind\":\"draw\",\"frame\":%u,\"path\":%u,\"color_va\":%u,\"depth_va\":%u,\"width\":%u,\"height\":%u,\"before_ui\":%s,\"surface_ui\":%s,\"blend_enable\":%u,\"blend_src\":%u,\"blend_dst\":%u,\"blend_equation\":%u,\"color_write_mask\":%u,\"alpha_test\":%u,\"alpha_func\":%u,\"alpha_ref\":%u,\"depth_test_enable\":%u,\"depth_write\":%u,\"stencil_enable\":%u,\"command_va\":%u,\"identity_kind\":\"%s\",\"depth_candidate\":%s,\"identity_proven\":%s,\"unsupported\":%s,\"instance\":%u,\"generation\":%u,\"slot\":%u,\"resource\":%u,\"node\":%u,\"part\":%u,\"subdraw\":%u,\"view\":%u,\"context\":%u,\"draw_target\":%u,\"callsite\":%u,\"packet_instance\":%u,\"static_raw_data\":%u,\"static_unverified_sync_word\":%u,\"static_observed_ctor_generation\":%u,\"program_hash\":\"%016llx\",\"constant_hash\":\"%016llx\",\"original_id_hash\":\"%016llx\",\"data_offset\":%llu,\"data_bytes\":%llu,\"consumed_inputs_saved\":%s,\"sdk_ready\":false}\n",
        frame,path,s->color_va,rs->zeta_va,s->width,s->height,surface_ui?"false":"true",surface_ui?"true":"false",
        rs->blend_enable,rs->blend_src,rs->blend_dst,rs->blend_eq,rs->color_mask,rs->alpha_test_enable,rs->alpha_func,rs->alpha_ref,rs->depth_test_enable,rs->depth_write,rs->stencil_enable,v?v->command_va:0,
        identity?(w[BF_MO_SLOT]==UINT32_MAX?"static_command":"actor_part"):"unknown",
        candidate?"true":"false",identity?"true":"false",supported?"false":"true",
        w[BF_MO_OBJECT],w[BF_MO_GENERATION],w[BF_MO_SLOT],w[BF_MO_RESOURCE],w[BF_MO_NODE],w[BF_MO_PART],w[BF_MO_SUBDRAW],w[BF_MO_VIEW],w[BF_MO_CONTEXT],w[BF_MO_DRAW_TARGET],w[BF_MO_CALLSITE],w[BF_MO_KIND]==BF_MOTION_PART_BEGIN?w[BF_MO_PACKET_INSTANCE]:0,
        w[BF_MO_DRAW_TARGET]==0x001F5600?w[BF_MO_STATIC_RAW_DATA]:0,
        w[BF_MO_DRAW_TARGET]==0x001F5600?w[BF_MO_STATIC_RAW_SYNC]:0,
        w[BF_MO_DRAW_TARGET]==0x001F5600?w[BF_MO_STATIC_OBSERVED_GENERATION]:0,
        (unsigned long long)program,(unsigned long long)constants,(unsigned long long)ids,(unsigned long long)offset,(unsigned long long)(trace.bytes-offset),saved?"true":"false");
} catch(...) {bf_motion_trace_failure("draw_diagnostic_exception");}
extern "C" void bf_motion_trace_ui(uint32_t frame) {if(bf_motion_trace_configured()){trace.frame=frame;trace.ui=true;}}
extern "C" void bf_motion_trace_flip(uint32_t frame) {
    if(bf_motion_observer_enabled()) {
        if(bf_motion_trace_configured()){trace.frame=frame;trace.summary();}
        trace.ui=false;consumed={};
    }
}
