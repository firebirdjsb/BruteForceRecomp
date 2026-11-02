#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include "../src/ui/graphics_options.h"
namespace bf {
struct ProcessResult { int code; std::string detail; };
// Launch only the explicitly selected native host, with an argument vector.
// No shell, command interpolation, external process injection or game-disc executable.
ProcessResult run_process(const std::filesystem::path &exe, const std::vector<std::string> &arguments,
                          const std::filesystem::path &directory, const std::filesystem::path &log,
                          const std::filesystem::path &saves = {}, bool keyboard = false,
                          const GraphicsOptions &graphics = {},
                          const std::filesystem::path &settings = {},
                          const std::function<void()> &started = {});
std::string read_tail(const std::filesystem::path &path, size_t limit = 3000);
std::string path_text(const std::filesystem::path &path);
std::filesystem::path text_path(const std::string &text);
}
