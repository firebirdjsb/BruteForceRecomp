#pragma once
#include <cstddef>
#include <cstdint>
using BootGuestFunction = void (*)(void);
void boot_trace_init(const char *xbe, std::size_t bytes);
BootGuestFunction boot_trace_lookup(std::uint32_t guest_target);
