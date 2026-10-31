// User-approved replacement of the optical-media identity service only.
#include "native_media.h"
#include "file_hash.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <atomic>
extern "C" {
#include "xbox/xboxrecomp.h"
void bf_original_media_002F801F(void);
#ifdef _MSC_VER
extern __declspec(thread) uint32_t g_eax, g_esp;
#else
extern __thread uint32_t g_eax, g_esp;
#endif
}
static bool verified;
static bool original_dvd;
static std::atomic_bool reported{false};

bool native_media_initialize(const std::filesystem::path &game) {
    struct Required { const char *path; uintmax_t bytes; const char *sha256; };
    // Identity and two required asset checks from the supplied matching ISO.
    // These do not claim a checksum audit of every level, video or sound file.
    static constexpr Required required[] = {
        {"default.xbe", 4366336, "b6ea5d4dd6500ae86c3f5e3e4618bc9b1e3facddcb483170dfee7a787087c721"},
        {"data/common.tgz", 4698830, "e678e27b47788123ff97837521400eb17d0cf3e28ca21d165d803d873ef367b9"},
        {"data/tutorial.tgz", 9619648, "0dfbc6c8e245bc6f41dfc75e0bae42c4c2747453949c44471294043c10c7642b"}
    };
    try {
        verified = false;
        for (const auto &file : required) {
            const auto path = game / file.path;
            if (std::filesystem::file_size(path) != file.bytes || sha256_file(path) != file.sha256) {
                std::fprintf(stderr, "[MEDIA] Unsupported or damaged file: %s\n", file.path);
                return false;
            }
        }
        verified = true;
        const char *mode = std::getenv("BF_ORIGINAL_DVD");
        original_dvd = mode && *mode && *mode != '0';
        std::fprintf(stderr, "[MEDIA] XBE and two required assets verified; %s media service.\n",
                     original_dvd ? "original DVD" : "native file");
        return true;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[MEDIA] Validation failed: %s\n", error.what());
        return false;
    }
}

extern "C" void sub_002F801F(void) {
    if (original_dvd) { bf_original_media_002F801F(); return; }
    if (uint64_t(g_esp) + 4 > g_xbox_total_ram) {
        std::fprintf(stderr, "[MEDIA] Invalid guest return slot\n");
        std::exit(24);
    }
    // Exact original helper body SHA-256:
    // f44421e4a03680a1170b1f18f84cb281ad2a3878831cc18e5fda23aa0c15cad2.
    // Its caller consumes EAX as NTSTATUS; original RET at guest 0x002F81E7
    // has no arguments to pop. Preserve all other guest registers and consume
    // only the 4-byte guest return address. This service validates host files,
    // and does not pretend to reproduce an Xbox DVD security-sector exchange.
    g_eax = verified ? 0u : 0xC0000190u;
    g_esp += 4;
    if (!reported.exchange(true, std::memory_order_relaxed))
        std::fprintf(stderr, "[MEDIA] Native local-file status: 0x%08X\n", g_eax);
}
