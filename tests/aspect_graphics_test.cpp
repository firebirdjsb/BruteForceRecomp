#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_presenter.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void resize(HWND window,unsigned width,unsigned height){
    // An unframed popup can exceed the monitor without the OS clamping the
    // client height to the work area, so 1440p/2160p readbacks stay exact.
    RECT r{0,0,LONG(width),LONG(height)};AdjustWindowRect(&r,WS_POPUP,FALSE);
    require(SetWindowPos(window,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER)!=0,"Resize failed");
}
std::vector<uint32_t> frame(const std::vector<uint32_t> &source,unsigned w,unsigned h){
    // Return value reports vsync pacing, so it is false for this unpaced test.
    bf::graphics::dx12_frame(source.data());require(bf::graphics::dx12_active(),"GPU presenter stopped");
    std::vector<uint32_t> image;uint32_t actual_w{},actual_h{};
    require(bf::graphics::dx12_readback(image,actual_w,actual_h),"GPU readback failed");
    require(actual_w==w&&actual_h==h,"Wrong readback dimensions");return image;
}
}
int main(){
    _putenv_s("BF_GRAPHICS_READBACK","1");_putenv_s("BF_DX12_DEBUG","1");_putenv_s("RECOMP_PRESENT_VSYNC","0");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BFNativeAspectTest";
    if(!RegisterClassW(&wc))return 1;
    HWND window=CreateWindowW(wc.lpszClassName,L"Native aspect GPU verification",WS_POPUP,0,0,800,600,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 2;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    int result{};
    try {
        std::vector<uint32_t> source(640*480,0xffc04020u);
        for(bool fsr:{false,true}) {
            resize(window,1280,720);require(bf::graphics::dx12_init(window,640,480,fsr),"Hardware DX12 init failed");
            for(unsigned h:{720u,1080u,1440u,2160u}) {
                const unsigned w=h*16/9;resize(window,w,h);
                require(bf::graphics::dx12_aspect(false),"4:3 switch failed");
                const auto original=frame(source,w,h);
                require(original[size_t(h/2)*w]==0xff000000u,"Original 4:3 bars missing");
                require(bf::graphics::dx12_aspect(true),"16:9 switch failed");
                const auto wide=frame(source,w,h);
                const auto center=wide[size_t(h/2)*w+w/2];
                for(unsigned x:{0u,w/8,w/2,w-1}) {
                    const auto color=wide[size_t(h/2)*w+x];
                    for(unsigned shift:{0u,8u,16u,24u})require(std::abs(int((color>>shift)&255)-int((center>>shift)&255))<=2,"16:9 failed to fill output");
                }
                require(bf::graphics::dx12_aspect(false),"4:3 restoration failed");
                require(frame(source,w,h)==original,"Aspect toggle failed exact 4:3 restoration");
                std::printf("PASS %s %ux%u: wide output and exact 4:3 restoration\n",fsr?"FSR1":"bilinear",w,h);
            }
            // Exercise the same nonuniform EASU transform on actual detail,
            // proving the wide FSR path differs from bilinear on GPU readback.
            resize(window,1280,720);require(bf::graphics::dx12_aspect(true),"Pattern aspect failed");
            for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x){const unsigned v=((x/3+y/5)&1)?208:38;source[y*640+x]=0xff000000u|(v<<16)|((x*255/639)<<8)|(y*255/479);}
            const auto pattern=frame(source,1280,720);
            if(fsr){
                require(bf::graphics::dx12_stats().spatial_passes>0,"FSR spatial pass did not execute");
                bf::graphics::dx12_shutdown();require(bf::graphics::dx12_init(window,640,480,false)&&bf::graphics::dx12_aspect(true),"Bilinear reference init failed");
                const auto reference=frame(source,1280,720);size_t different{};
                for(size_t i=0;i<pattern.size();++i)different+=pattern[i]!=reference[i];
                require(different>10000,"Wide FSR output matches bilinear");std::printf("PASS wide FSR shader detail: %zu pixels differ\n",different);
            }
            std::fill(source.begin(),source.end(),0xffc04020u);bf::graphics::dx12_shutdown();
        }
    }catch(const std::exception &e){std::fprintf(stderr,"FAIL %s\n",e.what());result=1;}
    bf::graphics::dx12_shutdown();DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return result;
}
