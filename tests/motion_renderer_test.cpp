#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "gpu/motion_renderer.h"
#include "gpu/scene_device.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT size=64;
void require(bool pass,const char *why){if(!pass)throw std::runtime_error(why);}
void check(HRESULT result,const char *why){require(SUCCEEDED(result),why);}
constexpr char shader_source[]=
GPU_MOTION_VERTEX_HLSL
"cbuffer Geometry:register(b0){float4 translation;};\n"
"struct Input{float4 p:POSITION;uint id:SV_VertexID;};\n"
"struct Output{float4 p:SV_Position;" GPU_MOTION_VARYINGS "};\n"
"Output vertex(Input i){Output o;float w=i.p.z;float2 pixel=i.p.xy+translation.xy;\n"
" float d=1.00000166893-.100000165403/w;\n"
" o.p=float4((pixel.x/32-1)*w,(1-pixel.y/32)*w,d*w,w);o.bf_capture_clip=o.p;\n"
" o.p=bf_motion_vertex(o.p,i.id,o.bf_previous_clip,o.bf_history_valid,o.bf_original_id);return o;}\n"
"[maxvertexcount(3)]void geometry(triangle Output v[3],inout TriangleStream<Output> stream){\n"
GPU_MOTION_TRIANGLE_VALIDITY
" stream.Append(v[0]);stream.Append(v[1]);stream.Append(v[2]);}\n"
GPU_MOTION_PIXEL_HLSL
"struct Pixel{float4 color:SV_Target0;float2 motion:SV_Target1;float reactive:SV_Target2;float depth:SV_Target4;float composition:SV_Target5;};\n"
"Pixel pixel(Output i){Pixel o;o.color=float4(.2,.4,.7,translation.z);\n"
" BFMotionPixel m=bf_motion_pixel(i.p,i.bf_previous_clip,i.bf_history_valid,o.color.a);\n"
" o.motion=m.motion;o.reactive=m.reactive;o.depth=m.canonical;o.composition=m.composition;return o;}\n";
struct GPU {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3DBlob> code;ComPtr<ID3D11Buffer> geometry,analytic;
    ComPtr<ID3D11Texture2D> original,reference,depth;
    ComPtr<ID3D11RenderTargetView> original_view,reference_view;ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11RasterizerState> raster;
    std::unique_ptr<BfMotionRenderer,decltype(&bf_motion_renderer_destroy)> renderer{nullptr,bf_motion_renderer_destroy};
    bool bridge;
    explicit GPU(bool native):bridge(native) {
        D3D_FEATURE_LEVEL level{};
        check(native?bf_scene_d3d12_create(0,&device,&context,&level):D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Create actual hardware fixture device");
        // The shared declaration is used once per stage; strip the duplicate
        // b2 declaration in this combined test source before compiling it.
        std::string source=shader_source;const std::string declaration=GPU_MOTION_CBUFFER;
        const auto second=source.find(declaration,source.find(declaration)+declaration.size());require(second!=std::string::npos,"Combined fixture constant declaration");source.erase(second,declaration.size());
        ComPtr<ID3DBlob> errors,pixel,geometry_code;
        auto compile=[&](const char *entry,const char *profile,ComPtr<ID3DBlob> &out){
            const auto result=D3DCompile(source.data(),source.size(),"motion-renderer-analytic",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&out,&errors);
            if(FAILED(result)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char *>(errors->GetBufferPointer()));check(result,"Compile actual GPU fixture");};
        compile("vertex","vs_5_0",code);compile("pixel","ps_5_0",pixel);compile("geometry","gs_5_0",geometry_code);
        check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs),"Create VS");
        check(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps),"Create PS");
        check(device->CreateGeometryShader(geometry_code->GetBufferPointer(),geometry_code->GetBufferSize(),nullptr,&gs),"Create history-validity GS");
        const D3D11_INPUT_ELEMENT_DESC input={"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        check(device->CreateInputLayout(&input,1,code->GetBufferPointer(),code->GetBufferSize(),&layout),"Create input layout");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&cb,nullptr,&geometry),"Create geometry constants");cb.ByteWidth=sizeof(BfMotionDrawConstants);
        check(device->CreateBuffer(&cb,nullptr,&analytic),"Create independently proven analytic projection constants");
        D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=size;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=1;
        texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&texture,nullptr,&original),"Create original RT0");check(device->CreateRenderTargetView(original.Get(),nullptr,&original_view),"Original view");
        check(device->CreateTexture2D(&texture,nullptr,&reference),"Create reference RT0");check(device->CreateRenderTargetView(reference.Get(),nullptr,&reference_view),"Reference view");
        texture.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;texture.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        check(device->CreateTexture2D(&texture,nullptr,&depth),"Create original D24");check(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"Create original DSV");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&rd,&raster),"Create raster state");renderer.reset(bf_motion_renderer_create(device.Get(),context.Get()));require(bool(renderer),"Create real motion history renderer");
    }
    ~GPU(){if(context)context->ClearState();renderer.reset();if(bridge&&context)bf_scene_d3d12_wait(context.Get());context.Reset();device.Reset();if(bridge)bf_scene_d3d12_shutdown();}
    std::vector<uint8_t> read(ID3D11Texture2D *source,unsigned bytes) {
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"Create readback");context->CopyResource(staging.Get(),source);
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map real GPU readback");std::vector<uint8_t> result(size*size*bytes);
        for(UINT y=0;y<size;++y)std::memcpy(result.data()+size_t(y)*size*bytes,static_cast<uint8_t *>(mapped.pData)+size_t(y)*mapped.RowPitch,size*bytes);
        context->Unmap(staging.Get(),0);return result;
    }
    void clear(const BfMotionSource &source) {
        context->OMSetRenderTargets(0,nullptr,nullptr);const float black[4]={0,0,0,1};context->ClearRenderTargetView(original_view.Get(),black);
        context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);bf_motion_renderer_clear(renderer.get(),&source,1);
    }
    void draw(const BfMotionSource &source,Nv2aRenderState state,const Nv2aXVsBatch *batch,const BFMotionRecord *identity,
              const std::array<std::array<float,4>,4> &points,float shift,bool reference_only=false) {
        BfMotionDrawBinding binding{};
        if(!reference_only)require(bf_motion_renderer_prepare(renderer.get(),&source,&state,batch,identity,&binding),"Prepare real semantic vertex history");
        // This public analytic shader is not a retail audited program, so the
        // production classifier MUST reject it. Its known view-Z fixture gives
        // an independent GPU check of canonical depth, without enabling a game.
        BfMotionDrawConstants constants{};constants.width=constants.height=float(size);constants.depth_a=250/249.9f;constants.depth_b=-.1f*constants.depth_a;
        if(!reference_only) {
            constants.flags=BF_MOTION_DRAW_ENABLED|BF_MOTION_DRAW_CANONICAL;
            if(binding.tracked)constants.flags|=BF_MOTION_DRAW_TRACKED;
            if(binding.previous_valid)constants.flags|=BF_MOTION_DRAW_PREVIOUS;
            if(state.blend_enable)constants.flags|=BF_MOTION_DRAW_TRANSPARENT;
            constants.original_count=4;constants.previous_count=4;constants.expected_frame=binding.previous_valid?source.frame-1:0;
        }
        if(source.apply_jitter) {
            constants.flags|=BF_MOTION_DRAW_ENABLED|BF_MOTION_DRAW_JITTER;
            constants.jitter_x=source.jitter_x;constants.jitter_y=source.jitter_y;
        }
        context->UpdateSubresource(analytic.Get(),0,nullptr,&constants,0,0);const float transform[4]={shift,0,state.blend_enable?.5f:1.f,0};
        context->UpdateSubresource(geometry.Get(),0,nullptr,transform,0,0);
        ID3D11Buffer *cb0=geometry.Get(),*cb2=analytic.Get();context->VSSetConstantBuffers(0,1,&cb0);context->PSSetConstantBuffers(0,1,&cb0);
        context->VSSetConstantBuffers(2,1,&cb2);context->PSSetConstantBuffers(2,1,&cb2);
        ID3D11ShaderResourceView *history[]={binding.original_ids,binding.previous};context->VSSetShaderResources(1,2,history);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof points;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=points.data();ComPtr<ID3D11Buffer> vertices;check(device->CreateBuffer(&desc,&data,&vertices),"Create consumed geometry inputs");
        constexpr uint32_t triangle_indices[]={0,1,2,0,2,3};desc.ByteWidth=sizeof triangle_indices;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;data.pSysMem=triangle_indices;
        ComPtr<ID3D11Buffer> indices;check(device->CreateBuffer(&desc,&data,&indices),"Create original topology");
        auto *vb=vertices.Get();UINT stride=16,offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R32_UINT,0);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(gs.Get(),nullptr,0);
        ID3D11RenderTargetView *targets[6]={reference_only?reference_view.Get():original_view.Get(),nullptr,nullptr,nullptr,nullptr,nullptr};
        if(!reference_only)bf_motion_renderer_targets(renderer.get(),source.color_va,source.frame,&targets[1],&targets[2],&targets[4],&targets[5]);
        if(state.blend_enable)targets[1]=targets[4]=nullptr;
        context->OMSetRenderTargets(reference_only?1:6,targets,dsv.Get());
        D3D11_BLEND_DESC blend{};blend.IndependentBlendEnable=TRUE;
        for(auto &target:blend.RenderTarget){target.SrcBlend=target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlend=target.DestBlendAlpha=D3D11_BLEND_ZERO;target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;}
        if(state.blend_enable){blend.RenderTarget[0].BlendEnable=TRUE;blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;}
        for(unsigned index:{2u,5u}){blend.RenderTarget[index].BlendEnable=TRUE;blend.RenderTarget[index].DestBlend=D3D11_BLEND_ONE;blend.RenderTarget[index].BlendOp=D3D11_BLEND_OP_MAX;blend.RenderTarget[index].RenderTargetWriteMask=1;}
        ComPtr<ID3D11BlendState> bs;check(device->CreateBlendState(&blend,&bs),"Create independent original/auxiliary blend");context->OMSetBlendState(bs.Get(),nullptr,UINT_MAX);
        D3D11_DEPTH_STENCIL_DESC depth_state{};depth_state.DepthEnable=TRUE;depth_state.DepthWriteMask=state.depth_write?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;depth_state.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        ComPtr<ID3D11DepthStencilState> ds;check(device->CreateDepthStencilState(&depth_state,&ds),"Create actual depth-test/write policy");context->OMSetDepthStencilState(ds.Get(),0);
        D3D11_VIEWPORT viewport{0,0,float(size),float(size),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());context->DrawIndexed(6,0,0);
        if(!reference_only&&binding.tracked)require(bf_motion_renderer_capture(renderer.get(),code->GetBufferPointer(),code->GetBufferSize(),4),"Capture actual VS outputs without CPU readback");
        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
float scalar(const std::vector<uint8_t> &pixels,unsigned at){float value;std::memcpy(&value,pixels.data()+at*4,4);return value;}
}
int main(int argc,char **argv)try {
    GPU gpu(argc>1&&std::strcmp(argv[1],"--d3d12")==0);
    const uint32_t ids[]={100,7,91,35},indices[]={0,1,2,0,2,3},program[][4]={{0,0x0020001b,0x08000000,0x0000f801}};
    Nv2aXVsBatch batch{};batch.vertex_ids=ids;batch.nverts=4;batch.indices=indices;batch.nindices=6;batch.program=program;batch.ninstr=1;
    BFMotionRecord identity{};auto *w=identity.words;w[BF_MO_KIND]=BF_MOTION_PART_BEGIN;w[BF_MO_VIEW_VALID]=1;w[BF_MO_OBJECT]=0x10000;w[BF_MO_GENERATION]=1;w[BF_MO_SLOT]=7;
    w[BF_MO_VIEW]=0x20000;w[BF_MO_CONTEXT]=0x30000;w[BF_MO_NODE]=0x40000;w[BF_MO_PART]=0x50000;w[BF_MO_SUBDRAW]=0x60000;
    Nv2aRenderState state{};state.color_mask=0x01010101;state.depth_test_enable=state.depth_write=1;
    BfMotionSource source{1,0x80001000,0x80005000,size,size,w[BF_MO_VIEW],w[BF_MO_CONTEXT],.1f,250,0,0,0};
    std::array<std::array<float,4>,4> points={std::array<float,4>{16,16,2,1},{48,16,2,1},{48,48,2,1},{16,48,2,1}};
    constexpr unsigned sample=32*size+40;
    for(uint32_t frame=1;frame<=3;++frame) {
        source.frame=frame;source.apply_jitter=frame>1;
        source.jitter_x=frame==2?.25f:-.375f;source.jitter_y=frame==2?-.125f:.25f;
        gpu.clear(source);
        auto current=points;float shift=frame==2?8.f:0.f;
        if(frame==3){current[0][0]+=16;current[1][0]+=16;current[2][0]+=24;current[3][0]+=24;}
        gpu.draw(source,state,&batch,&identity,current,shift);
        BfMotionSnapshot snapshot{};require(bf_motion_renderer_snapshot(gpu.renderer.get(),source.color_va,frame,&snapshot),"Snapshot actual GPU attachments");
        require(!snapshot.complete&&snapshot.unsupported_projection,"Synthetic fixture must never qualify as a proved retail projection");
        const auto vectors=gpu.read(snapshot.motion,8),canonical=gpu.read(snapshot.canonical_depth,4),reactive=gpu.read(snapshot.reactive,1);
        const float expected=frame==1?0:frame==2?-8:-12.1875f;
        require(std::abs(scalar(vectors,sample*2)-expected)<.03f&&std::abs(scalar(vectors,sample*2+1))<1e-4f,"Actual previous/current GPU vertices must produce translated/deformed pixel vectors");
        require((frame==1?reactive[sample]==255:reactive[sample]==0),"New semantic history is explicitly reactive, previous valid geometry is not fabricated zero");
        const float depth=250/249.9f-(.1f*250/249.9f)/2;
        std::printf("frame %u motion %.6f canonical %.9f expected %.9f reactive %u\n",frame,scalar(vectors,sample*2),scalar(canonical,sample),depth,reactive[sample]);
        require(std::abs(scalar(canonical,sample)-depth)<2e-6f,"Canonical MRT must use primary near/far and actual reciprocal view W");
        // Transparent material contributes actual alpha coverage and preserves
        // the opaque motion/depth already accepted by the original DSV.
        if(frame==2) {
            auto transparent=state;transparent.blend_enable=1;transparent.blend_src=0x302;transparent.blend_dst=0x303;transparent.blend_eq=0x8006;transparent.depth_write=0;
            gpu.draw(source,transparent,nullptr,nullptr,current,shift);
            require(gpu.read(snapshot.motion,8)==vectors&&gpu.read(snapshot.canonical_depth,4)==canonical,"Transparent effects must preserve underlying real motion/depth");
            const auto mask=gpu.read(snapshot.reactive,1);require(mask[sample]>=127&&mask[sample]<=129,"Actual fragment alpha must produce reactive/composition coverage");
        }
        const auto original=gpu.read(gpu.original.Get(),4);const float black[4]={0,0,0,1};gpu.context->ClearRenderTargetView(gpu.reference_view.Get(),black);gpu.context->ClearDepthStencilView(gpu.dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
        gpu.draw(source,state,nullptr,nullptr,current,shift,true);
        if(frame==2){auto transparent=state;transparent.blend_enable=1;transparent.depth_write=0;gpu.draw(source,transparent,nullptr,nullptr,current,shift,true);}
        require(original==gpu.read(gpu.reference.Get(),4),"Motion/history/canonical capture must preserve original RT0 bytes exactly");
        bf_motion_renderer_complete(gpu.renderer.get(),frame);
    }
    source.apply_jitter=0;source.frame=4;gpu.clear(source);gpu.draw(source,state,nullptr,nullptr,points,0);
    BfMotionSnapshot missing{};require(bf_motion_renderer_snapshot(gpu.renderer.get(),source.color_va,4,&missing)&&missing.untracked_opaque&&!missing.complete,"Unknown opaque owner must reject the whole source");
    const auto invalid=gpu.read(missing.motion,8);require(std::isnan(scalar(invalid,(32*size+32)*2)),"Unknown opaque geometry must not receive invented valid zero motion");
    source.frame=5;gpu.clear(source);auto forged=identity;
    forged.words[BF_MO_SLOT]=UINT32_MAX;forged.words[BF_MO_DRAW_TARGET]=0x001F5600u;
    forged.words[BF_MO_SUBDRAW]=forged.words[BF_MO_PART];
    gpu.draw(source,state,&batch,&forged,points,0);
    require(bf_motion_renderer_snapshot(gpu.renderer.get(),source.color_va,5,&missing)&&
        missing.untracked_opaque&&!missing.tracked_draws&&!missing.complete,
        "Retail static adapter withdrawal must survive a forged positive generation");
    // At the shared diagonal, the bad vertex has exactly zero interpolation
    // weight. Per-pixel thresholding alone would falsely bless that history.
    bf_motion_renderer_complete(gpu.renderer.get(),5);
    source.frame=6;gpu.clear(source);auto behind=points;behind[1][2]=-2;
    gpu.draw(source,state,&batch,&identity,behind,0);bf_motion_renderer_complete(gpu.renderer.get(),6);
    source.frame=7;gpu.clear(source);gpu.draw(source,state,&batch,&identity,points,0);
    require(bf_motion_renderer_snapshot(gpu.renderer.get(),source.color_va,7,&missing),"Read mixed-validity triangles");
    const auto mixed=gpu.read(missing.reactive,1),mixed_vectors=gpu.read(missing.motion,8);
    require(mixed[32*size+32]==255&&mixed[32*size+40]==255&&mixed[32*size+24]==0,
        "Any invalid previous vertex must invalidate its whole triangle, including zero-weight edge pixels");
    require(scalar(mixed_vectors,(32*size+32)*2)==0&&scalar(mixed_vectors,(32*size+32)*2+1)==0,
        "Known invalid per-vertex history must carry finite zero motion plus explicit reactivity");
    std::puts("PASS GPU post-VS history scatter/lookup, exact actor IDs, new-history reset, rigid/deformed motion, jitter, triangle validity, alpha masks, canonical depth and unchanged RT0; unproved world remains incomplete");return 0;
}catch(const std::exception &error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
