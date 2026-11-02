#include "dx12_presenter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../ui/host_ui.h"

extern "C" {
int bf_d3d11_enabled(void);
int bf_d3d11_init(HWND, uint32_t, uint32_t);
int bf_d3d11_frame(const uint32_t *);
int bf_d3d11_active(void);
void bf_d3d11_shutdown(void);
}
namespace {
enum class Backend { none, dx12, dx11 };
Backend backend{};
HWND host_window{};uint32_t host_width{},host_height{};bool live_fsr{};
const char *choice() {
    const auto *value = std::getenv("BF_RENDER_BACKEND");
    return value && *value ? value : "auto";
}
}
extern "C" int gpu_present_enabled(void) { return std::strcmp(choice(), "gdi") != 0; }
extern "C" int gpu_present_init(HWND window, uint32_t width, uint32_t height) {
    host_window=window;host_width=width;host_height=height;
    const auto *requested = choice();
    if (std::strcmp(requested, "auto") && std::strcmp(requested, "dx12") && std::strcmp(requested, "dx11") && std::strcmp(requested, "gdi")) {
        std::fprintf(stderr, "[GRAPHICS] Unknown backend '%s'; using automatic selection\n", requested);
        requested = "auto";
    }
    const auto *upscaler = std::getenv("BF_UPSCALER");
    const bool fsr = upscaler && !std::strcmp(upscaler, "fsr1");
    live_fsr=fsr;bf_ui_fsr_initial(fsr);bf_ui_fsr_supported(0);
    if (upscaler && *upscaler && std::strcmp(upscaler, "off") && !fsr)
        std::fprintf(stderr, "[GRAPHICS] Upscaler '%s' is unavailable in this renderer; using bilinear\n", upscaler);
    if (!std::strcmp(requested, "gdi")) return 0;
    if (std::strcmp(requested, "dx11") && bf::graphics::dx12_init(window, width, height, fsr)) {
        bf_ui_fsr_supported(1);
        backend = Backend::dx12; return 1;
    }
    if (fsr) std::fprintf(stderr, "[GRAPHICS] FSR1 requires the DX12 path in this build; disabling upscaling\n");
    if (bf_d3d11_init(window, width, height)) { backend = Backend::dx11; return 1; }
    backend = Backend::none;
    std::fprintf(stderr, "[GRAPHICS] Using GDI fallback\n");
    return 0;
}
extern "C" int gpu_present_active(void) {
    if (backend == Backend::dx12) return bf::graphics::dx12_active();
    return backend == Backend::dx11 && bf_d3d11_active();
}
extern "C" int gpu_present_frame(const uint32_t *pixels) {
    if(backend==Backend::dx12 && live_fsr!=(bf_ui_fsr()!=0)) {
        live_fsr=bf_ui_fsr()!=0;
        bf::graphics::dx12_shutdown();
        if(!bf::graphics::dx12_init(host_window,host_width,host_height,live_fsr)) {
            bf_ui_fsr_supported(0);
            backend=bf_d3d11_init(host_window,host_width,host_height)?Backend::dx11:Backend::none;
        }
    }
    if (backend == Backend::dx12) {
        if(!bf::graphics::dx12_scale(bf_ui_scale()))return 0;
        return bf::graphics::dx12_frame(pixels);
    }
    if (backend == Backend::dx11) return bf_d3d11_frame(pixels);
    return 0;
}
extern "C" void gpu_present_shutdown(void) {
    bf::graphics::dx12_shutdown(); bf_d3d11_shutdown(); backend = Backend::none;
}
