#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "xbox_memory_layout.h"
}

extern "C" uint32_t bf_gpu_semaphore_target(void) {
    const auto base = uintptr_t(xbox_GetMemoryOffset());
    const auto word = [base](uint32_t va) {
        uint32_t value;
        std::memcpy(&value, reinterpret_cast<const void *>(base + va), sizeof(value));
        return value;
    };
    // The hash-pinned XBE's D3D fence wait (0x00371CE0) reads this global
    // device pointer and the semaphore pointer at device+0x30. Read the
    // live pointer, never a volatile allocation address saved from a run.
    const uint32_t device = word(0x0037DD88);
    if (device < 0x10000 || uint64_t(device) + 0x34 > g_xbox_total_ram) {
        std::fprintf(stderr, "[GPU] Invalid D3D device for semaphore release\n");
        std::exit(26);
    }
    const uint32_t target = word(device + 0x30);
    // The original allocator returns the coherent contiguous-RAM alias.
    // Require an aligned DWORD wholly inside the mapped retail RAM window.
    if ((target & 3) || target < 0x80000000u ||
        uint64_t(target) + 4 > uint64_t(0x80000000u) + g_xbox_total_ram) {
        std::fprintf(stderr, "[GPU] Invalid semaphore target 0x%08X\n", target);
        std::exit(26);
    }
    std::fprintf(stderr, "[GPU] Command-driven semaphore target: 0x%08X\n", target);
    return target;
}
