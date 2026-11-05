#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "motion_renderer.h"
#include "motion_history.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace bf::gpu;
namespace {
uint64_t hash_bytes(const void *data,size_t bytes) {
    uint64_t hash=1469598103934665603ull;const auto *p=static_cast<const uint8_t *>(data);
    for(size_t i=0;i<bytes;++i)hash=(hash^p[i])*1099511628211ull;return hash;
}
bool near_value(float a,float b,float tolerance=2e-5f) {
    return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance*std::max(1.f,std::abs(b));
}
// These exact actual shader bodies were replayed with consumed game inputs on
// CPU and hardware GPU. Dynamic matrix/viewport checks below remain mandatory.
// The audited mapping is OP.w=dot(world,K13)=view-Z, with the original viewport
// epilogue; arbitrary programs with merely plausible constants do not qualify.
constexpr uint64_t primary_programs[]={
    0xd5facfe9de0c6001ull,0xd3e5a89958c03ab6ull,0x7d553df26b64d371ull,0x98140cd4d2d05f3eull,
    0xdfe6a6ac5f3d0eddull,0xd6d602dce25555b9ull,0xf79a7b625b0c2e3dull,0x22fdbb10e5fc463dull,
    0x653bc021bb6ad223ull,0x582c997ba4b80db1ull,0x54bda8b6b35983f8ull,0x72724bda62d2182aull,
    0x3bb6d6214e4cb4e4ull,0xc16df920c4540a93ull,0x85166df5f6d2dc98ull,0xa8ec98bd3255f28eull,
    0xaf7a7cc6db505c95ull,0xedab14be522dd253ull,0xd3e31e0d4df3d6ccull,0x4674c6c08515c805ull,0xda32e1a09c180d1eull};
bool screen_program(const Nv2aXVsBatch *batch) {
    if(!batch||!batch->program||!batch->ninstr||batch->ninstr>136)return false;
    const auto hash=hash_bytes(batch->program,size_t(batch->ninstr)*16);
    return hash==0x4fb88543c1a077c0ull||hash==0x7697f78d3d6b7106ull||hash==0x7cf8626f15aa5029ull||
        hash==0xeb6a24caa41e93c8ull||hash==0x63900152929f9716ull;
}
struct Target {
    BfMotionSnapshot snapshot{};
    ComPtr<ID3D11Texture2D> textures[4];
    ComPtr<ID3D11RenderTargetView> views[4];
};
struct CapturePipeline {ComPtr<ID3D11GeometryShader> shader;std::vector<uint8_t> code;};
}
struct BfMotionRenderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::unique_ptr<MotionHistory> history;
    std::unordered_map<uint32_t,Target> targets;
    std::unordered_map<uint64_t,CapturePipeline> capture_pipelines;
    ComPtr<ID3D11Buffer> constants,ids,stream;
    ComPtr<ID3D11ShaderResourceView> id_view,stream_view;
    MotionIdentity current_key{};
    bool capture_current{};
    uint32_t current_frame{},current_color{};
    UINT stream_capacity{};
    Target *target(const BfMotionSource &source) {
        if(!source.frame||!source.color_va||!source.width||!source.height||source.width>16384||source.height>16384)return nullptr;
        auto found=targets.find(source.color_va);
        if(found==targets.end()) {
            if(targets.size()>=32)return nullptr;
            found=targets.emplace(source.color_va,Target{}).first;
        }
        auto &target=found->second;
        if(target.snapshot.source.width!=source.width||target.snapshot.source.height!=source.height) {
            Target replacement;
            const DXGI_FORMAT formats[]={DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R8_UNORM};
            for(unsigned index=0;index<4;++index) {
                D3D11_TEXTURE2D_DESC desc{};desc.Width=source.width;desc.Height=source.height;desc.MipLevels=desc.ArraySize=1;
                desc.Format=formats[index];desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
                if(FAILED(device->CreateTexture2D(&desc,nullptr,&replacement.textures[index]))||
                    FAILED(device->CreateRenderTargetView(replacement.textures[index].Get(),nullptr,&replacement.views[index])))return nullptr;
            }
            if(target.snapshot.source.width){history->reset();current_frame=0;}
            target=std::move(replacement);
        }
        if(target.snapshot.source.frame!=source.frame) {
            target.snapshot={};target.snapshot.source=source;
            // A cleared constant screen background has undefined prior scene
            // history: explicit zero motion, no material coverage, far depth.
            // Untracked opaque geometry subsequently overwrites with NaN and
            // taints completeness; no zero world-motion fallback exists.
            const float zero[4]{},one[4]={1,1,1,1};
            context->ClearRenderTargetView(target.views[0].Get(),zero);
            context->ClearRenderTargetView(target.views[1].Get(),zero);
            context->ClearRenderTargetView(target.views[2].Get(),one);
            context->ClearRenderTargetView(target.views[3].Get(),zero);
            target.snapshot.motion=target.textures[0].Get();target.snapshot.reactive=target.textures[1].Get();
            target.snapshot.canonical_depth=target.textures[2].Get();target.snapshot.composition=target.textures[3].Get();
        }
        if(current_frame!=source.frame) {history->begin_frame(source.frame);current_frame=source.frame;}
        return &target;
    }
};
extern "C" int bf_motion_primary_projection(const Nv2aXVsBatch *batch,const BfMotionSource *source) {
    if(!batch||!source||!batch->program||!batch->consts||!batch->ninstr||batch->ninstr>136||
       !std::isfinite(source->near_plane)||!std::isfinite(source->far_plane)||source->near_plane<=0||source->far_plane<=source->near_plane)return 0;
    const auto hash=hash_bytes(batch->program,size_t(batch->ninstr)*16);
    if(std::find(std::begin(primary_programs),std::end(primary_programs),hash)==std::end(primary_programs))return 0;
    const auto *k=batch->consts;const float a=k[8][2],b=k[8][3];
    if(!near_value(k[0][0],source->width*.5f)||!near_value(k[0][1],-source->height*.5f)||
       !near_value(k[0][2],16777215.f)||!near_value(k[1][0],source->width*.5f+.53125f)||
       !near_value(k[1][1],source->height*.5f+.53125f)||!std::isfinite(a)||a<=1||!std::isfinite(b)||b>=0||
       !near_value(-b/a,source->near_plane))return 0;
    for(unsigned component=0;component<4;++component) {
        const float homogeneous=component==3?1.f:0.f;
        if(!near_value(k[5][component],homogeneous)||!near_value(k[13][component],k[4][component])||
           !near_value(k[10][component],k[6][0]*k[2][component])||!near_value(k[11][component],k[7][1]*k[3][component])||
           !near_value(k[12][component],a*k[4][component]+b*homogeneous))return 0;
    }
    // Large-far background uses the same proven view-Z W but a different A/B;
    // canonical RT4 deliberately uses the primary scope's near/far instead.
    if(hash!=0x54bda8b6b35983f8ull) {
        const float primary_a=source->far_plane/(source->far_plane-source->near_plane);
        if(!near_value(a,primary_a,1e-6f)||!near_value(b,-source->near_plane*primary_a,1e-6f))return 0;
    }
    return 1;
}
extern "C" BfMotionRenderer *bf_motion_renderer_create(ID3D11Device *device,ID3D11DeviceContext *context) try {
    if(!device||!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return nullptr;
    ComPtr<ID3D11Device> owner;context->GetDevice(&owner);if(owner.Get()!=device)return nullptr;
    auto renderer=std::make_unique<BfMotionRenderer>();renderer->device=device;renderer->context=context;
    renderer->history=std::make_unique<MotionHistory>(device,context);if(!renderer->history->ready())return nullptr;
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(BfMotionDrawConstants);desc.Usage=D3D11_USAGE_DYNAMIC;
    desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(device->CreateBuffer(&desc,nullptr,&renderer->constants)))return nullptr;
    return renderer.release();
}catch(...){return nullptr;}
extern "C" void bf_motion_renderer_destroy(BfMotionRenderer *renderer) {delete renderer;}
extern "C" void bf_motion_renderer_clear(BfMotionRenderer *renderer,const BfMotionSource *source,int full_far) try {
    if(!renderer||!source)return;auto *target=renderer->target(*source);if(!target)return;
    if(!target->snapshot.draws)target->snapshot.clear_observed=full_far!=0;
    // Later depth alias/post-effect clears retain these world resources.
}catch(...){}
extern "C" int bf_motion_renderer_prepare(BfMotionRenderer *renderer,const BfMotionSource *source,const Nv2aRenderState *state,
    const Nv2aXVsBatch *batch,const BFMotionRecord *record,BfMotionDrawBinding *binding) try {
    if(binding)*binding={};if(!renderer||!source||!state||!binding)return 0;
    renderer->capture_current=false;
    if(screen_program(batch))return 0;
    const bool rgb=(state->color_mask&0x10101)!=0,alpha=(state->color_mask&0x1000000)!=0;
    if(!rgb&&!alpha&&!state->depth_write)return 0; // Actual query-only/no-write work.
    auto *target=renderer->target(*source);if(!target)return 0;auto &snapshot=target->snapshot;
    if((snapshot.source.view&&snapshot.source.view!=source->view)||
       (snapshot.source.context&&snapshot.source.context!=source->context)||
       (snapshot.source.depth_va&&snapshot.source.depth_va!=source->depth_va))++snapshot.unsupported_projection;
    if(!snapshot.draws||!snapshot.source.view)snapshot.source=*source;
    renderer->current_color=source->color_va;
    ++snapshot.draws;
    const bool material=state->blend_enable&&!state->depth_write;
    const bool supported_blend=state->blend_eq==0||state->blend_eq==0x8006;
    const bool transparent=(!rgb&&alpha&&!state->depth_write)||(material&&supported_blend&&
        (state->blend_src==0x302||state->blend_src==1)&&(state->blend_dst==0x303||state->blend_dst==1));
    const bool projection=bf_motion_primary_projection(batch,source)!=0;
    const bool canonical=projection&&state->depth_write;
    if(!transparent&&!projection)++snapshot.unsupported_projection;
    BfMotionDrawConstants constants{};constants.flags=BF_MOTION_DRAW_ENABLED;constants.width=float(source->width);constants.height=float(source->height);
    const float a=source->far_plane/(source->far_plane-source->near_plane);constants.depth_a=a;constants.depth_b=-source->near_plane*a;
    constants.material_reactivity=1;
    if(canonical)constants.flags|=BF_MOTION_DRAW_CANONICAL;
    if(transparent)constants.flags|=BF_MOTION_DRAW_TRANSPARENT;
    const bool jitter_valid=std::isfinite(source->jitter_x)&&std::isfinite(source->jitter_y)&&
        std::abs(source->jitter_x)<=.5f&&std::abs(source->jitter_y)<=.5f;
    if(source->apply_jitter&&!jitter_valid)++snapshot.unsupported_projection;
    if(source->apply_jitter&&(projection||transparent)&&jitter_valid) {
        constants.flags|=BF_MOTION_DRAW_JITTER;constants.jitter_x=source->jitter_x;constants.jitter_y=source->jitter_y;snapshot.jitter_applied=1;
    }
    bool tracked=false;MotionPrevious previous;
    if(batch&&batch->vertex_ids&&batch->nverts&&batch->nverts<=65536&&batch->program&&batch->ninstr&&batch->ninstr<=136&&
        batch->indices&&batch->nindices&&batch->nindices<=196608&&
        record&&record->words[BF_MO_KIND]==BF_MOTION_PART_BEGIN&&record->words[BF_MO_VIEW_VALID]&&
        record->words[BF_MO_OBJECT]&&record->words[BF_MO_GENERATION]&&record->words[BF_MO_SLOT]<4096&&
        record->words[BF_MO_DRAW_TARGET]!=0x001F5600u&&record->words[BF_MO_NODE]&&record->words[BF_MO_PART]&&record->words[BF_MO_SUBDRAW]) {
        const auto *w=record->words;MotionIdentity identity{w[BF_MO_OBJECT],w[BF_MO_GENERATION],w[BF_MO_SLOT],w[BF_MO_VIEW],w[BF_MO_CONTEXT],
            w[BF_MO_NODE],w[BF_MO_PART],w[BF_MO_SUBDRAW],hash_bytes(batch->program,size_t(batch->ninstr)*16),hash_bytes(batch->indices,size_t(batch->nindices)*4)};
        previous=renderer->history->prepare(identity,batch->vertex_ids,batch->nverts);
        if(previous.positions) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=batch->nverts*4;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};data.pSysMem=batch->vertex_ids;ComPtr<ID3D11Buffer> ids;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_UINT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=batch->nverts;
            ComPtr<ID3D11ShaderResourceView> id_view;
            if(SUCCEEDED(renderer->device->CreateBuffer(&desc,&data,&ids))&&SUCCEEDED(renderer->device->CreateShaderResourceView(ids.Get(),&view,&id_view))) {
                renderer->ids=std::move(ids);renderer->id_view=std::move(id_view);renderer->current_key=identity;renderer->capture_current=true;tracked=true;
                constants.flags|=BF_MOTION_DRAW_TRACKED;constants.original_count=batch->nverts;
                constants.previous_count=previous.count;constants.expected_frame=previous.expected_frame;
                if(previous.frame_valid)constants.flags|=BF_MOTION_DRAW_PREVIOUS;
            }
        }
    }
    if(transparent)++snapshot.transparent_draws;
    else if(!tracked)++snapshot.untracked_opaque;
    if(tracked){++snapshot.tracked_draws;if(previous.frame_valid)++snapshot.previous_draws;else{++snapshot.new_history_draws;snapshot.requires_reset=1;}}
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(renderer->context->Map(renderer->constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {++snapshot.resource_failures;return 0;}
    std::memcpy(mapped.pData,&constants,sizeof constants);renderer->context->Unmap(renderer->constants.Get(),0);
    renderer->current_color=source->color_va;
    binding->constants=renderer->constants.Get();binding->original_ids=tracked?renderer->id_view.Get():nullptr;binding->previous=tracked?previous.positions.Get():nullptr;
    binding->outputs=BF_MOTION_BIND_MASKS|(transparent?0:BF_MOTION_BIND_VECTOR)|(canonical?BF_MOTION_BIND_DEPTH:0);
    binding->tracked=tracked;binding->previous_valid=tracked&&previous.frame_valid;binding->projection_valid=projection;binding->jitter_applied=(constants.flags&BF_MOTION_DRAW_JITTER)!=0;
    return 1;
}catch(...){
    if(renderer&&source) {const auto found=renderer->targets.find(source->color_va);if(found!=renderer->targets.end())++found->second.snapshot.resource_failures;}
    return 0;
}
extern "C" void bf_motion_renderer_targets(BfMotionRenderer *renderer,uint32_t color,uint32_t frame,ID3D11RenderTargetView **motion,
    ID3D11RenderTargetView **reactive,ID3D11RenderTargetView **depth,ID3D11RenderTargetView **composition) {
    if(motion)*motion=nullptr;if(reactive)*reactive=nullptr;if(depth)*depth=nullptr;if(composition)*composition=nullptr;
    if(!renderer)return;const auto found=renderer->targets.find(color);if(found==renderer->targets.end()||found->second.snapshot.source.frame!=frame)return;
    auto &views=found->second.views;if(motion)*motion=views[0].Get();if(reactive)*reactive=views[1].Get();if(depth)*depth=views[2].Get();if(composition)*composition=views[3].Get();
}
extern "C" int bf_motion_renderer_capture(BfMotionRenderer *renderer,const void *code,size_t bytes,uint32_t vertices) try {
    if(!renderer||!renderer->capture_current||!code||!bytes||!vertices||vertices>65536)return 0;
    auto &snapshot=renderer->targets.at(renderer->current_color).snapshot;
    const auto hash=hash_bytes(code,bytes);auto found=renderer->capture_pipelines.find(hash);
    if(found==renderer->capture_pipelines.end()) {
        if(renderer->capture_pipelines.size()>=1024){++snapshot.resource_failures;return 0;}
        CapturePipeline pipeline;pipeline.code.assign(static_cast<const uint8_t *>(code),static_cast<const uint8_t *>(code)+bytes);
        const D3D11_SO_DECLARATION_ENTRY declarations[]={{0,"TEXCOORD",7,0,4,0},{0,"TEXCOORD",8,0,1,0}};UINT stride=20;
        if(FAILED(renderer->device->CreateGeometryShaderWithStreamOutput(code,bytes,declarations,2,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&pipeline.shader))) {++snapshot.resource_failures;return 0;}
        found=renderer->capture_pipelines.emplace(hash,std::move(pipeline)).first;
    } else if(found->second.code.size()!=bytes||std::memcmp(found->second.code.data(),code,bytes)) {++snapshot.resource_failures;return 0;}
    if(renderer->stream_capacity<vertices) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=vertices*20;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_STREAM_OUTPUT|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Buffer> stream;ComPtr<ID3D11ShaderResourceView> view;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_UINT;srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=vertices*5;
        if(FAILED(renderer->device->CreateBuffer(&desc,nullptr,&stream))||FAILED(renderer->device->CreateShaderResourceView(stream.Get(),&srv,&view))) {++snapshot.resource_failures;return 0;}
        renderer->stream=std::move(stream);renderer->stream_view=std::move(view);renderer->stream_capacity=vertices;
    }
    ID3D11Buffer *old_output[4]{};renderer->context->SOGetTargets(4,old_output);bool bound=false;
    for(auto *output:old_output)if(output){bound=true;output->Release();}
    if(bound){++snapshot.resource_failures;return 0;}
    ComPtr<ID3D11GeometryShader> previous;std::array<ID3D11ClassInstance *,256> instances{};UINT count=UINT(instances.size());
    renderer->context->GSGetShader(&previous,instances.data(),&count);D3D11_PRIMITIVE_TOPOLOGY topology;renderer->context->IAGetPrimitiveTopology(&topology);
    auto *output=renderer->stream.Get();UINT offset=0;renderer->context->GSSetShader(found->second.shader.Get(),nullptr,0);
    renderer->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);renderer->context->SOSetTargets(1,&output,&offset);renderer->context->Draw(vertices,0);
    output=nullptr;renderer->context->SOSetTargets(1,&output,&offset);renderer->context->GSSetShader(previous.Get(),instances.data(),count);renderer->context->IASetPrimitiveTopology(topology);
    for(UINT i=0;i<count;++i)if(instances[i])instances[i]->Release();
    const bool captured=renderer->history->capture(renderer->current_key,renderer->stream_view.Get(),vertices);
    if(!captured)++snapshot.resource_failures;renderer->capture_current=false;return captured;
}catch(...){
    if(renderer) {const auto found=renderer->targets.find(renderer->current_color);if(found!=renderer->targets.end())++found->second.snapshot.resource_failures;}
    return 0;
}
extern "C" void bf_motion_renderer_complete(BfMotionRenderer *renderer,uint32_t frame) {
    if(renderer)renderer->history->complete_frame(frame);
}
extern "C" void bf_motion_renderer_surface_reset(BfMotionRenderer *renderer,uint32_t color) {
    if(renderer&&renderer->targets.erase(color))renderer->history->reset();
}
extern "C" void bf_motion_renderer_resource_failure(BfMotionRenderer *renderer,uint32_t color,uint32_t frame) {
    if(!renderer)return;auto found=renderer->targets.find(color);
    if(found!=renderer->targets.end()&&found->second.snapshot.source.frame==frame)++found->second.snapshot.resource_failures;
}
extern "C" int bf_motion_renderer_snapshot(BfMotionRenderer *renderer,uint32_t color,uint32_t frame,BfMotionSnapshot *out) {
    if(out)*out={};if(!renderer||!out)return 0;auto found=renderer->targets.find(color);
    if(found==renderer->targets.end()||found->second.snapshot.source.frame!=frame)return 0;
    *out=found->second.snapshot;
    const auto &source=out->source;
    const bool camera=source.view&&source.context&&std::isfinite(source.near_plane)&&std::isfinite(source.far_plane)&&
        source.near_plane>0&&source.far_plane>source.near_plane;
    out->complete=camera&&out->clear_observed&&out->draws&&!out->untracked_opaque&&!out->unsupported_projection&&!out->resource_failures;return 1;
}
