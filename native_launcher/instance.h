#pragma once
#include <cstdint>
#include <filesystem>

namespace bf {
// One interactive launcher per library, regardless of which installed copy
// starts it. The OS releases the lock on normal exit and process failure.
class LauncherInstance {
public:
    explicit LauncherInstance(const std::filesystem::path &workspace);
    ~LauncherInstance();
    LauncherInstance(const LauncherInstance &)=delete;
    LauncherInstance &operator=(const LauncherInstance &)=delete;
    bool owns_library() const noexcept { return owner_; }
    void activate_existing() const noexcept;
private:
    void close() noexcept;
    bool owner_{};
    uint32_t owner_pid_{};
#ifdef _WIN32
    void *handle_{};
#else
    int descriptor_{-1};
#endif
};
}
