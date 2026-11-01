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
#elif defined(__linux__)
#include <csignal>
#include <unistd.h>
static struct sigaction previous_segv{}, previous_bus{};
static void device_fault(int signal, siginfo_t *info, void *context) {
    if (info && context && xbox_PosixMmioFault(context, reinterpret_cast<uintptr_t>(info->si_addr))) return;
    const auto &previous = signal == SIGSEGV ? previous_segv : previous_bus;
    // Forward unrelated faults to the prior owner. A default/ignored
    // synchronous memory fault must retain its fatal signal, never loop by
    // returning to an instruction the device model did not understand.
    if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN) {
        if (previous.sa_flags & SA_SIGINFO) previous.sa_sigaction(signal, info, context);
        else previous.sa_handler(signal);
        return;
    }
    struct sigaction fatal{};
    fatal.sa_handler = SIG_DFL;
    sigemptyset(&fatal.sa_mask);
    sigaction(signal, &fatal, nullptr);
    raise(signal);
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
#elif defined(__linux__)
    struct sigaction action{};
    action.sa_sigaction = device_fault;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, &previous_segv) != 0) return false;
    if (sigaction(SIGBUS, &action, &previous_bus) != 0) {
        sigaction(SIGSEGV, &previous_segv, nullptr);
        return false;
    }
    xbox_OhciInit();
    std::fprintf(stderr, "[INPUT] Guest USB controller model initialized (POSIX)\n");
    return true;
#else
    std::fprintf(stderr, "[INPUT] Host signal-handler integration is not implemented on this platform yet\n");
    return false;
#endif
}
