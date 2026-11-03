// Experimental host bound to the authenticated retail executable.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <exception>
#include <string_view>
#include "boot_trace.h"
#include "native_media.h"
#include "instruction_services.h"
#include "startup_sampler.h"
#include "host_input.h"
#include "host_audio.h"
#include "guest_failure.h"
#include "ui/host_ui.h"
#ifdef BF_EXPERIMENTAL_NV2A_D3D11
extern "C" int bruteforce_d3d11_install(void);
extern "C" void bf_scene_d3d12_shutdown(void);
#endif
extern "C" {
#include "xbox/xboxrecomp.h"
#include "video_player.h"
using GuestFunction = void (*)(void);
GuestFunction recomp_lookup(uint32_t);
void sub_0037EE7C(void);
void sub_002F04CB(void);
void sub_002F04D2(void);
void sub_002F04D5(void);
void sub_002F04DC(void);
void sub_002F04E3(void);
int recomp_dispatch_init(void);
void xbox_path_init(const char *, const char *);
#ifdef _MSC_VER
extern __declspec(thread) uint32_t g_esp;
#else
extern __thread uint32_t g_esp;
#endif
// Normal calls belong to the generated dispatcher; audited missing callbacks
// and optional tracing entries are supplied here.
GuestFunction recomp_lookup_manual(uint32_t target) {
    // Measured startup callback, recovered from its hash-verified XBE body.
    if (target == 0x0037EE7C) return sub_0037EE7C;
    switch(target) {
    case 0x002F04CB:return sub_002F04CB;
    case 0x002F04D2:return sub_002F04D2;
    case 0x002F04D5:return sub_002F04D5;
    case 0x002F04DC:return sub_002F04DC;
    case 0x002F04E3:return sub_002F04E3;
    default:break;
    }
    return boot_trace_lookup(target);
}
void recomp_icall_fail_log(uint32_t va) {
    std::fprintf(stderr, "Unresolved guest call at 0x%08X; stopping.\n", va);
    report_guest_failure(va);
    std::exit(20);
}
void recomp_icall_not_code_log(uint32_t va) {
    std::fprintf(stderr, "Invalid guest code target 0x%08X; stopping.\n", va);
    report_guest_failure(va);
    std::exit(21);
}
void recomp_unimpl(const char *instruction, uint32_t va) {
    if (handle_platform_instruction(instruction, va)) return;
    std::fprintf(stderr, "Unsupported instruction at 0x%08X: %s\n", va, instruction);
    std::exit(22);
}
}

static bool memory_ready = false;
static bool kernel_ready = false;
static void cleanup() {
    if (kernel_ready) { kernel_ready = false; xbox_kernel_shutdown(); }
    stop_host_audio();
    if (memory_ready) { memory_ready = false; xbox_MemoryLayoutShutdown(); }
#ifdef BF_EXPERIMENTAL_NV2A_D3D11
    bf_scene_d3d12_shutdown();
#endif
}

static int run_native(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    try {
        if(argc==2 && std::string_view(argv[1])=="--capabilities") {
#ifdef BF_EXPERIMENTAL_NV2A_D3D11
            std::puts("nv2a_d3d11=1\nnv2a_d3d12=1\nhost_fps60=1");
#else
            std::puts("nv2a_d3d11=0\nnv2a_d3d12=0\nhost_fps60=0");
#endif
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--validate-only")
            return native_media_initialize(std::filesystem::absolute(argv[2])) ? 0 : 11;
        if (argc != 3) {
            std::fprintf(stderr, "Usage: bruteforce_recomp <extracted-game-folder> <scratch-save-folder>\n");
            return 2;
        }
        const auto game = std::filesystem::absolute(argv[1]);
        const auto saves = std::filesystem::absolute(argv[2]);
        if (game == saves) return 2;
        if (!native_media_initialize(game)) return 11;
        // Enable both codec/MMIO and APU halves together. Actual menu PCM
        // has been measured; BF_ENABLE_AUDIO=0 retains a diagnostic fallback.
        const char *audio_setting=std::getenv("BF_ENABLE_AUDIO");
        const bool audio=!audio_setting || (*audio_setting && *audio_setting!='0');
        if(audio){
#ifdef _WIN32
            _putenv_s("RECOMP_AC97_READY","1");
#else
            setenv("RECOMP_AC97_READY","1",1);
#endif
        }else{
#ifdef _WIN32
            _putenv_s("RECOMP_AC97_READY","");
#else
            unsetenv("RECOMP_AC97_READY");
#endif
        }
        // This XDK builds its D3D notification event directly in guest RAM
        // (live device +0x195C). The runtime validates Type/Size and mirrors
        // its SignalState; a guest address must never become a host HANDLE.
        // Keep an explicit environment override for baseline comparisons.
        if (!std::getenv("RECOMP_TITLE_KEVENTS")) {
#ifdef _WIN32
            _putenv_s("RECOMP_TITLE_KEVENTS", "1");
#else
            setenv("RECOMP_TITLE_KEVENTS", "1", 0);
#endif
        }
        // Follow actual FIFO JUMP/CALL/RETURN and submission boundaries. The
        // XDK fence and reusable-buffer software methods now have handlers;
        // inferred min/max PUT segments lose commands when the ring wraps.
        if (!std::getenv("RECOMP_PB_WALK")) {
#ifdef _WIN32
            _putenv_s("RECOMP_PB_WALK", "dma");
#else
            setenv("RECOMP_PB_WALK", "dma", 0);
#endif
        }
        std::filesystem::create_directories(saves);
        std::ifstream file(game / "default.xbe", std::ios::binary | std::ios::ate);
        if (!file) return 3;
        const auto size = file.tellg();
        if (size != 4366336) { std::fprintf(stderr, "Unsupported XBE size.\n"); return 4; }
        std::vector<char> bytes(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(bytes.data(), size)) return 5;
        // Decode little-endian DWORDs without unaligned pointer casts.
        const auto read32 = [&](size_t offset) -> uint32_t {
            uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i)
                value |= uint32_t(static_cast<unsigned char>(bytes.at(offset + i))) << (8 * i);
            return value;
        };
        // Offsets are XBE file-header fields, pinned by verification.json.
        // 0x128 contains the retail XOR-encoded 32-bit guest entry point.
        if (read32(0) != 0x48454258 || (read32(0x128) ^ 0xA8FC57AB) != 0x002F5D1F)
            return 6;
        if (const char *backend=std::getenv("RECOMP_GPU"); backend && *backend && std::string_view(backend)!="cpu") {
            if (std::string_view(backend)!="d3d11"&&std::string_view(backend)!="d3d12") {
                std::fprintf(stderr,"Unknown scene backend: %s\n",backend); return 14;
            }
#ifdef BF_EXPERIMENTAL_NV2A_D3D11
            // The retail tutorial/HUD uses programmable vertices at screen
            // depth zero. Viewport c[59].z is not an extra UI clipping plane.
            // Keep an explicit diagnostic override for matched comparisons.
            if(!std::getenv("RECOMP_PROGRAM_NEAR_ZERO")) {
#ifdef _WIN32
                _putenv_s("RECOMP_PROGRAM_NEAR_ZERO","1");
#else
                setenv("RECOMP_PROGRAM_NEAR_ZERO","1",0);
#endif
            }
            if (!bruteforce_d3d11_install()) return 14;
#else
            std::fprintf(stderr,"GPU scene rendering requires BF_EXPERIMENTAL_NV2A_D3D11=ON.\n"); return 14;
#endif
        }
#ifdef BF_EXPERIMENTAL_NV2A_D3D11
        bf_ui_settings_initial(1);
#else
        bf_ui_settings_initial(0);
#endif
        std::atexit(cleanup);
        if (!xbox_MemoryLayoutInit(bytes.data(), bytes.size())) return 7;
        memory_ready = true;
        install_guest_failure_diagnostics();
        xbox_kernel_init();
        kernel_ready = true;
        xbox_path_init(game.string().c_str(), saves.string().c_str());
        xbox_kernel_bridge_init();
        if (!start_host_input()) return 12;
        if (audio && !start_host_audio()) return 13;
        boot_trace_init(bytes.data(), bytes.size());
        // ESP is a guest 32-bit register. The runtime owns its mapped stack.
        g_esp = XBOX_STACK_TOP;
        if (!recomp_dispatch_init()) return 8;
        // Guest address belongs to this exact XBE, not a host process address.
        const auto entry = recomp_lookup(0x002F5D1F);
        if (!entry) return 9;
        xbox_WatchdogStart();
        start_startup_sampler();
        std::fprintf(stderr, "Experimental Brute Force boot; gameplay unverified.\n");
        entry();
        cleanup();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Host failure: %s\n", error.what());
        cleanup();
        return 10;
    }
}

#ifndef _WIN32
static int process_argc;
static char **process_argv;
static int guest_window_main() { return run_native(process_argc, process_argv); }
#endif

int main(int argc, char **argv) {
#ifndef _WIN32
    // SDL owns the process main thread for window/event/controller pumping;
    // the runtime creates the guest thread with the required stack size.
    // Offline validation must also work on a machine with no display.
    if (argc == 3 && std::string_view(argv[1]) != "--validate-only") {
        process_argc = argc;
        process_argv = argv;
        return xbox_HostWindowMain(guest_window_main);
    }
#endif
    return run_native(argc, argv);
}
