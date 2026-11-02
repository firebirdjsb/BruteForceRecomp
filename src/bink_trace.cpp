// Bounded read-only diagnostics around the original BinkCopyToBuffer body.
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
extern "C" {
#include "xbox_memory_layout.h"
extern __declspec(thread) uint32_t g_esp, g_eax;
void bf_original_bink_copy(void);
void bf_original_bink_open(void);
void bf_original_002F8CB0(void);
void bf_original_002F8DA0(void);
}
namespace {
std::atomic_uint menu_handle{};
std::atomic_bool menu_loading{};
bool read(uint32_t va, void *data, size_t bytes) {
    SIZE_T copied = 0;
    return uint64_t(va) + bytes <= 0x100000000ull &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(uintptr_t(xbox_GetMemoryOffset()) + va),
            data, bytes, &copied) && copied == bytes;
}
void sample(const char *name, uint32_t va) {
    std::array<uint8_t, 16> bytes{};
    std::fprintf(stderr, "[BINK] %s %08X:", name, va);
    if (read(va, bytes.data(), bytes.size())) for (auto byte : bytes) std::fprintf(stderr, " %02X", byte);
    else std::fprintf(stderr, " unreadable");
    std::fprintf(stderr, "\n");
}
void dump(const char *name, uint32_t va, size_t size) {
    const char *dir = std::getenv("BF_BINK_DUMP_DIR");
    if (!dir || size > 1024 * 1024) return;
    std::vector<uint8_t> bytes(size);
    if (!read(va,bytes.data(),bytes.size())) return;
    const auto path = std::filesystem::path(dir) / name;
    FILE *f = nullptr;
    if (_wfopen_s(&f,path.c_str(),L"wb") == 0) { std::fwrite(bytes.data(),1,bytes.size(),f);std::fclose(f); }
}
}
extern "C" void sub_003574B0(void) {
    std::array<uint32_t, 3> args{};
    const bool trace = std::getenv("BF_TRACE_BINK") && read(g_esp,args.data(),sizeof(args));
    char path[200]{};
    if(trace)read(args[1],path,sizeof(path)-1);
    const bool menu = trace && (std::strstr(path,"menuBack") || std::strstr(path,"MenuBack"));
    if (menu) menu_loading = true;
    bf_original_bink_open();
    if (menu) menu_loading = false;
    if (trace) {
        std::fprintf(stderr,"[BINK-OPEN] caller=%08X flags=%08X handle=%08X file=%s\n",args[0],args[2],g_eax,path);
        if (menu) { menu_handle = g_eax; dump("menu-open.bin",g_eax,0x38c); }
        if(g_eax){sample("handle",g_eax);sample("planes",g_eax+0xB8);}
    }
}
extern "C" void sub_002F8CB0(void) {
    std::array<uint32_t,3> args{};
    const bool trace=menu_loading && read(g_esp,args.data(),sizeof(args));
    bf_original_002F8CB0();
    if(trace)std::fprintf(stderr,"[LAYOUT] queue caller=%08X slot=%08X size=%u\n",args[0],args[1],args[2]);
}
extern "C" void sub_002F8DA0(void) {
    std::array<uint32_t,2> args{};
    std::array<uint32_t,64> pointers{}, lengths{};
    uint32_t count=0, ptrs=0, sizes=0;
    const bool trace=menu_loading && read(g_esp,args.data(),sizeof(args));
    if(trace) {
        read(0x412ADC,&count,4); read(0x3B7D10,&ptrs,4); read(0x3B7D14,&sizes,4);
        if(count<=64){read(ptrs,pointers.data(),count*4);read(sizes,lengths.data(),count*4);}
    }
    bf_original_002F8DA0();
    if(trace) {
        std::fprintf(stderr,"[LAYOUT] allocate caller=%08X base=%08X size=%u entries=%u\n",args[0],g_eax,args[1],count);
        for(uint32_t i=0;i<count && i<64;++i){uint32_t value=0;read(pointers[i],&value,4);std::fprintf(stderr,"[LAYOUT] slot=%08X size=%u value=%08X\n",pointers[i],lengths[i],value);}
    }
}
extern "C" void sub_00359280(void) {
    std::array<uint32_t, 8> args{};
    // Original 0x00359280 accepts seven DWORD stack arguments and RET 28.
    // args[0] is its four-byte return slot; args[1..7] are Bink, destination,
    // pitch, height, destination x/y and flags. The original body owns the ABI.
    static std::atomic_uint menu_count{}, intro_count{};
    bool trace = false;
    if (std::getenv("BF_TRACE_BINK") && read(g_esp, args.data(), sizeof(args))) {
        auto &count = args[1] == menu_handle.load() ? menu_count : intro_count;
        trace = count.fetch_add(1) < 6;
    }
    if (trace) {
        std::fprintf(stderr, "[BINK] copy caller=%08X handle=%08X dst=%08X pitch=%u height=%u xy=%u,%u flags=%08X\n",
            args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        std::array<uint32_t, 12> fields{};
        // These are the actual plane/bank fields consumed by original
        // BinkCopyToBufferRect at 0x00358400, not a fabricated BINK structure.
        if (read(args[1] + 0xB8, fields.data(), sizeof(fields))) {
            std::fprintf(stderr, "[BINK] B8..E4:"); for (auto field : fields) std::fprintf(stderr, " %08X", field); std::fprintf(stderr, "\n");
            if (fields[0] < 2) {
                sample("YUV bank", fields[1 + fields[0]]);
                if (fields[3 + fields[0]]) sample("alpha bank", fields[3 + fields[0]]);
            }
        }
        if (args[1] == menu_handle.load()) {
            dump("menu-handle.bin", args[1], 0x38c);
            dump("menu-yuv.bin", fields[1 + (fields[0] & 1)], 640 * 480 * 3 / 2);
        }
    }
    bf_original_bink_copy();
    if (trace) { std::fprintf(stderr, "[BINK] return=%08X\n", g_eax); sample("output", args[2]); }
}
