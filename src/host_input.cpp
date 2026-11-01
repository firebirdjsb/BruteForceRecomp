#include "host_input.h"
#include <cstdio>
#include <cstdlib>
extern "C" {
#include "xbox_memory_layout.h"
#include "ohci.h"
}
#ifdef _WIN32
#include <windows.h>
static LONG CALLBACK device_fault(EXCEPTION_POINTERS *exception) {
    if (exception && exception->ExceptionRecord &&
        exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        exception->ExceptionRecord->NumberParameters >= 2 && xbox_VehMmioFault(exception))
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

bool start_host_input() {
    if (!std::getenv("RECOMP_USB")) return true;
#ifdef _WIN32
    // The runtime's statically linked XAPI reaches USB through guest MMIO.
    // Install the actual register decoder before the OHCI model protects its
    // two 4-KiB register pages. No external process or driver is modified.
    if (!AddVectoredExceptionHandler(1, device_fault)) {
        std::fprintf(stderr, "[INPUT] Cannot install the guest device fault handler\n");
        return false;
    }
    xbox_OhciInit();
    std::fprintf(stderr, "[INPUT] Guest USB controller model initialized\n");
    return true;
#else
    std::fprintf(stderr, "[INPUT] Host signal-handler integration is not implemented on this platform yet\n");
    return false;
#endif
}
