#pragma once
#include <cstdint>
// Bounded read-only context captured before a failed guest call terminates.
void report_guest_failure(uint32_t target);
void install_guest_failure_diagnostics();
