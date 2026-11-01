// Optional diagnostics for this host's own main thread, never another process.
#include "startup_sampler.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include "xbox_memory_layout.h"

static bool guest_word(uint32_t va, uint32_t *value) {
    SIZE_T read = 0;
    // Read only this process's mapped guest memory; the API rejects bad pages.
    const uintptr_t address = uintptr_t(xbox_GetMemoryOffset()) + va;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address),
                             value, sizeof(*value), &read) && read == sizeof(*value);
}

static DWORD WINAPI sample_main_thread(void *argument) {
    const DWORD id = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(argument));
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
    if (!thread) return 1;
    const HANDLE process = GetCurrentProcess();
    const bool symbols = SymInitialize(process, nullptr, TRUE) != FALSE;
    for (unsigned sample = 0; sample < 3; ++sample) {
        Sleep(2000);
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (SuspendThread(thread) == DWORD(-1)) break;
        const BOOL captured = GetThreadContext(thread, &context);
        uint32_t device = 0, put = 0, get_pointer = 0, get = 0;
        // Authenticated D3D wait at guest 0x00371DD3 reads the device pointer
        // at 0x0037DD88, submitted position at +0x2C, and GET pointer at +0x30.
        const bool ring = guest_word(0x0037DD88, &device) && device &&
            device <= UINT32_MAX - 0x30 && guest_word(device + 0x2C, &put) &&
            guest_word(device + 0x30, &get_pointer) && guest_word(get_pointer, &get);
        // Resume before logging/symbol lookup, which may acquire host locks.
        if (ResumeThread(thread) == DWORD(-1)) {
            std::fprintf(stderr, "[SAMPLE] Cannot resume main thread\n");
            TerminateProcess(process, 25);
        }
        if (!captured) continue;
        // RIP is the native x64 instruction pointer; it is not a guest VA.
        const DWORD64 pc = context.Rip;
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 256]{};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (symbols && SymFromAddr(process, pc, &displacement, symbol))
            std::fprintf(stderr, "[SAMPLE] main: %s+0x%llX", symbol->Name, static_cast<unsigned long long>(displacement));
        else
            std::fprintf(stderr, "[SAMPLE] main native RIP: 0x%llX", static_cast<unsigned long long>(pc));
        if (symbols && SymGetLineFromAddr64(process, pc, &line_displacement, &line))
            std::fprintf(stderr, " at %s:%lu", line.FileName, static_cast<unsigned long>(line.LineNumber));
        std::fprintf(stderr, "\n");
        if (ring)
            std::fprintf(stderr, "[SAMPLE] D3D device=0x%08X submitted=0x%08X get-pointer=0x%08X completed=0x%08X\n",
                         device, put, get_pointer, get);
    }
    if (symbols) SymCleanup(process);
    CloseHandle(thread);
    return 0;
}
#endif

void start_startup_sampler() {
    const char *enabled = std::getenv("BF_SAMPLE_BOOT");
    if (!enabled || !*enabled || *enabled == '0') return;
#ifdef _WIN32
    HANDLE worker = CreateThread(nullptr, 0, sample_main_thread,
        reinterpret_cast<void *>(static_cast<ULONG_PTR>(GetCurrentThreadId())), 0, nullptr);
    if (worker) CloseHandle(worker);
    else std::fprintf(stderr, "[SAMPLE] Cannot start sampler\n");
#else
    std::fprintf(stderr, "[SAMPLE] Native PC sampling currently requires Windows x64\n");
#endif
}
