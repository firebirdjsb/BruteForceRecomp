#include "motion_history.h"
#include "motion_hlsl.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <d3dcompiler.h>
namespace bf::gpu {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char scatter_shader[]=R"(
struct Position {float4 clip;uint id;uint frame;uint valid;uint pad;};
Buffer<uint> source:register(t0);
StructuredBuffer<uint> domain:register(t1);
RWStructuredBuffer<Position> current:register(u0);
cbuffer Scatter:register(b0) {uint records;uint count;uint frame;uint unused;};
[numthreads(64,1,1)] void main(uint3 thread:SV_DispatchThreadID) {
    if(thread.x>=records)return;
    uint at=thread.x*5,id=source[at+4],lo=0,hi=count;
    while(lo<hi) {uint mid=lo+(hi-lo)/2;if(domain[mid]<id)lo=mid+1;else hi=mid;}
    if(lo>=count||domain[lo]!=id)return;
    float4 clip=asfloat(uint4(source[at],source[at+1],source[at+2],source[at+3]));
    Position p;p.clip=clip;p.id=id;p.frame=frame;
    p.valid=all(isfinite(clip))&&abs(clip.w)>1e-20;p.pad=0;
    current[lo]=p;
})";
constexpr char lookup_shader[]=BF_MOTION_LOOKUP_HLSL;
struct KeyHash {
    size_t operator()(const MotionIdentity &k) const noexcept {
        uint64_t h=1469598103934665603ull;
        const uint32_t words[]={k.object,k.generation,k.live_slot,k.view,k.context,k.node,k.part,k.subdraw,
            uint32_t(k.program),uint32_t(k.program>>32),uint32_t(k.topology),uint32_t(k.topology>>32),uint32_t(k.kind)};
        for(uint32_t w:words)h=(h^w)*1099511628211ull;return size_t(h);
    }
};
bool identity_valid(const MotionIdentity &k) {
    const bool owner=k.kind==MotionIdentityKind::actor?k.live_slot<4096:
        k.kind==MotionIdentityKind::static_pushbuffer&&k.live_slot==UINT32_MAX&&k.part==k.subdraw;
    return owner&&k.object&&k.generation&&k.view&&k.context&&k.node&&k.part&&k.subdraw&&k.program&&k.topology;
}
// Preserve every compute-stage binding modified by scatter, including dynamic
// linkage instances and append/consume counter values (UINT(-1) preserves them).
struct ComputeState {
    ID3D11DeviceContext *context;
    ComPtr<ID3D11ComputeShader> shader;
    std::array<ID3D11ClassInstance *,256> instances{};UINT instance_count=256;
    ComPtr<ID3D11ShaderResourceView> sources[2];ComPtr<ID3D11UnorderedAccessView> output;
    ComPtr<ID3D11Buffer> constants;
    explicit ComputeState(ID3D11DeviceContext *c):context(c) {
        c->CSGetShader(&shader,instances.data(),&instance_count);
        ID3D11ShaderResourceView *s[2]{};c->CSGetShaderResources(0,2,s);sources[0].Attach(s[0]);sources[1].Attach(s[1]);
        c->CSGetUnorderedAccessViews(0,1,&output);c->CSGetConstantBuffers(0,1,&constants);
    }
    ~ComputeState() {
        ID3D11UnorderedAccessView *none=nullptr;UINT keep=UINT(-1);
        context->CSSetUnorderedAccessViews(0,1,&none,&keep);
        ID3D11ShaderResourceView *s[2]={sources[0].Get(),sources[1].Get()};context->CSSetShaderResources(0,2,s);
        auto *u=output.Get();context->CSSetUnorderedAccessViews(0,1,&u,&keep);
        auto *b=constants.Get();context->CSSetConstantBuffers(0,1,&b);
        context->CSSetShader(shader.Get(),instances.data(),instance_count);
        for(UINT i=0;i<instance_count;++i)if(instances[i])instances[i]->Release();
    }
};
}
struct MotionHistory::Impl {
    struct Entry {
        std::vector<uint32_t> domain;
        ComPtr<ID3D11Buffer> buffers[2],dictionary;
        ComPtr<ID3D11ShaderResourceView> views[2],ids;
        ComPtr<ID3D11UnorderedAccessView> output[2];
        uint64_t bytes{},used{};
        uint32_t prepared{},completed{};
        unsigned current{};
        bool previous_valid{},captured{},rejected{};
        MotionReset reason=MotionReset::first_frame;
    };
    struct Slot {uint32_t object{},generation{};};
    std::array<Slot,4096> slots{};
    std::unordered_map<uint32_t,uint32_t> static_generations;
    std::unordered_map<MotionIdentity,Entry,KeyHash> entries;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ComputeShader> scatter;ComPtr<ID3D11Buffer> constants;
    uint64_t budget{},retained{},evictions{},invalidations{},clock{};
    uint32_t frame{},last_completed{};
    MotionReset reason=MotionReset::none;
    void clear(MotionReset why) {
        entries.clear();slots={};static_generations.clear();retained=0;frame=last_completed=0;reason=why;++invalidations;
    }
    bool initialize(ID3D11Device *d,ID3D11DeviceContext *c,uint64_t limit) {
        if(!d||!c||!limit||limit>MotionHistory::max_budget||d->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0||
           c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        ComPtr<ID3D11Device> owner;c->GetDevice(&owner);if(owner.Get()!=d)return false;
        device=d;context=c;budget=limit;
        ComPtr<ID3DBlob> code,errors;
        if(FAILED(D3DCompile(scatter_shader,sizeof scatter_shader-1,"motion-history-scatter",nullptr,nullptr,"main","cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors)))return false;
        if(FAILED(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&scatter)))return false;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(d->CreateBuffer(&desc,nullptr,&constants));
    }
    bool make_entry(Entry &e,const std::vector<uint32_t> &ids) {
        e.domain=ids;e.bytes=uint64_t(ids.size())*(2*sizeof(MotionPosition)+sizeof(uint32_t));
        std::vector<MotionPosition> initial(ids.size());
        for(size_t i=0;i<ids.size();++i)initial[i].original_id=ids[i];
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(initial.size()*sizeof(MotionPosition));
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(MotionPosition);
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=initial.data();
        for(unsigned i=0;i<2;++i) {
            if(FAILED(device->CreateBuffer(&desc,&data,&e.buffers[i]))||
               FAILED(device->CreateShaderResourceView(e.buffers[i].Get(),nullptr,&e.views[i]))||
               FAILED(device->CreateUnorderedAccessView(e.buffers[i].Get(),nullptr,&e.output[i])))return false;
        }
        desc.ByteWidth=UINT(ids.size()*4);desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.StructureByteStride=4;
        data.pSysMem=ids.data();
        return SUCCEEDED(device->CreateBuffer(&desc,&data,&e.dictionary))&&
               SUCCEEDED(device->CreateShaderResourceView(e.dictionary.Get(),nullptr,&e.ids));
    }
    bool evict_to_fit(uint64_t bytes) {
        if(bytes>budget)return false;
        while(retained>budget-bytes||entries.size()>=MotionHistory::max_entries) {
            auto victim=entries.end();
            for(auto it=entries.begin();it!=entries.end();++it)if(it->second.prepared!=frame&&
                (victim==entries.end()||it->second.used<victim->second.used))victim=it;
            if(victim==entries.end())return false;
            retained-=victim->second.bytes;entries.erase(victim);++evictions;
        }
        return true;
    }
};
MotionHistory::MotionHistory(ID3D11Device *d,ID3D11DeviceContext *c,uint64_t budget) noexcept try {
    impl_=std::make_unique<Impl>();if(!impl_->initialize(d,c,budget))impl_.reset();
} catch(...) {impl_.reset();}
MotionHistory::~MotionHistory()=default;
bool MotionHistory::ready() const noexcept{return impl_!=nullptr;}
bool MotionHistory::begin_frame(uint32_t frame) noexcept {
    if(!impl_||!frame)return false;auto &s=*impl_;
    if(s.frame==frame)return true;
    if(s.frame||(s.last_completed&&uint64_t(s.last_completed)+1!=frame))s.clear(MotionReset::frame_gap);
    s.frame=frame;return true;
}
MotionPrevious MotionHistory::prepare(const MotionIdentity &key,const uint32_t *ids,uint32_t count) noexcept try {
    MotionPrevious result;
    if(!impl_){result.reason=MotionReset::resource_failure;return result;}auto &s=*impl_;
    if(!s.frame){result.reason=s.reason=MotionReset::frame_not_open;return result;}
    if(!identity_valid(key)){result.reason=s.reason=MotionReset::invalid_identity;return result;}
    if(!ids||!count||count>65536){result.reason=s.reason=MotionReset::invalid_domain;return result;}
    std::vector<uint32_t> domain(ids,ids+count);std::sort(domain.begin(),domain.end());
    if(std::adjacent_find(domain.begin(),domain.end())!=domain.end()) {
        result.reason=s.reason=MotionReset::invalid_domain;return result;
    }
    MotionReset why=s.reason==MotionReset::frame_gap?MotionReset::frame_gap:MotionReset::first_frame;
    if(key.kind==MotionIdentityKind::actor) {
      auto &slot=s.slots[key.live_slot];
      if(slot.generation&&(slot.generation!=key.generation||slot.object!=key.object)) {
        for(auto it=s.entries.begin();it!=s.entries.end();)if(it->first.kind==MotionIdentityKind::actor&&it->first.live_slot==key.live_slot) {
            s.retained-=it->second.bytes;it=s.entries.erase(it);
        } else ++it;
        ++s.invalidations;why=MotionReset::slot_reused;
      }
      slot={key.object,key.generation};
    } else {
      auto known=s.static_generations.find(key.object);
      if(known!=s.static_generations.end()&&known->second!=key.generation) {
        for(auto it=s.entries.begin();it!=s.entries.end();)if(it->first.kind==MotionIdentityKind::static_pushbuffer&&it->first.object==key.object) {
            s.retained-=it->second.bytes;it=s.entries.erase(it);
        } else ++it;
        ++s.invalidations;why=MotionReset::slot_reused;
      }
      // Bound lifetime bookkeeping independently of compiled-command count.
      if(known==s.static_generations.end()&&s.static_generations.size()>=MotionHistory::max_entries) {
          result.reason=s.reason=MotionReset::budget_exhausted;return result;
      }
      s.static_generations[key.object]=key.generation;
    }
    auto it=s.entries.find(key);
    if(it!=s.entries.end()&&it->second.domain!=domain) {
        s.retained-=it->second.bytes;s.entries.erase(it);it=s.entries.end();
        ++s.invalidations;why=MotionReset::domain_changed;
    }
    if(it==s.entries.end()) {
        const uint64_t bytes=uint64_t(domain.size())*(2*sizeof(MotionPosition)+4);
        if(!s.evict_to_fit(bytes)){result.reason=s.reason=MotionReset::budget_exhausted;return result;}
        Impl::Entry e;
        if(!s.make_entry(e,domain)){result.reason=s.reason=MotionReset::resource_failure;return result;}
        e.reason=why;it=s.entries.emplace(key,std::move(e)).first;s.retained+=it->second.bytes;
    }
    auto &e=it->second;e.used=++s.clock;
    if(e.prepared!=s.frame) {
        e.previous_valid=e.completed!=0&&uint64_t(e.completed)+1==s.frame;
        if(e.previous_valid){e.current^=1;e.reason=MotionReset::none;}
        else if(e.prepared)e.reason=MotionReset::frame_gap;
        e.prepared=s.frame;e.captured=e.rejected=false;
    }
    result.positions=e.views[e.current^1];result.count=uint32_t(e.domain.size());
    result.expected_frame=e.previous_valid?s.frame-1:0;result.frame_valid=e.previous_valid;result.reason=s.reason=e.reason;
    return result;
} catch(...) {if(impl_)impl_->reason=MotionReset::resource_failure;MotionPrevious v;v.reason=MotionReset::resource_failure;return v;}
bool MotionHistory::capture(const MotionIdentity &key,ID3D11ShaderResourceView *source,uint32_t records) noexcept try {
    if(!impl_)return false;auto &s=*impl_;auto it=s.entries.find(key);
    if(!s.frame||it==s.entries.end()||it->second.prepared!=s.frame) {s.reason=MotionReset::frame_not_open;return false;}
    auto &e=it->second;
    if(e.captured||e.rejected) {e.rejected=true;e.completed=0;e.reason=s.reason=MotionReset::duplicate_submission;++s.invalidations;return false;}
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};if(source)source->GetDesc(&desc);
    if(!source||!records||records>1048576||desc.Format!=DXGI_FORMAT_R32_UINT||
       desc.ViewDimension!=D3D11_SRV_DIMENSION_BUFFER||uint64_t(records)*5>desc.Buffer.NumElements) {
        e.rejected=true;e.reason=s.reason=MotionReset::invalid_domain;return false;
    }
    ComputeState restore(s.context.Get());
    const uint32_t constants[4]={records,uint32_t(e.domain.size()),s.frame,0};
    s.context->UpdateSubresource(s.constants.Get(),0,nullptr,constants,0,0);
    ID3D11ShaderResourceView *sources[2]={source,e.ids.Get()};
    ID3D11UnorderedAccessView *output=e.output[e.current].Get();UINT keep=UINT(-1);
    auto *cb=s.constants.Get();s.context->CSSetShader(s.scatter.Get(),nullptr,0);
    s.context->CSSetConstantBuffers(0,1,&cb);s.context->CSSetShaderResources(0,2,sources);
    s.context->CSSetUnorderedAccessViews(0,1,&output,&keep);s.context->Dispatch((records+63)/64,1,1);
    e.captured=true;s.reason=MotionReset::none;return true;
} catch(...) {if(impl_)impl_->reason=MotionReset::resource_failure;return false;}
bool MotionHistory::complete_frame(uint32_t frame) noexcept {
    if(!impl_||!impl_->frame||impl_->frame!=frame)return false;auto &s=*impl_;
    for(auto &[key,e]:s.entries)if(e.prepared==frame)e.completed=e.captured&&!e.rejected?frame:0;
    s.last_completed=frame;s.frame=0;return true;
}
void MotionHistory::reset() noexcept {if(impl_)impl_->clear(MotionReset::first_frame);}
MotionReset MotionHistory::last_reason() const noexcept{return impl_?impl_->reason:MotionReset::resource_failure;}
MotionHistoryStats MotionHistory::stats() const noexcept {
    if(!impl_)return {};return {impl_->retained,impl_->entries.size(),impl_->evictions,impl_->invalidations};
}
const char *MotionHistory::reason_name(MotionReset r) noexcept {
    switch(r) {
    case MotionReset::none:return "none";case MotionReset::first_frame:return "first_frame";
    case MotionReset::frame_gap:return "frame_gap";case MotionReset::slot_reused:return "slot_reused";
    case MotionReset::domain_changed:return "original_id_domain_changed";
    case MotionReset::duplicate_submission:return "duplicate_semantic_submission";
    case MotionReset::budget_exhausted:return "budget_exhausted";case MotionReset::invalid_identity:return "invalid_identity";
    case MotionReset::invalid_domain:return "invalid_original_id_domain";case MotionReset::resource_failure:return "resource_failure";
    case MotionReset::frame_not_open:return "source_frame_not_open";
    }return "invalid_reason";
}
const char *MotionHistory::lookup_hlsl() noexcept{return lookup_shader;}
}
