#pragma once
#include "process.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
namespace bf {
struct LibraryState {
    std::string phase = "empty", message = "Select your original Brute Force disc image to begin.", file, error, log;
    float fraction = 0;
    bool busy = false, ready = false, running = false;
};
class Library {
public:
    explicit Library(std::filesystem::path workspace);
    ~Library();
    LibraryState state() const;
    const std::filesystem::path &workspace() const { return root_; }
    std::filesystem::path game_directory() const { return root_/"game-core"; }
    std::filesystem::path executable() const;
    void set_executable(std::filesystem::path path);
    GraphicsOptions graphics() const;
    void set_graphics(GraphicsOptions options);
    void import_disc(std::filesystem::path iso);
    void verify();
    void launch(bool keyboard);
    void cancel();
    void join();
private:
    bool begin(const char *phase, const char *message);
    void fail(const std::string &message);
    void validate_files();
    void save_settings();
    void check_host(const std::filesystem::path &exe);
    std::filesystem::path root_, executable_;
    GraphicsOptions graphics_;
    mutable std::mutex mutex_;
    LibraryState state_;
    std::thread worker_;
    std::atomic_bool cancelled_{false};
};
std::filesystem::path find_game_executable(const std::filesystem::path &application);
}
