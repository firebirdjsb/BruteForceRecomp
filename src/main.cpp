// Experimental host; no game-function overrides or guessed global writes.
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
extern "C" {
#include "xbox/xboxrecomp.h"
using GuestFunction = void (*)(void);
GuestFunction recomp_lookup(uint32_t);
int recomp_dispatch_init(void);
void xbox_path_init(const char *, const char *);
#ifdef _MSC_VER
extern __declspec(thread) uint32_t g_esp;
#else
extern __thread uint32_t g_esp;
#endif
// NULL delegates to the generated dispatcher. This host has no overrides.
GuestFunction recomp_lookup_manual(uint32_t target) { return boot_trace_lookup(target); }
void recomp_icall_fail_log(uint32_t va) {
    std::fprintf(stderr, "Unresolved guest call at 0x%08X; stopping.\n", va);
    std::exit(20);
}
void recomp_icall_not_code_log(uint32_t va) {
    std::fprintf(stderr, "Invalid guest code target 0x%08X; stopping.\n", va);
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
    if (memory_ready) { memory_ready = false; xbox_MemoryLayoutShutdown(); }
}

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    try {
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
        std::atexit(cleanup);
        if (!xbox_MemoryLayoutInit(bytes.data(), bytes.size())) return 7;
        memory_ready = true;
        xbox_kernel_init();
        kernel_ready = true;
        xbox_path_init(game.string().c_str(), saves.string().c_str());
        xbox_kernel_bridge_init();
        boot_trace_init(bytes.data(), bytes.size());
        // ESP is a guest 32-bit register. The runtime owns its mapped stack.
        g_esp = XBOX_STACK_TOP;
        if (!recomp_dispatch_init()) return 8;
        // Guest address belongs to this exact XBE, not a host process address.
        const auto entry = recomp_lookup(0x002F5D1F);
        if (!entry) return 9;
        xbox_WatchdogStart();
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
