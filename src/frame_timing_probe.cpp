#include "gpu/frame_interval.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "xbox_memory_layout.h"
}

namespace {
bool read(uint64_t address, void *out, size_t bytes) {
    // Diagnostics only read committed low guest RAM, never MMIO or aliases.
    if (address < 0x10000 || uint64_t(address) + bytes > g_xbox_total_ram) return false;
    const auto *source = reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset()) + address);
#ifdef _WIN32
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), source, out, bytes, &copied) && copied == bytes;
#else
    std::memcpy(out, source, bytes); return true;
#endif
}
}

extern "C" uint32_t bf_frame_probe_swap(uint32_t parameter) {
    static const unsigned interval = [] {
        const char *value = std::getenv("BF_TEST_FRAME_INTERVAL");
        return value && std::strcmp(value, "1") == 0 ? 1u : 0u;
    }();
    const uint32_t changed=bf_swap_interval(parameter, interval?60:30);
    static unsigned logged{};
    if (interval && (parameter & 31u)==1u && logged++<8)
        std::fprintf(stderr,"[FRAME-PROBE] swap %08X -> %08X (interval %u -> %u, immediate %u)\n",
            parameter,changed,(parameter>>5)&7u,(changed>>5)&7u,(parameter>>8)&1u);
    return changed;
}

extern "C" void bf_frame_probe_body(uint32_t world, uint32_t body, uint32_t stack) noexcept try {
    // Authenticated original 0x002C6A7B, before FSTP [ESI+0x78]. All offsets
    // below describe that exact retail function, not discovered host pointers.
    // No guest register, x87 slot, stack byte or physics field is written.
    static FILE *file = [] {
        const char *path = std::getenv("BF_FRAME_TIMING_CSV");
        FILE *opened = path && *path ? std::fopen(path, "w") : nullptr;
        if (opened) std::fputs("host_us,world,body,world_time,next_time,body_time,minimum,step,previous_x,previous_y,previous_z,input_x,input_y,input_z\n", opened);
        return opened;
    }();
    if (!file) return;
    if (uint64_t(world)+0x120 > g_xbox_total_ram || uint64_t(body)+0xC8 > g_xbox_total_ram ||
        uint64_t(stack)+0x64 > g_xbox_total_ram) return;
    double time{}, next{}, last{};
    float minimum{}, step{}, previous[3]{}, input[3]{};
    uint32_t position{};
    // World +0x108/+0x118 are double clocks. Body +0x70 is its previous
    // clock, +0xBC its previous position. Stable ESP+0x10 is the original
    // effective float timestep; ESP+0x60 holds the input-position pointer.
    if (!read(world + 0x108, &time, 8) || !read(world + 0x118, &next, 8) ||
        !read(body + 0x70, &last, 8) || !read(world + 0xB4, &minimum, 4) ||
        !read(stack + 0x10, &step, 4) || !read(body + 0xBC, previous, 12) ||
        !read(stack + 0x60, &position, 4) || !read(position, input, 12)) return;
    static std::mutex lock;
    std::lock_guard guard(lock);
    static unsigned rows{};
    // Bound diagnostic output even if an unexpected call loop is encountered.
    if (rows >= 2000000) return;
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::fprintf(file, "%lld,%08X,%08X,%.12g,%.12g,%.12g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
        static_cast<long long>(now), world, body, time, next, last, minimum, step,
        previous[0], previous[1], previous[2], input[0], input[1], input[2]);
    if (++rows % 128 == 0) std::fflush(file);
} catch (...) { std::fputs("[FRAME-PROBE] body sample dropped\n",stderr); }

extern "C" void bf_frame_probe_clock(uint32_t world, uint32_t stack) noexcept try {
    static FILE *file = [] {
        const char *path = std::getenv("BF_FRAME_TIMING_CSV");
        FILE *opened = path && *path ? std::fopen((std::string(path)+".clock.csv").c_str(), "w") : nullptr;
        if (opened) std::fputs("host_us,world,caller,current,target,minimum\n", opened);
        return opened;
    }();
    if (!file || uint64_t(world)+0x110 > g_xbox_total_ram || uint64_t(stack)+20 > g_xbox_total_ram) return;
    // Original 0x002C7D00 takes scheduler, world and double target on its
    // unmodified incoming stack: world at +8, target at +12 (eight bytes).
    double current{}, target{}; float minimum{}; uint32_t caller{};
    if (!read(world+0x108,&current,8) || !read(world+0xB4,&minimum,4) ||
        !read(stack+12,&target,8) || !read(stack,&caller,4)) return;
    static std::mutex lock;
    std::lock_guard guard(lock);
    static unsigned rows{};
    if (rows >= 2000000) return;
    const auto now=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::fprintf(file,"%lld,%08X,%08X,%.12g,%.12g,%.9g\n",
        static_cast<long long>(now),world,caller,current,target,minimum);
    if (++rows % 64 == 0) std::fflush(file);
} catch (...) { std::fputs("[FRAME-PROBE] clock sample dropped\n",stderr); }
