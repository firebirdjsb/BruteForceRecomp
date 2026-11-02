#include "guest_failure.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
extern "C" {
#include "xbox_memory_layout.h"
#ifdef _MSC_VER
extern __declspec(thread) uint32_t g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_esp, g_ebp, g_seh_ebp;
#else
extern __thread uint32_t g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_esp, g_ebp, g_seh_ebp;
#endif
}
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
bool word(uint32_t address, uint32_t &value) {
    if (uint64_t(address) + sizeof(value) > 0x100000000ull) return false;
    const auto *native = reinterpret_cast<const void *>(uintptr_t(xbox_GetMemoryOffset()) + address);
#ifdef _WIN32
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), native, &value, sizeof(value), &copied) && copied == sizeof(value);
#else
    // Only the committed low guest RAM is read here. Device and high alias
    // addresses require their own platform-aware reader and are omitted.
    if (uint64_t(address) + sizeof(value) > g_xbox_total_ram) return false;
    std::memcpy(&value, native, sizeof(value)); return true;
#endif
}
void words(const char *name, uint32_t address, unsigned count) {
    std::fprintf(stderr, "[GUEST-FAILURE] %s @%08X:", name, address);
    for (unsigned i = 0; i < count; ++i) {
        const uint64_t at = uint64_t(address) + i * sizeof(uint32_t);
        uint32_t value{};
        if (at > UINT32_MAX || !word(uint32_t(at), value)) { std::fprintf(stderr, " unreadable"); break; }
        std::fprintf(stderr, " %08X", value);
    }
    std::fprintf(stderr, "\n");
}
}
void report_guest_failure(uint32_t target) {
    std::fprintf(stderr, "[GUEST-FAILURE] target=%08X EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X ESP=%08X EBP=%08X SEH=%08X\n",
        target, g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_esp, g_ebp, g_seh_ebp);
    // A generated CALL pushes its original four-byte return VA before the
    // failure callback. Capture it before the caller restores its stack.
    words("stack", g_esp, 32);
    if (g_ebx) words("ebx data", g_ebx, 8);
    if (g_eax) words("eax object", g_eax, 8);
    if (g_ecx) words("ecx object", g_ecx, 8);
    if (g_esi) words("esi object", g_esi, 8);
    if (g_edi) words("edi object", g_edi, 8);
    uint32_t return_va{},caller_va{};
    if(word(g_esp,return_va)&&return_va==0x002B74D6 && word(g_esp+20,caller_va)&&caller_va==0x002C6D49) {
        // Exact retail call chain: 0x002C6A00 -> 0x002B7490 -> virtual call.
        // At the failed CALL its return slot, two arguments and two saved
        // DWORDs occupy 20 bytes; the outer CALL's return slot adds four.
        // The original outer routine addresses its four incoming arguments
        // at its stable ESP+0x58/+0x5C/+0x60/+0x64 respectively.
        const uint32_t outer_esp=g_esp+24;
        uint32_t handle{},rotation{},position{},body{},world{};
        word(outer_esp+0x58,handle);word(outer_esp+0x5C,rotation);word(outer_esp+0x60,position);
        words("physics call arguments",outer_esp+0x58,4);
        if(handle&&word(handle,body)&&body) {
            words("physics body",body,8);
            words("physics previous position",body+0xBC,4);
            words("physics inverse timestep",body+0x70,4);
            if(word(body+0xC,world)&&world) {
                words("physics world clock",world+0x108,6);
                words("physics minimum timestep",world+0xB4,2);
            }
        }
        if(rotation)words("physics input rotation",rotation,4);
        if(position)words("physics input position",position,4);
        words("physics caller distances",outer_esp+0x10,3);
        // 0x002B39F0 reserves 0x78 bytes and saves EBP/ESI. Its local base
        // is 0x68 above this caller's stable ESP; retain that bounded frame
        // to identify the producer of an invalid incoming position.
        words("physics producer frame",outer_esp+0x68,64);
    }
}
#ifdef _WIN32
namespace {
LONG WINAPI native_failure(EXCEPTION_POINTERS *failure) {
    if(!failure||!failure->ExceptionRecord||!failure->ContextRecord)return EXCEPTION_CONTINUE_SEARCH;
    const auto &record=*failure->ExceptionRecord;
    const auto &context=*failure->ContextRecord;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(stderr,"[NATIVE-FAILURE] code=%08lX RIP=%llX module-RVA=%llX operation=%llu address=%llX\n",
        record.ExceptionCode,(unsigned long long)context.Rip,(unsigned long long)(context.Rip-base),
        record.NumberParameters?record.ExceptionInformation[0]:0,
        record.NumberParameters>1?record.ExceptionInformation[1]:0);
    std::fprintf(stderr,"[NATIVE-FAILURE] RAX=%llX RBX=%llX RCX=%llX RDX=%llX RSI=%llX RDI=%llX R8=%llX R9=%llX\n",
        context.Rax,context.Rbx,context.Rcx,context.Rdx,context.Rsi,context.Rdi,context.R8,context.R9);
    report_guest_failure(0);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH; // Retain the original fatal exception.
}
}
#endif
void install_guest_failure_diagnostics() {
#ifdef _WIN32
    if(std::getenv("BF_TRACE_NATIVE_FAILURE"))SetUnhandledExceptionFilter(native_failure);
#endif
}
#ifdef _WIN32
extern "C" void bf_trace_menu_child(unsigned phase, uint32_t parent, uint32_t cursor, uint32_t child) {
    static unsigned count;
    if (++count > 8 && child) return;
    std::fprintf(stderr, "[MENU-CHILD] phase=%u parent=%08X cursor=%08X child=%08X\n", phase, parent, cursor, child);
    words("parent", parent, 8);
    if (parent <= UINT32_MAX - 0x74) words("children begin/end", parent + 0x74, 3);
    words("cursor", cursor, 4);
    if (child) words("child", child, 8);
}
extern "C" void bf_trace_menu_insert(unsigned phase, uint32_t parent, uint32_t child) {
    static unsigned count;
    if (++count > 1024) return;
    uint32_t begin{}, end{};
    if (parent > UINT32_MAX - 0x7C || !word(parent + 0x74, begin) || !word(parent + 0x78, end)) return;
    std::fprintf(stderr, "[MENU-INSERT] phase=%u parent=%08X child=%08X begin=%08X end=%08X\n", phase, parent, child, begin, end);
    if (begin && end >= begin && end - begin <= 1024) words("inserted children", begin, unsigned((end - begin) / 4));
}
#endif
