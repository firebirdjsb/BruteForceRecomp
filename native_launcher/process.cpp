#include "process.h"
#include <fstream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace bf {
std::string path_text(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char *>(value.data()), value.size());
}
std::filesystem::path text_path(const std::string &text) { return std::filesystem::u8path(text); }
std::string read_tail(const std::filesystem::path &path, size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff size = file.tellg();
    if (size <= 0) return {};
    file.seekg(size > std::streamoff(limit) ? size - std::streamoff(limit) : std::streamoff(0));
    return std::string(std::istreambuf_iterator<char>(file), {});
}
static const char *removed[] = {"RECOMP_WATCHDOG_SECS", "RECOMP_PAD_SCRIPT", "BF_SAMPLE_BOOT", "BF_TRACE_DELAY", "RECOMP_KEYBOARD", "RECOMP_MKB"};
#ifdef _WIN32
static std::wstring wide(const std::string &text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 process argument");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count);
    return result;
}
static std::wstring quote(const std::wstring &text) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto ch : text) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0;
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif
ProcessResult run_process(const std::filesystem::path &exe, const std::vector<std::string> &arguments,
                          const std::filesystem::path &directory, const std::filesystem::path &log,
                          const std::filesystem::path &saves, bool keyboard, const GraphicsOptions &graphics) {
    if (!std::filesystem::is_regular_file(exe)) throw std::runtime_error("The native game executable was not found.");
    std::filesystem::create_directories(directory);
#ifdef _WIN32
    std::wstring command = quote(exe.wstring());
    for (const auto &argument : arguments) command += L" " + quote(wide(argument));
    std::map<std::wstring, std::wstring> values;
    if (auto *environment = GetEnvironmentStringsW()) {
        for (auto *item = environment; *item; item += wcslen(item) + 1) {
            std::wstring entry(item);
            const auto equal = entry.find(L'=', entry[0] == L'=' ? 1 : 0);
            if (equal != std::wstring::npos) values[entry.substr(0, equal)] = entry.substr(equal + 1);
        }
        FreeEnvironmentStringsW(environment);
    }
    for (const auto *name : removed) values.erase(wide(name));
    if (!saves.empty()) {
        values[L"RECOMP_SAVE_DIR"] = saves.wstring();
        values[L"BF_RENDER_BACKEND"] = wide(graphics.backend);
        values[L"BF_UPSCALER"] = graphics.fsr1 ? L"fsr1" : L"off";
        for (const auto *name : {"RECOMP_USB", "RECOMP_PB_EXEC", "RECOMP_FB_WINDOW", "RECOMP_VBLANK", "RECOMP_TITLE_KEVENTS", "RECOMP_UNIMPL_TRAP"}) values[wide(name)] = L"1";
        if (keyboard) values[L"RECOMP_KEYBOARD"] = L"1";
    }
    std::vector<wchar_t> block;
    for (const auto &[name, value] : values) {
        const auto entry = name + L'=' + value;
        block.insert(block.end(), entry.begin(), entry.end()); block.push_back(0);
    }
    block.push_back(0);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open the game session log.");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = output; startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, block.data(), directory.c_str(), &startup, &process);
    const auto error = GetLastError();
    CloseHandle(output);
    if (!started) throw std::runtime_error("Cannot start the native host (Windows error " + std::to_string(error) + ").");
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(process.hProcess, &code); CloseHandle(process.hProcess);
    return {int(code), read_tail(log)};
#else
    std::map<std::string, std::string> values;
    for (auto **item = environ; *item; ++item) {
        std::string entry(*item); const auto equal = entry.find('=');
        if (equal != std::string::npos) values[entry.substr(0, equal)] = entry.substr(equal + 1);
    }
    for (const auto *name : removed) values.erase(name);
    if (!saves.empty()) {
        values["RECOMP_SAVE_DIR"] = path_text(saves);
        for (const auto *name : {"RECOMP_USB", "RECOMP_PB_EXEC", "RECOMP_FB_WINDOW", "RECOMP_VBLANK", "RECOMP_TITLE_KEVENTS", "RECOMP_UNIMPL_TRAP"}) values[name] = "1";
        if (keyboard) values["RECOMP_KEYBOARD"] = "1";
    }
    std::vector<std::string> environment;
    for (const auto &[name, value] : values) environment.push_back(name + '=' + value);
    std::vector<char *> env;
    for (auto &value : environment) env.push_back(value.data()); env.push_back(nullptr);
    std::vector<std::string> storage{path_text(exe)};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char *> args;
    for (auto &value : storage) args.push_back(value.data()); args.push_back(nullptr);
    const auto executable = path_text(exe), cwd = path_text(directory);
    const int output = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output < 0) throw std::runtime_error("Cannot open the game session log.");
    const pid_t pid = fork();
    if (pid == 0) {
        // Only async-signal-safe operations after fork; SDL owns other threads.
        if (chdir(cwd.c_str()) || dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) _exit(126);
        close(output); execve(executable.c_str(), args.data(), env.data()); _exit(127);
    }
    close(output);
    if (pid < 0) throw std::runtime_error("Cannot create the native host process.");
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) throw std::runtime_error("Cannot wait for the native host.");
    return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), read_tail(log)};
#endif
}
}
