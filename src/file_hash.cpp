#include "file_hash.h"
#include <array>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

std::string sha256_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    std::array<unsigned char, 32> digest{}; // SHA-256 emits exactly 32 bytes.
    std::array<unsigned char, 65536> buffer{}; // Bounded 64-KiB streaming buffer.
#ifdef _WIN32
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        ~Hash() {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } state;
    if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(state.algorithm, &state.hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("Cannot initialize SHA-256");
#else
    struct Hash {
        EVP_MD_CTX *context = EVP_MD_CTX_new();
        ~Hash() { EVP_MD_CTX_free(context); }
    } state;
    if (!state.context || EVP_DigestInit_ex(state.context, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot initialize SHA-256");
#endif
    while (input) {
        input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        const auto count = input.gcount();
        if (count <= 0) break;
#ifdef _WIN32
        if (BCryptHashData(state.hash, buffer.data(), static_cast<ULONG>(count), 0) < 0)
#else
        if (EVP_DigestUpdate(state.context, buffer.data(), static_cast<size_t>(count)) != 1)
#endif
            throw std::runtime_error("Cannot update SHA-256");
    }
    if (input.bad()) throw std::runtime_error("Read failure: " + path.string());
#ifdef _WIN32
    if (BCryptFinishHash(state.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("Cannot finish SHA-256");
#else
    unsigned length = 0;
    if (EVP_DigestFinal_ex(state.context, digest.data(), &length) != 1 || length != digest.size())
        throw std::runtime_error("Cannot finish SHA-256");
#endif
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}
