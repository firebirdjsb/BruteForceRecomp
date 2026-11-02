#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "xbox_memory_layout.h"
int bf_gpu_signal_event(uint32_t guest_va);
int bf_gpu_apply_pushbuffer(uint32_t parameter, uint32_t return_address);
}

extern "C" int bf_gpu_software_method(uint32_t parameter, uint32_t clear_depth) {
    if (!parameter) return 0;
    // Original handler 0x00377F80 selects the low five bits. Case 5 at
    // 0x00378119 signals miniport+0x1A4, equivalent to device+0x196C.
    // D3D_BlockOnTime (0x00371CE0) emits it and waits on that same event.
    const uint32_t operation = parameter & 31u;
    if (std::getenv("BF_TRACE_PUSHBUFFER")) {
        static unsigned counts[32]{};
        if (++counts[operation] <= 8)
            std::fprintf(stderr,"[GPU-SOFTWARE] operation=%u parameter=%08X\n",operation,parameter);
    }
    if (operation == 3 || operation == 4 || operation == 10)
        return bf_gpu_apply_pushbuffer(parameter,clear_depth);
    if (operation != 5) return 0;
    uint32_t device;
    const auto base = uintptr_t(xbox_GetMemoryOffset());
    std::memcpy(&device,reinterpret_cast<const void *>(base+0x0037DD88u),4);
    // The authenticated image owns this device pointer; allocations remain
    // dynamic. Validate the complete 16-byte event before the bridge reads it.
    if ((device & 3u) || device < 0x10000u ||
        uint64_t(device)+0x197Cu > g_xbox_total_ram ||
        !bf_gpu_signal_event(device+0x196Cu)) {
        std::fprintf(stderr,"[GPU] Invalid fence notification at device=%08X\n",device);
        std::exit(26);
    }
    return 1;
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
