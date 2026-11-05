#include "dx12_presenter.h"
#include "native_aspect_state.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include "../ui/host_ui.h"
#include "temporal_handoff.h"
#include "../ui/upscaler_state.h"

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
uint64_t source_sequence{},source_time{};
uint64_t temporal_sequence{};
std::string temporal_provider;
bool temporal_hud{};
double display_rate{},generated_rate{};uint64_t rate_time{};bf::graphics::PresentationStats previous_stats;
const char *choice() {
    const auto *value = std::getenv("BF_RENDER_BACKEND");
    return value && *value ? value : "auto";
}
void capture_present(uint64_t present_time) noexcept {
    // Opt-in diagnostic only: read back the actual DX12 swap-chain output on
    // its owning window thread. Never substitute the CPU source framebuffer.
    struct Capture {
        bool configured{},enabled{},triggered{},candidate_seen{};
        unsigned limit=4,count{};
        uint64_t last_candidate{},last_present{};
        std::array<uint64_t,8> sources{};
        std::filesystem::path directory,trigger;
    };
    static Capture capture;
    try {
        if(!capture.configured) {
            capture.configured=true;
            const auto *directory=std::getenv("BF_PRESENT_CAPTURE_DIR");
            const auto *trigger=std::getenv("BF_PRESENT_CAPTURE_TRIGGER");
            const auto *readback=std::getenv("BF_GRAPHICS_READBACK");
            if(!directory||!*directory||!trigger||!*trigger||!readback||std::strcmp(readback,"1"))return;
            capture.directory=std::filesystem::u8path(directory);capture.trigger=std::filesystem::u8path(trigger);
            if(const auto *count=std::getenv("BF_PRESENT_CAPTURE_COUNT");count&&*count) {
                char *end{};const auto value=std::strtoul(count,&end,10);
                if(end!=count&&!*end&&value>0)capture.limit=unsigned(value>8?8:value);
            }
            capture.enabled=true;
        }
        if(!capture.enabled||capture.count>=capture.limit||!bf::graphics::dx12_active())return;
        const auto presented=bf::graphics::dx12_stats().presents;
        // A minimized/occluded frame can leave the previous readback intact.
        // Require a newly successful present before assigning source metadata.
        if(presented==capture.last_present)return;
        capture.last_present=presented;
        // Repeated presentation/interpolation of one source never polls the
        // filesystem or reads the GPU again. At most eight unique IDs are saved.
        if(capture.candidate_seen&&capture.last_candidate==source_sequence)return;
        capture.candidate_seen=true;capture.last_candidate=source_sequence;
        for(unsigned i=0;i<capture.count;++i)if(capture.sources[i]==source_sequence)return;
        if(!capture.triggered) {
            std::error_code error;
            if(!std::filesystem::is_regular_file(capture.trigger,error))return;
            std::filesystem::create_directories(capture.directory);
            capture.triggered=true;
        }
        std::vector<uint32_t> rgba;uint32_t width{},height{};
        if(!bf::graphics::dx12_readback(rgba,width,height)) {
            capture.enabled=false;std::fprintf(stderr,"[PRESENT CAPTURE] GPU readback unavailable; capture stopped\n");return;
        }
        if(!width||!height||rgba.size()!=size_t(width)*height)throw std::runtime_error("Invalid GPU readback dimensions");
        const auto stem="present-"+std::to_string(capture.count+1)+"-source-"+std::to_string(source_sequence);
        const auto image_path=capture.directory/(stem+".ppm");
        std::ofstream image(image_path,std::ios::binary|std::ios::trunc);
        image<<"P6\n"<<width<<' '<<height<<"\n255\n";
        // R8G8B8A8 readback is little-endian: bytes 0, 1 and 2 are R, G and B.
        std::vector<char> row(size_t(width)*3);
        for(uint32_t y=0;y<height;++y) {
            for(uint32_t x=0;x<width;++x) {
                const auto pixel=rgba[size_t(y)*width+x];
                row[size_t(x)*3]=char(pixel&255);row[size_t(x)*3+1]=char((pixel>>8)&255);row[size_t(x)*3+2]=char((pixel>>16)&255);
            }
            image.write(row.data(),std::streamsize(row.size()));
        }
        image.close();if(!image)throw std::runtime_error("Cannot write GPU capture image");
        std::ofstream metadata(capture.directory/(stem+".json"),std::ios::trunc);
        metadata<<"{\n  \"capture\": \"dx12_swapchain_gpu_readback\",\n  \"source_sequence\": "<<source_sequence
            <<",\n  \"source_time_ns\": "<<source_time<<",\n  \"present_time_ns\": "<<present_time
            <<",\n  \"width\": "<<width<<",\n  \"height\": "<<height
            <<",\n  \"wide\": "<<(bf_aspect_frame_wide()?"true":"false")
            <<",\n  \"fsr1\": "<<(live_fsr&&temporal_sequence==0?"true":"false")
            <<",\n  \"temporal_sequence\": "<<temporal_sequence
            <<",\n  \"temporal_provider\": \""<<temporal_provider<<"\""
            <<",\n  \"native_hud_composed\": "<<(temporal_hud?"true":"false")<<"\n}\n";
        metadata.close();if(!metadata)throw std::runtime_error("Cannot write GPU capture metadata");
        capture.sources[capture.count++]=source_sequence;
        std::fprintf(stderr,"[PRESENT CAPTURE] GPU output %ux%u source=%llu wide=%d saved (%u/%u)\n",width,height,
            static_cast<unsigned long long>(source_sequence),bf_aspect_frame_wide(),capture.count,capture.limit);
    } catch(const std::exception &error) {
        capture.enabled=false;std::fprintf(stderr,"[PRESENT CAPTURE] %s; capture stopped\n",error.what());
    }
}
}
extern "C" uint64_t gpu_present_clock_ns(void) {
    static const uint64_t frequency=[] {LARGE_INTEGER value{};QueryPerformanceFrequency(&value);return uint64_t(value.QuadPart);}();
    LARGE_INTEGER value{};QueryPerformanceCounter(&value);const uint64_t ticks=uint64_t(value.QuadPart);
    return (ticks/frequency)*1000000000ull+(ticks%frequency)*1000000000ull/frequency;
}
extern "C" void gpu_present_source(uint64_t sequence,uint64_t time_ns){source_sequence=sequence;source_time=time_ns;}
extern "C" double gpu_present_display_fps(void){return display_rate;}
extern "C" double gpu_present_generated_fps(void){return generated_rate;}
extern "C" int gpu_present_enabled(void) { return std::strcmp(choice(), "gdi") != 0; }
extern "C" int gpu_present_init(HWND window, uint32_t width, uint32_t height) {
    host_window=window;host_width=width;host_height=height;
    const auto *requested = choice();
    if (std::strcmp(requested, "auto") && std::strcmp(requested, "dx12") && std::strcmp(requested, "dx11") && std::strcmp(requested, "gdi")) {
        std::fprintf(stderr, "[GRAPHICS] Unknown backend '%s'; using automatic selection\n", requested);
        requested = "auto";
    }
    const auto *upscaler = std::getenv("BF_UPSCALER");
    // Settings have already initialized this request. Presenter creation must
    // read it; applying an absent environment override would erase persistence.
    const auto mode=bf_upscaler_mode();
    const bool fsr=mode==BF_UPSCALER_FSR1;
    live_fsr=fsr;bf_ui_fsr_supported(0);bf_aspect_presenter_supported(0);
    if (upscaler && *upscaler && std::strcmp(upscaler, "off") && !fsr&&mode!=3&&mode!=4)
        std::fprintf(stderr, "[GRAPHICS] Upscaler '%s' is unavailable in this renderer; using bilinear\n", upscaler);
    if (!std::strcmp(requested, "gdi")) return 0;
    if (std::strcmp(requested, "dx11") && bf::graphics::dx12_init(window, width, height, fsr)) {
        bf_ui_fsr_supported(1);
        bf_aspect_presenter_supported(1);
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
            bf_aspect_presenter_supported(0);
            backend=bf_d3d11_init(host_window,host_width,host_height)?Backend::dx11:Backend::none;
        }
    }
    if (backend == Backend::dx12) {
        if(!bf::graphics::dx12_aspect(bf_aspect_frame_wide()!=0)) {
            bf_aspect_presenter_supported(0);return 0;
        }
        if(!bf::graphics::dx12_scale(bf_ui_scale())){bf_aspect_presenter_supported(0);return 0;}
        const auto rate=bf_ui_interpolation_allowed()?bf_ui_interpolation():0;
        const auto now=gpu_present_clock_ns();
        const auto before=bf::graphics::dx12_stats();
        int paced=0;
        temporal_sequence=0;temporal_provider.clear();temporal_hud=false;
        const auto temporal=bf::graphics::selected_temporal_frame();
        if(temporal) {
            auto frame=temporal->presentation();
            frame.stamp.source_ns=source_time;frame.stamp.now_ns=now;
            static std::vector<uint32_t> host_overlay;
            host_overlay.resize(size_t(host_width)*host_height);
            bf_ui_paint_overlay(host_overlay.data(),host_width,host_height);
            frame.host_overlay=host_overlay.data();
            paced=bf::graphics::dx12_frame_texture(frame);
            if(paced) {
                temporal_sequence=temporal->sequence;temporal_provider=temporal->provider;temporal_hud=temporal->hud!=nullptr;
                const unsigned actual=temporal_provider.size()>1&&temporal_provider[1]=='.'&&
                    (temporal_provider[0]=='3'||temporal_provider[0]=='4')?unsigned(temporal_provider[0]-'0'):0;
                bf_upscaler_status(actual,temporal_provider.c_str(),"");
            }
        }
        if(!paced) paced=bf::graphics::dx12_frame_timed(pixels,{source_sequence,source_time,now,rate});
        capture_present(now);
        if(!bf::graphics::dx12_active())bf_aspect_presenter_supported(0);
        const auto stats=bf::graphics::dx12_stats();
        if(stats.presents>before.presents&&!temporal_sequence&&bf_upscaler_mode()<=1) {
            const bool spatial=stats.spatial_frames>before.spatial_frames;
            bf_upscaler_status(spatial?1:0,spatial?"EASU + RCAS":"",
                !spatial&&bf_upscaler_mode()==1?"FSR 1 unavailable at this output size":"");
        }
        if(!rate_time||stats.presents<previous_stats.presents){rate_time=now;previous_stats=stats;}
        if(now-rate_time>=1000000000ull) {
            const double seconds=double(now-rate_time)/1e9;
            display_rate=(stats.presents-previous_stats.presents)/seconds;generated_rate=(stats.generated-previous_stats.generated)/seconds;
            if(std::getenv("BF_PRESENT_STATS"))std::fprintf(stderr,"[PRESENT] source %.2f/s display %.2f/s generated %.2f/s target=%u uploads %.2f/s FSR %.2f/s\n",
                (stats.source_frames-previous_stats.source_frames)/seconds,display_rate,generated_rate,rate,
                (stats.uploads-previous_stats.uploads)/seconds,(stats.spatial_passes-previous_stats.spatial_passes)/seconds);
            rate_time=now;previous_stats=stats;
        }
        return paced;
    }
    if (backend == Backend::dx11) return bf_d3d11_frame(pixels);
    return 0;
}
extern "C" void gpu_present_shutdown(void) {
    bf_aspect_presenter_supported(0);
    bf::graphics::dx12_shutdown(); bf_d3d11_shutdown(); backend = Backend::none;
}
