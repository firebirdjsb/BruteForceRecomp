#include "instruction_services.h"
#include <atomic>
#include <cstring>
#include <cstdio>

bool handle_platform_instruction(const char *text, std::uint32_t guest_va) {
    if (guest_va != 0x00376C1D || std::strcmp(text, "wbinvd") != 0) return false;
    // Original XBE instruction at guest 0x00376C1D is 0F 09 (WBINVD).
    // This port maps guest RAM as ordinary coherent host memory; GPU work is
    // translated through the host graphics API, with no Xbox cache model or
    // physical GPU DMA consuming dirty CPU cache lines. Publish CPU writes at
    // this guest cache-maintenance boundary. Do not execute privileged WBINVD
    // on the host, or claim cycle/cache-state equivalence to Xbox hardware.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    static std::atomic_bool reported{false};
    if (!reported.exchange(true, std::memory_order_relaxed))
        std::fprintf(stderr, "[CPU] Guest WBINVD: coherent host-memory barrier\n");
    return true;
}
