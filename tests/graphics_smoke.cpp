#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_presenter.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
void pump() { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
void resize(HWND window, int width, int height) {
    RECT rect{0, 0, width, height}; AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    require(SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER) != 0, "Cannot resize verification window"); pump();
}
std::vector<uint32_t> read_frame(const std::vector<uint32_t> &input, UINT &width, UINT &height) {
    bf::graphics::dx12_frame(input.data());
    require(bf::graphics::dx12_active(), "Presenter stopped during frame");
    std::vector<uint32_t> result;
    require(bf::graphics::dx12_readback(result, width, height), "GPU readback failed");
    return result;
}
}
int main() {
    _putenv_s("BF_GRAPHICS_READBACK", "1");
    _putenv_s("BF_DX12_DEBUG", "1");
    _putenv_s("RECOMP_PRESENT_VSYNC", "0");
    WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"BruteForceGraphicsVerification";
    if (!RegisterClassW(&type)) return 1;
    HWND window = CreateWindowW(type.lpszClassName, L"Brute Force - native graphics verification",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 760, nullptr, nullptr, type.hInstance, nullptr);
    if (!window) return 1;
    ShowWindow(window, SW_SHOWNORMAL); pump();
    int status = 0;
    try {
        std::vector<uint32_t> source(320 * 240, 0xffc04020u);
        std::vector<uint32_t> bilinear;
        require(!bf::graphics::dx12_init(window, 0, 240, true) && !bf::graphics::dx12_active(), "Invalid input was accepted");
        for (const bool fsr : {false, true}) {
            resize(window, 1000, 720);
            require(bf::graphics::dx12_init(window, 320, 240, fsr), "Hardware DX12 initialization failed");
            std::fill(source.begin(), source.end(), 0xffc04020u);
            UINT width{}, height{};
            const auto flat = read_frame(source, width, height);
            const auto first_stats=bf::graphics::dx12_stats();
            for(unsigned repeat=0;repeat<3;++repeat)
                require(read_frame(source,width,height)==flat,"Repeated source pixels changed");
            const auto repeat_stats=bf::graphics::dx12_stats();
            require(repeat_stats.uploads==first_stats.uploads&&repeat_stats.spatial_passes==first_stats.spatial_passes,
                "Repeated source reuploaded or reran spatial scaling");
            // Source IDs can repeat while the F1 compositor changes a single
            // pixel. Exact content validation must refresh that frame too.
            source[120*320+160]^=0x00ffffff;
            bf::graphics::dx12_frame_timed(source.data(),{7,1000000,1000000,0});
            const auto edited_stats=bf::graphics::dx12_stats();
            source[120*320+160]^=0x00ffffff;
            bf::graphics::dx12_frame_timed(source.data(),{7,1000000,2000000,0});
            const auto restored=read_frame(source,width,height);
            require(restored==flat&&edited_stats.uploads==repeat_stats.uploads+1&&
                bf::graphics::dx12_stats().uploads==edited_stats.uploads+1,"Same-ID UI edit was cached incorrectly");
            require(width == 1000 && height == 720, "Incorrect client dimensions");
            const auto center = flat[size_t(height / 2) * width + width / 2];
            for (unsigned shift : {0u, 8u, 16u, 24u}) {
                const int actual = (center >> shift) & 255, expected = (0xff2040c0u >> shift) & 255;
                require(std::abs(actual - expected) <= 2, "Color/channel or alpha mismatch");
            }
            require(flat[size_t(height / 2) * width] == 0xff000000u, "Letterbox is not black");
            for (UINT y = 0; y < 240; ++y) for (UINT x = 0; x < 320; ++x) {
                const UINT value = ((x / 3 + y / 5) & 1) ? 208 : 38;
                source[size_t(y) * 320 + x] = 0xff000000u | (value << 16) | ((x * 255 / 319) << 8) | (y * 255 / 239);
            }
            const auto pattern = read_frame(source, width, height);
            require(pattern != flat, "Rendered output did not respond to source changes");
            if (!fsr) bilinear = pattern;
            else {
                size_t changed = 0;
                for (size_t i = 0; i < pattern.size(); ++i) changed += pattern[i] != bilinear[i];
                require(changed > 10000, "FSR shader output matches bilinear; effect was not exercised");
                std::printf("FSR vs bilinear: %zu distinct output pixels\n", changed);
            }
            resize(window, 913, 641);
            const auto resized = read_frame(source, width, height);
            require(width == 913 && height == 641 && resized.size() == size_t(width) * height, "Resize/readback failed");
            if(fsr) {
                std::fill(source.begin(),source.end(),0xffc04020u);
                for(unsigned factor:{2u,4u,8u,0u}) {
                    require(bf::graphics::dx12_scale(factor),"Live FSR scale change failed");
                    const auto image=read_frame(source,width,height);
                    const auto color=image[size_t(height/2)*width+width/2];
                    for(unsigned shift:{0u,8u,16u,24u})
                        require(std::abs(int((color>>shift)&255)-int((0xff2040c0u>>shift)&255))<=2,"FSR scale changed source color");
                    require(image[size_t(height/2)*width]==0xff000000u,"FSR scale changed letterboxing");
                }
                require(!bf::graphics::dx12_scale(3),"Invalid FSR scale accepted");
            }
            // Smaller than the source: FSR must fall back to bilinear without stale UAVs.
            resize(window, 231, 173);
            read_frame(source, width, height);
            require(width == 231 && height == 173, "Downscale fallback failed");
            bf::graphics::dx12_shutdown();
            require(!bf::graphics::dx12_active(), "Presenter remained active after shutdown");
            std::printf("PASS %s: GPU color, alpha, letterbox, changing input, resize, downscale, shutdown\n", fsr ? "DX12 + FSR1" : "DX12 bilinear");
        }
    } catch (const std::exception &error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); status = 1; }
    bf::graphics::dx12_shutdown(); DestroyWindow(window); UnregisterClassW(type.lpszClassName, type.hInstance);
    return status;
}
