// Optional read-only tracing of the real kernel bridge. No response overrides.
#include "boot_trace.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <atomic>
extern "C" {
#include "xbox/xboxrecomp.h"
BootGuestFunction recomp_lookup_kernel(uint32_t);
#ifdef _MSC_VER
extern __declspec(thread) uint32_t g_esp;
#else
extern __thread uint32_t g_esp;
#endif
}

static uint32_t ioctl_target;
static BootGuestFunction actual_ioctl;
static std::atomic_uint calls{0};

static uint32_t guest32(uint32_t va) {
    // Guest pointers are 32-bit offsets into the runtime's retail RAM map.
    if (uint64_t(va) + 4 > g_xbox_total_ram) throw std::out_of_range("trace guest DWORD");
    const auto *p = reinterpret_cast<const unsigned char *>(uintptr_t(xbox_GetMemoryOffset()) + va);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

static void dump(uint32_t va, uint32_t bytes) {
    if (uint64_t(va) + bytes > g_xbox_total_ram) return;
    const auto *p = reinterpret_cast<const unsigned char *>(uintptr_t(xbox_GetMemoryOffset()) + va);
    for (uint32_t i = 0; i < bytes; ++i) std::fprintf(stderr, " %02X", p[i]);
}

static void trace_ioctl() {
    uint32_t request = 0, length = 0, data = 0, data_length = 0;
    bool trace = calls.fetch_add(1, std::memory_order_relaxed) < 16;
    if (trace) {
        try {
            // The stack has a 4-byte return address, followed by ten DWORD
            // arguments. Slots 5/6/7 hold IOCTL, input pointer and input size.
            request = guest32(g_esp + 28);
            length = guest32(g_esp + 32);
            std::fprintf(stderr, "[BF_TRACE] ioctl=0x%08X input=%u", guest32(g_esp + 24), length);
            if (length >= 44) {
                // Xbox SCSI_PASS_THROUGH_DIRECT is 44 bytes: transfer length
                // at +12, guest data pointer at +20 and 16-byte CDB at +28.
                data_length = guest32(request + 12);
                data = guest32(request + 20);
                std::fprintf(stderr, " CDB:"); dump(request + 28, 16);
                std::fprintf(stderr, " data-before:"); dump(data, data_length < 48 ? data_length : 48);
            }
            std::fprintf(stderr, "\n");
        } catch (const std::exception &error) {
            std::fprintf(stderr, "[BF_TRACE] read rejected: %s\n", error.what());
            trace = false;
        }
    }
    // The runtime returns a shared dispatcher and selects its ordinal as a
    // lookup side effect. Select this call now, after all earlier kernel calls.
    const auto selected = recomp_lookup_kernel(ioctl_target);
    if (!selected) {
        std::fprintf(stderr, "[BF_TRACE] kernel target disappeared\n");
        std::exit(23);
    }
    selected();
    if (trace && length >= 44) {
        std::fprintf(stderr, "[BF_TRACE] request-after:"); dump(request, 44);
        std::fprintf(stderr, " data-after:"); dump(data, data_length < 48 ? data_length : 48);
        std::fprintf(stderr, "\n");
    }
}

void boot_trace_init(const char *xbe, std::size_t bytes) {
    const char *enabled = std::getenv("BF_TRACE_BOOT");
    if (!enabled || !*enabled || *enabled == '0') return;
    const auto file32 = [&](std::size_t offset) {
        if (offset > bytes || bytes - offset < 4) throw std::out_of_range("trace XBE DWORD");
        const auto *p = reinterpret_cast<const unsigned char *>(xbe + offset);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    };
    // Decode the retail kernel thunk VA from the XBE header at file +0x158.
    const uint32_t thunk = file32(0x158) ^ 0x5B6D40B6;
    const uint32_t base = file32(0x104);
    const uint32_t count = file32(0x11c);
    const uint32_t table = file32(0x120) - base;
    for (uint32_t s = 0; s < count; ++s) {
        // Section headers are 56 bytes: VA +4, raw offset +12, raw size +16.
        const std::size_t section = std::size_t(table) + s * 56;
        const uint32_t va = file32(section + 4), raw = file32(section + 12), size = file32(section + 16);
        if (thunk < va || thunk - va >= size) continue;
        for (uint32_t i = 0; i < 512; ++i) {
            // Every IAT slot is a 4-byte encoded ordinal in the original XBE.
            const uint32_t ordinal = file32(std::size_t(raw) + thunk - va + i * 4);
            if (!ordinal) return;
            if (ordinal == (0x80000000u | 196u)) {
                ioctl_target = guest32(thunk + i * 4);
                actual_ioctl = recomp_lookup_kernel(ioctl_target);
                return;
            }
        }
    }
}

BootGuestFunction boot_trace_lookup(uint32_t target) {
    return actual_ioctl && target == ioctl_target ? trace_ioctl : nullptr;
}
