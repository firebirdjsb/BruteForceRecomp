#pragma once
#include <filesystem>
#include <functional>
#include <span>
#include <cstdint>
#include <string>
namespace bf {
struct ContentFile {const char *path;uint64_t size;const char *sha256;};
struct ContentManifest {const char *id;std::span<const ContentFile> files;};
extern const ContentManifest dlc_manifest;
// Only allowlisted package directories are published, after full SHA-256
// verification. Existing differing packages are retained and reported.
unsigned ensure_content_pack(const std::filesystem::path &bundle,const std::filesystem::path &destination,
    const ContentManifest &manifest,const std::function<bool()> &cancelled={});
std::string content_sha256(const std::filesystem::path &file);
}
