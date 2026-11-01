#include "instruction_services.h"
#include <atomic>
#include <cstring>
#include <cstdio>
extern "C" {
#include "kernel.h"
#ifdef _MSC_VER
extern __declspec(thread) uint32_t g_eflags;
#else
extern __thread uint32_t g_eflags;
#endif
}

static thread_local bool usb_irq_masked;
static thread_local KIRQL usb_saved_irql;

bool handle_platform_instruction(const char *text, std::uint32_t guest_va) {
    // XPP's frame-counter snapshot masks guest interrupts around two reads.
    // Preserve IF and use the runtime's guest IRQL mask, which its USB/GPU
    // dispatchers already honor. Never execute privileged CLI/STI on the host.
    if (guest_va == 0x00384FA5 && std::strcmp(text, "cli") == 0) {
        if (usb_irq_masked) return false;
        usb_saved_irql = xbox_KfRaiseIrql(31);
        usb_irq_masked = true;
        g_eflags &= ~uint32_t(0x200); // EFLAGS bit 9 is guest IF.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return true;
    }
    if (guest_va == 0x00384FB6 && std::strcmp(text, "sti") == 0) {
        if (!usb_irq_masked) return false;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        g_eflags |= uint32_t(0x200);
        usb_irq_masked = false;
        xbox_KfLowerIrql(usb_saved_irql);
        return true;
    }
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
