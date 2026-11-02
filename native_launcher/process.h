#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace bf {
struct GraphicsOptions { std::string backend = "auto"; bool fsr1 = false; };
struct ProcessResult { int code; std::string detail; };
// Launch only the explicitly selected native host, with an argument vector.
// No shell, command interpolation, external process injection or game-disc executable.
ProcessResult run_process(const std::filesystem::path &exe, const std::vector<std::string> &arguments,
                          const std::filesystem::path &directory, const std::filesystem::path &log,
                          const std::filesystem::path &saves = {}, bool keyboard = false,
                          const GraphicsOptions &graphics = {});
std::string read_tail(const std::filesystem::path &path, size_t limit = 3000);
std::string path_text(const std::filesystem::path &path);
std::filesystem::path text_path(const std::string &text);
}
