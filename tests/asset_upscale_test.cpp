#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "gpu/asset_upscale.h"
#include "gpu/scene_device.h"
#include <wrl/client.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static uint32_t color(unsigned face,unsigned mip,unsigned x,unsigned y){return ((40+face*20+mip*10)<<24)|((30+y*11)<<16)|((20+x*13)<<8)|(10+face*7+mip*5+x*4);}
static uint32_t swap_rb(uint32_t v){return(v&0xFF00FF00u)|((v&255)<<16)|((v>>16)&255);}
int main(int argc,char **argv){
    bool bridge=false,hardware=false;for(int i=1;i<argc;i++){bridge|=!std::strcmp(argv[i],"--d3d12");hardware|=!std::strcmp(argv[i],"--hardware");}
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    HRESULT hr=bridge?bf_scene_d3d12_create(!hardware,&device,&context,&level):D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
    if(FAILED(hr)){std::printf("SKIP unavailable test device %08lX\n",static_cast<unsigned long>(hr));return 77;}
    unsigned cases=0;
    for(unsigned factor:{2u,4u})for(bool bgra:{false,true})for(unsigned faces:{1u,6u}){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=8;desc.Height=4;desc.MipLevels=4;desc.ArraySize=faces;
        desc.Format=bgra?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(faces==6)desc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        // Cube resources must be square; non-square images are tested separately.
        if(faces==6)desc.Height=8;
        std::vector<std::vector<uint32_t>> pixels(faces*4);std::vector<D3D11_SUBRESOURCE_DATA> data(faces*4);
        for(unsigned f=0;f<faces;f++)for(unsigned m=0;m<4;m++){
            const unsigned w=std::max(1u,desc.Width>>m),h=std::max(1u,desc.Height>>m),index=f*4+m;
            pixels[index].resize(w*h);for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)pixels[index][y*w+x]=color(f,m,x,y);
            data[index]={pixels[index].data(),w*4,0};
        }
        ComPtr<ID3D11Texture2D> original,upscaled,readback;ComPtr<ID3D11ShaderResourceView> view;
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,data.data(),&original)));
        CHECK(bf_asset_upscale(device.Get(),context.Get(),original.Get(),factor,&upscaled,&view));
        D3D11_TEXTURE2D_DESC output{};upscaled->GetDesc(&output);const unsigned extra=factor==4?2:1;
        CHECK(output.Width==desc.Width*factor&&output.Height==desc.Height*factor&&output.ArraySize==faces&&output.MipLevels==4+extra);
        output.Usage=D3D11_USAGE_STAGING;output.BindFlags=0;output.CPUAccessFlags=D3D11_CPU_ACCESS_READ;output.MiscFlags=0;
        CHECK(SUCCEEDED(device->CreateTexture2D(&output,nullptr,&readback)));context->CopyResource(readback.Get(),upscaled.Get());
        if(bridge)CHECK(SUCCEEDED(bf_scene_d3d12_wait(context.Get())));
        for(unsigned f=0;f<faces;f++)for(unsigned m=0;m<4;m++){
            D3D11_MAPPED_SUBRESOURCE map{};const unsigned index=D3D11CalcSubresource(m+extra,f,output.MipLevels);
            CHECK(SUCCEEDED(context->Map(readback.Get(),index,D3D11_MAP_READ,0,&map)));
            for(unsigned y=0;y<std::max(1u,desc.Height>>m);y++)for(unsigned x=0;x<std::max(1u,desc.Width>>m);x++){
                uint32_t actual;std::memcpy(&actual,static_cast<const unsigned char*>(map.pData)+y*map.RowPitch+x*4,4);
                CHECK(actual==(bgra?swap_rb(color(f,m,x,y)):color(f,m,x,y)));
            }
            context->Unmap(readback.Get(),index);
        }
        D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)));
        const auto *row=static_cast<const uint32_t*>(map.pData);CHECK(row[0]!=row[output.Width-1]);
        CHECK((row[output.Width/2]>>24)==40);context->Unmap(readback.Get(),0);cases++;
    }
    context->ClearState();context->Flush();if(bridge)CHECK(SUCCEEDED(bf_scene_d3d12_wait(context.Get())));
    context.Reset();device.Reset();if(bridge)bf_scene_d3d12_shutdown();
    std::printf("PASS %u asset cases: 2x/4x, RGBA/BGRA, non-square/cube, exact original mips, color/alpha and GPU readback.\n",cases);return 0;
}
