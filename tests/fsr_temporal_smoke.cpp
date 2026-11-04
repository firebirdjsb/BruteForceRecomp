#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "graphics/fsr_temporal.h"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using namespace bf::graphics;
constexpr unsigned rw=320,rh=180,ow=640,oh=360;
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void check(HRESULT hr,const char *why){if(FAILED(hr)){char msg[200];std::snprintf(msg,sizeof msg,"%s: %08lX",why,(unsigned long)hr);throw std::runtime_error(msg);}}
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;uint64_t serial{};HANDLE event{};
    std::vector<ComPtr<ID3D12Resource>> uploads;
    Gpu(){
        ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i){
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))){
                std::wprintf(L"Adapter: %s (vendor=%04X device=%04X)\n",desc.Description,desc.VendorId,desc.DeviceId);break;
            }adapter.Reset();
        }
        require(device!=nullptr,"No hardware DX12 device");
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_6};
        check(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof model),"shader model");
        std::printf("Shader model: %x\n",unsigned(model.HighestShaderModel));
        D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"commands");check(commands->Close(),"close initial list");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");
    }
    ~Gpu(){if(event)CloseHandle(event);}
    void begin(){uploads.clear();check(allocator->Reset(),"reset allocator");check(commands->Reset(allocator.Get(),nullptr),"reset commands");}
    void submit(){
        check(commands->Close(),"close commands");ID3D12CommandList *lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);
        check(queue->Signal(fence.Get(),++serial),"signal");check(fence->SetEventOnCompletion(serial,event),"completion");
        require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"GPU timeout");check(device->GetDeviceRemovedReason(),"device removed");
    }
    ComPtr<ID3D12Resource> buffer(UINT64 bytes,D3D12_HEAP_TYPE type){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)),"buffer");return result;
    }
    ComPtr<ID3D12Resource> texture(unsigned w,unsigned h,DXGI_FORMAT format,bool output=false){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=format;if(output)desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> result;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,output?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&result)),"texture");return result;
    }
    void transition(ID3D12Resource *r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};commands->ResourceBarrier(1,&b);}
    void upload(ID3D12Resource *r,const void *pixels,unsigned row_bytes){
        const auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        auto memory=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);void *mapped{};D3D12_RANGE empty{0,0};check(memory->Map(0,&empty,&mapped),"map upload");
        for(unsigned y=0;y<desc.Height;++y)std::memcpy((char*)mapped+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch,(const char*)pixels+size_t(y)*row_bytes,row_bytes);memory->Unmap(0,nullptr);
        transition(r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=r;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=memory.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
        commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(r,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(memory);
    }
    struct Readback { ComPtr<ID3D12Resource> resource;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; };
    Readback copy(ID3D12Resource *r){
        auto desc=r->GetDesc();Readback result;UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&result.footprint,nullptr,nullptr,&bytes);result.resource=buffer(bytes,D3D12_HEAP_TYPE_READBACK);
        transition(r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=result.resource.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=result.footprint;
        commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);return result;
    }
    std::vector<uint32_t> read(const Readback &r){
        void *mapped{};check(r.resource->Map(0,nullptr,&mapped),"map readback");std::vector<uint32_t> pixels(size_t(ow)*oh);
        for(unsigned y=0;y<oh;++y)std::memcpy(pixels.data()+size_t(y)*ow,(char*)mapped+r.footprint.Offset+size_t(y)*r.footprint.Footprint.RowPitch,ow*4);
        D3D12_RANGE empty{0,0};r.resource->Unmap(0,&empty);return pixels;
    }
    void errors(){
        ComPtr<ID3D12InfoQueue> info;if(FAILED(device.As(&info)))return;
        unsigned failures=0;
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<char> bytes(size);auto *m=(D3D12_MESSAGE*)bytes.data();info->GetMessage(i,m,&size);
            if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"DX12: %s\n",m->pDescription);++failures;}}
        require(!failures,"D3D12 debug layer errors");
    }
};
double centroid(const std::vector<uint32_t>&pixels){
    double sum=0;unsigned count=0;for(unsigned y=60;y<240;++y)for(unsigned x=0;x<ow;++x){const auto p=pixels[size_t(y)*ow+x];if((p&255)>140&&((p>>8)&255)<100){sum+=x;++count;}}
    require(count>2000,"Upscaler/generated image lost moving foreground");return sum/count;
}
void dump(const std::filesystem::path &name,const std::vector<uint32_t> &pixels){
    auto *f=_wfopen(name.c_str(),L"wb");require(f!=nullptr,"Cannot write test capture");std::fprintf(f,"P6\n%u %u\n255\n",ow,oh);
    for(auto p:pixels){const unsigned char rgb[]={static_cast<unsigned char>(p),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p>>16)};std::fwrite(rgb,1,3,f);}std::fclose(f);
}
}
int main(int argc,char **argv)try{
    const bool bgra=argc>1&&!std::strcmp(argv[1],"--fsr4-bgra");
    const bool combined=argc>1&&!std::strcmp(argv[1],"--fsr4-framegen");
    const bool fg=combined||(argc>1&&!std::strcmp(argv[1],"--framegen"));const bool ml=bgra||combined||(argc>1&&!std::strcmp(argv[1],"--fsr4"));
    Gpu gpu;wchar_t exe[32768]{};require(GetModuleFileNameW(nullptr,exe,32768)>0,"Executable path unavailable");
    FsrTemporal fsr(gpu.device.Get(),gpu.queue.Get(),std::filesystem::path(exe).parent_path());
    FsrOptions options{rw,rh,ow,oh};options.nonlinear_color=true;options.frame_generation=fg;
    for(bool generation:{false,true}){
        if(generation&&!fg)continue;auto available=fsr.versions(generation);uint64_t selected=0;
        for(const auto &v:available){std::printf("%s provider %016llx: %s\n",generation?"FG":"SR",(unsigned long long)v.id,v.name.c_str());
            if(v.name.find(generation||!ml?"3.1.":"4.1.")!=std::string::npos)selected=v.id;}
        if(!selected){std::printf("SKIP: requested %s implementation unavailable on this hardware/driver\n",generation?"FSR 3 frame generation":ml?"FSR 4.1":"FSR 3.1");return 77;}
        (generation?options.frame_generation_version:options.upscale_version)=selected;
    }
    fsr.configure(options);std::printf("Selected SR: %s; FG: %s\n",fsr.upscale_name().c_str(),fsr.frame_generation_name().c_str());
    auto color=gpu.texture(rw,rh,bgra?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM),depth=gpu.texture(rw,rh,DXGI_FORMAT_R32_FLOAT),motion=gpu.texture(rw,rh,DXGI_FORMAT_R32G32_FLOAT);
    auto output=gpu.texture(ow,oh,DXGI_FORMAT_R8G8B8A8_UNORM,true),generated=gpu.texture(ow,oh,DXGI_FORMAT_R8G8B8A8_UNORM,true);
    TemporalSceneFrame frame;frame.color=color.Get();frame.depth=depth.Get();frame.motion=motion.Get();frame.scene=1;
    frame.elapsed_ms=1000.f/30;frame.near_plane=.1f;frame.far_plane=100;frame.vertical_fov=1.04719755f;
    gpu.begin();bool rejected=false;try{auto invalid=frame;invalid.sequence=1;invalid.motion=nullptr;fsr.record(gpu.commands.Get(),invalid,output.Get(),generated.Get());}catch(const std::exception&){rejected=true;}
    require(rejected,"Missing motion vectors were accepted");gpu.submit();
    std::vector<uint32_t> colors(size_t(rw)*rh);std::vector<float> depths(size_t(rw)*rh),motions(size_t(rw)*rh*2);
    std::vector<uint32_t> previous,current,intermediate;double previous_center=0;unsigned distinct=0;
    for(unsigned i=1;i<=24;++i){
        frame.sequence=i;frame.reset=i==1||i==18;frame.scene=i<18?1:2;fsr.jitter(i,frame.jitter_x,frame.jitter_y);
        // Analytically render two opaque planes. The red foreground translates
        // two render pixels/frame after warm-up. Motion is derived from that
        // exact translation, not estimated from the resulting color image.
        const float left=60+float(i>8?2*(i-8):0),dx=i>8?2.f:0.f;
        for(unsigned y=0;y<rh;++y)for(unsigned x=0;x<rw;++x){const size_t p=size_t(y)*rw+x;const float sx=x+.5f+frame.jitter_x,sy=y+.5f+frame.jitter_y;
            const bool object=sx>=left&&sx<left+48&&sy>=40&&sy<105;colors[p]=object?0xff1030e6u:(((unsigned(sx)/8+unsigned(sy)/8)&1)?0xff302010u:0xff201008u);
            if(bgra)colors[p]=(colors[p]&0xff00ff00u)|((colors[p]&255u)<<16)|((colors[p]>>16)&255u);
            const float z=object?3.f:10.f;depths[p]=100.f/99.9f-(100.f*.1f)/(99.9f*z);motions[p*2]=object?-dx:0;motions[p*2+1]=0;
        }
        gpu.begin();gpu.upload(color.Get(),colors.data(),rw*4);gpu.upload(depth.Get(),depths.data(),rw*4);gpu.upload(motion.Get(),motions.data(),rw*8);
        fsr.record(gpu.commands.Get(),frame,output.Get(),fg?generated.Get():nullptr);auto read=gpu.copy(output.Get());Gpu::Readback generated_read;if(fg)generated_read=gpu.copy(generated.Get());
        if(i==5){bool duplicate=false;try{fsr.record(gpu.commands.Get(),frame,output.Get(),fg?generated.Get():nullptr);}catch(const std::exception&){duplicate=true;}require(duplicate,"Repeated presentation was accepted as a source frame");}
        gpu.submit();current=gpu.read(read);const double center=centroid(current);
        require(std::abs(center-2*(left+24)+.5)<4,"Upscaled object is misplaced");
        if(fg){intermediate=gpu.read(generated_read);if(i>=11&&i!=18&&i!=19){const auto middle=centroid(intermediate);require(std::abs(middle-(center+previous_center)*.5)<1.25&&std::abs(middle-center)>.7&&std::abs(middle-previous_center)>.7,"Generated foreground is not between source frames");
                size_t differences=0,previous_differences=0;for(size_t p=0;p<current.size();++p){differences+=((intermediate[p]^current[p])&0xffffff)!=0;previous_differences+=((intermediate[p]^previous[p])&0xffffff)!=0;}require(differences>50&&previous_differences>50,"Frame generation repeated source RGB pixels");distinct+=1;
                std::printf("frame %u: previous center %.2f, generated %.2f, current %.2f; %zu changed pixels\n",i,previous_center,middle,center,differences);}}
        previous=current;previous_center=center;
    }
    require(!fg||distinct>=8,"Insufficient generated-frame evidence");gpu.errors();
    const auto tag=bgra?"fsr4-bgra":combined?"fsr4-framegen":fg?"fsr-framegen":ml?"fsr4":"fsr3";dump(std::filesystem::path(std::string(tag)+"-source.ppm"),current);if(fg)dump(std::filesystem::path(std::string(tag)+"-generated.ppm"),intermediate);
    std::printf("PASS: %s, 24 rendered frames, jitter, real analytic foreground motion/depth, source-repeat rejection and scene-cut reset. Gameplay integration is not exercised.\n",tag);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
