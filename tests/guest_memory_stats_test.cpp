#include "guest_memory_stats.h"
#include <cstdio>
int main() {
    // 64-MB guest: the answer depends on guest reservations, never host RAM.
    if (bf_pages_for_bytes(0) != 0 || bf_pages_for_bytes(1) != 1 ||
        bf_pages_for_bytes(4096) != 1 || bf_pages_for_bytes(4097) != 2) return 1;
    const auto before = bf_memory_available(16384, 1280, 2176, 256, 1318);
    const auto after = bf_memory_available(16384, 1280, 2176, 256, 1318 + 4096);
    if (before != 11354 || before - after != 4096) return 2;
    if (bf_memory_available(16384, 16384, 0, 0, 0) != 0 ||
        bf_memory_available(16384, 20000, 0, 0, 0) != 0 ||
        bf_memory_available(16384, UINT32_MAX, UINT32_MAX, 0, 0) != 0) return 3;
    if (bf_memory_available(32768, 1280, 2176, 256, 1318) != before + 16384) return 4;
    std::puts("Guest memory page accounting passed.");
}
