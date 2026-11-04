#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "gpu/scene_snapshot.h"
#include "gpu/scene_device.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
using Microsoft::WRL::ComPtr;
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void check(HRESULT h,const char *why){if(FAILED(h))throw std::runtime_error(why);}
struct Received {
    unsigned count{};BfSceneSnapshotInfo info{};
    ComPtr<ID3D11Texture2D> color,depth;
    ComPtr<ID3D11ComputeShader> expected_shader;
    ID3D11DeviceContext *context{};
};
void receive(const BfSceneSnapshot *frame,void *user){
    auto &r=*(Received*)user;ComPtr<ID3D11ComputeShader> shader;r.context->CSGetShader(&shader,nullptr,nullptr);
    require(shader.Get()==r.expected_shader.Get(),"Scene snapshot changed compute shader state");
    require(frame&&frame->color&&frame->device_depth&&frame->color_view&&frame->depth_view,"Incomplete real scene snapshot");
    r.color=frame->color;r.depth=frame->device_depth;r.info=frame->info;++r.count;
}
std::vector<unsigned char> read(ID3D11Device *d,ID3D11DeviceContext *c,ID3D11Texture2D *texture){
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;check(d->CreateTexture2D(&desc,nullptr,&staging),"staging");c->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"readback");std::vector<unsigned char> pixels(size_t(desc.Width)*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y)std::memcpy(pixels.data()+size_t(y)*desc.Width*4,(char*)mapped.pData+size_t(y)*mapped.RowPitch,desc.Width*4);c->Unmap(staging.Get(),0);return pixels;
}
}
struct NativeCopy {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback[2];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[2]{};
};
static HRESULT native_copy(ID3D12Device *device,ID3D12CommandQueue *,ID3D12GraphicsCommandList *commands,
    ID3D12Resource *const *resources,UINT count,void *user) {
    if(count!=2)return E_INVALIDARG;
    auto &out=*(NativeCopy*)user;
    for(UINT i=0;i<count;++i) {
        auto desc=resources[i]->GetDesc();UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&out.layout[i],nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
        buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        auto result=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out.readback[i]));
        if(FAILED(result))return result;
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={resources[i],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=resources[i];src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=out.readback[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=out.layout[i];
        commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commands->ResourceBarrier(1,&barrier);
    }
    return S_OK;
}
static std::vector<unsigned char> native_read(NativeCopy &copy,unsigned i) {
    void *mapped=nullptr;check(copy.readback[i]->Map(0,nullptr,&mapped),"native scene readback");
    std::vector<unsigned char> bytes(64*48*4);
    for(unsigned y=0;y<48;++y)std::memcpy(bytes.data()+y*64*4,(char*)mapped+copy.layout[i].Offset+y*copy.layout[i].Footprint.RowPitch,64*4);
    D3D12_RANGE empty{0,0};copy.readback[i]->Unmap(0,&empty);return bytes;
}
int main(int argc,char **argv)try{
    const bool d12=argc>1&&!std::strcmp(argv[1],"--d3d12");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    HRESULT created=d12?bf_scene_d3d12_create(0,&device,&context,&level):D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
    if(FAILED(created)){std::printf("SKIP: %s scene device unavailable\n",d12?"D3D12/11On12":"D3D11");return 77;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color,depth,mismatch;check(device->CreateTexture2D(&desc,nullptr,&color),"color");
    desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;check(device->CreateTexture2D(&desc,nullptr,&depth),"depth");
    desc.Width=32;check(device->CreateTexture2D(&desc,nullptr,&mismatch),"mismatched depth");
    ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;
    check(device->CreateRenderTargetView(color.Get(),nullptr,&rtv),"rtv");check(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"dsv");
    constexpr char shaders[]=R"GPU(
float4 vs(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),.25,1);}
float4 ps():SV_Target{return float4(1,0,0,1);}
[numthreads(1,1,1)]void cs(){}
)GPU";
    ComPtr<ID3DBlob> v,p,s,error;check(D3DCompile(shaders,sizeof shaders-1,"snapshot-test",nullptr,nullptr,"vs","vs_5_0",0,0,&v,&error),"vs compile");
    check(D3DCompile(shaders,sizeof shaders-1,"snapshot-test",nullptr,nullptr,"ps","ps_5_0",0,0,&p,&error),"ps compile");
    check(D3DCompile(shaders,sizeof shaders-1,"snapshot-test",nullptr,nullptr,"cs","cs_5_0",0,0,&s,&error),"cs compile");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11ComputeShader> cs;
    check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs),"vs");check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps),"ps");check(device->CreateComputeShader(s->GetBufferPointer(),s->GetBufferSize(),nullptr,&cs),"cs");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;raster.ScissorEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;check(device->CreateRasterizerState(&raster,&rs),"raster");
    D3D11_VIEWPORT viewport{0,0,64,48,0,1};D3D11_RECT scissor{32,0,64,48};auto *target=rtv.Get();
    context->OMSetRenderTargets(1,&target,dsv.Get());context->RSSetViewports(1,&viewport);context->RSSetScissorRects(1,&scissor);context->RSSetState(rs.Get());
    context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->CSSetShader(cs.Get(),nullptr,0);
    const float blue[4]={0,0,1,1},green[4]={0,1,0,1};context->ClearRenderTargetView(target,blue);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,.75f,0);context->Draw(3,0);
    Received received;received.expected_shader=cs;received.context=context.Get();bf_scene_snapshot_consumer(receive,&received);
    BfSceneSnapshotInfo info{91,0x81000000,0x81200000,1};
    require(!bf_scene_snapshot_publish(device.Get(),context.Get(),color.Get(),mismatch.Get(),&info),"Mismatched frame attachments accepted");
    require(bf_scene_snapshot_publish(device.Get(),context.Get(),color.Get(),depth.Get(),&info)==1,"Real scene snapshot rejected");
    require(received.count==1&&received.info.frame==91&&received.info.width==64&&received.info.height==48&&received.info.depth_va==info.depth_va,"Scene metadata lost source identity");
    // Subsequent UI/effect writes must not change the saved scene color/depth.
    context->ClearRenderTargetView(target,green);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
    const auto pixels=read(device.Get(),context.Get(),received.color.Get()),z=read(device.Get(),context.Get(),received.depth.Get());
    const size_t left=24*64+16,right=24*64+48;
    require(pixels[left*4]==255&&pixels[left*4+2]==0&&pixels[right*4]==0&&pixels[right*4+2]==255,"Pre-UI color was changed by later UI rendering");
    float left_z,right_z;std::memcpy(&left_z,z.data()+left*4,4);std::memcpy(&right_z,z.data()+right*4,4);
    require(std::abs(left_z-.75f)<.000001f&&std::abs(right_z-.25f)<.000001f,"Actual D24 device depth was not normalized correctly");
    const auto final=read(device.Get(),context.Get(),color.Get());require(final[left*4+1]==255&&final[right*4+1]==255,"Snapshot modified subsequent scene/UI rendering");
    if(d12) {
        NativeCopy copy;ID3D11Resource *inputs[]={received.color.Get(),received.depth.Get()};
        check(bf_scene_d3d12_access(context.Get(),inputs,2,native_copy,&copy),"fenced native scene access");
        require(native_read(copy,0)==pixels&&native_read(copy,1)==z,"Native DX12 did not receive the actual scene color/depth");
        require(read(device.Get(),context.Get(),received.color.Get())==pixels&&read(device.Get(),context.Get(),received.depth.Get())==z,"11On12 scene resources were not returned correctly");
        std::puts("PASS fenced scene interop: native D3D12 received matching actual color/depth; D3D11 resources usable after return");
    }
    bf_scene_snapshot_consumer(nullptr,nullptr);require(!bf_scene_snapshot_enabled(),"Snapshot remained enabled without a consumer/capture request");bf_scene_snapshot_shutdown();
    if(d12)check(bf_scene_d3d12_wait(context.Get()),"11On12 completion");
    std::printf("PASS %s: real pre-UI color, normalized device depth (.75/.25), source identity, mismatched-frame rejection, compute-state preservation, later UI exclusion\n",d12?"D3D12/11On12":"D3D11");
    if(d12)bf_scene_d3d12_shutdown();return 0;
}catch(const std::exception &e){bf_scene_snapshot_shutdown();std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
