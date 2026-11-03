#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_presenter.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace {
constexpr unsigned w=320,h=240;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
uint32_t pattern(int x,int y) {
    uint32_t value=uint32_t(x+1000)*73856093u^uint32_t(y+1000)*19349663u;value^=value>>13;value*=1274126177u;
    return 0xff000000u|(value&0xffffff);
}
uint32_t rgba(uint32_t bgra){return (bgra&0xff00ff00u)|((bgra&255)<<16)|((bgra>>16)&255);}
std::vector<uint32_t> scene(int shift) {
    std::vector<uint32_t> result(w*h);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
        result[y*w+x]=(x<40&&y<20)?0xffe0a020u:pattern(int(x)-shift,y);return result;
}
std::vector<uint32_t> show(const std::vector<uint32_t> &image,bf::graphics::FrameStamp stamp) {
    MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    bf::graphics::dx12_frame_timed(image.data(),stamp);
    require(bf::graphics::dx12_active(),"Timed presenter stopped");std::vector<uint32_t> result;unsigned width{},height{};
    require(bf::graphics::dx12_readback(result,width,height)&&width==w&&height==h,"Interpolation readback dimensions");return result;
}
}
int main() {
    _putenv_s("BF_GRAPHICS_READBACK","1");_putenv_s("BF_DX12_DEBUG","1");_putenv_s("RECOMP_PRESENT_VSYNC","0");
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"BruteForceInterpolationTest";
    RegisterClassW(&type);RECT rect{0,0,w,h};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"Brute Force interpolation verification",WS_OVERLAPPEDWINDOW,0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,type.hInstance,nullptr);
    if(!window)return 1;ShowWindow(window,SW_SHOW);
    int status=0;
    try {
        require(bf::graphics::dx12_init(window,w,h,false),"DX12 init");const auto old=scene(0),current=scene(8);const uint64_t start=1000000000,step=33333332;
        show(old,{1,start,start,120});show(current,{2,start+step,start+step,120});
        for(unsigned fraction=1;fraction<4;++fraction) {
            const auto image=show(current,{2,start+step,start+step+step*fraction/4+1,120});double error=0,unwarped=0;unsigned samples=0;
            for(unsigned y=48;y<h-48;++y)for(unsigned x=48;x<w-48;++x) {
                const auto expected=rgba(pattern(int(x)-int(2*fraction),y)),actual=image[y*w+x],endpoint=rgba(current[y*w+x]);
                for(unsigned shift:{0u,8u,16u}){error+=std::abs(int((actual>>shift)&255)-int((expected>>shift)&255));unwarped+=std::abs(int((endpoint>>shift)&255)-int((expected>>shift)&255));samples++;}
            }
            std::printf("phase %.2f: motion-warp error %.3f, unwarped error %.3f\n",fraction/4.0,error/samples,unwarped/samples);
            require(error/samples<5&&error<unwarped*.1,"Interpolation did not reconstruct translated pixels");
            require(image[5*w+5]==rgba(current[5*w+5]),"Static HUD pixel changed");
        }
        const auto stalled=show(current,{2,start+step,start+step*8,120});require(stalled[100*w+100]==rgba(current[100*w+100]),"Stalled history was reused");
        std::vector<uint32_t> cut(w*h,0xffc02010);const auto reset=show(cut,{4,start+step*9,start+step*9,120});require(reset[100*w+100]==rgba(cut[0]),"Skipped frame did not reset");
        const auto off=show(current,{5,start+step*10,start+step*10,0});require(off[100*w+100]==rgba(current[100*w+100]),"Off path changed source");
        std::puts("PASS: real GPU-generated quarter/half/three-quarter frames, static HUD, reset, stall and off paths.");
    }catch(const std::exception &error){std::fprintf(stderr,"FAIL: %s\n",error.what());status=1;}
    bf::graphics::dx12_shutdown();DestroyWindow(window);return status;
}
