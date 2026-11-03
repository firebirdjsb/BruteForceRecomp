#include "library.h"
#include "manifest.h"
#include "process.h"
#include "xr_sha256.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace bf {
Library::Library(std::filesystem::path workspace) : root_(std::filesystem::absolute(std::move(workspace))) {
    std::filesystem::create_directories(root_);
    std::ifstream settings(root_ / "launcher.settings");
    std::string value;
    if (settings >> std::quoted(value)) executable_ = text_path(value);
    std::string backend; bool fsr = false;
    if (settings >> backend >> fsr) {
        if (backend == "auto" || backend == "dx12" || backend == "dx11" || backend == "gdi") {graphics_.backend=backend;graphics_.fsr1=fsr;}
    }
    graphics_=load_graphics(root_/"graphics.settings",graphics_);
    if (std::filesystem::exists(game_directory() / "default.xbe")) verify();
}
Library::~Library() { cancel(); join(); }
void Library::join() { if (worker_.joinable()) worker_.join(); }
void Library::cancel() { cancelled_.store(true); }
LibraryState Library::state() const { std::lock_guard guard(mutex_); return state_; }
std::filesystem::path Library::executable() const { std::lock_guard guard(mutex_); return executable_; }
void Library::save_settings() {
    auto temporary = root_ / "launcher.settings.new";
    std::ofstream output(temporary, std::ios::trunc);
    output << std::quoted(path_text(executable_)) << '\n' << graphics_.backend << ' ' << graphics_.fsr1 << '\n';
    output.close();
    if (!output) throw std::runtime_error("Cannot save the launcher's settings.");
    // rename on Windows cannot replace an existing file; it contains only
    // our executable path, never game progress or imported content.
    std::error_code error;
    std::filesystem::remove(root_ / "launcher.settings", error);
    std::filesystem::rename(temporary, root_ / "launcher.settings");
    save_graphics(root_/"graphics.settings",graphics_);
}
void Library::set_executable(std::filesystem::path path) {
    if (state().busy) return;
    std::lock_guard guard(mutex_);
    executable_ = std::filesystem::absolute(std::move(path));
    save_settings();
}
GraphicsOptions Library::graphics() const { std::lock_guard guard(mutex_); return graphics_; }
void Library::set_graphics(GraphicsOptions options) {
    if (state().busy) return;
    if (!valid_graphics(options))
        throw std::runtime_error("Unknown graphics backend");
    std::lock_guard guard(mutex_);
    graphics_ = std::move(options); save_settings();
}
bool Library::begin(const char *phase, const char *message) {
    if (state().busy) return false;
    join(); cancelled_.store(false);
    std::lock_guard guard(mutex_);
    state_.busy = true; state_.phase = phase; state_.message = message;
    state_.error.clear(); state_.file.clear(); state_.fraction = 0;
    return true;
}
void Library::fail(const std::string &message) {
    std::lock_guard guard(mutex_);
    state_.phase = "error"; state_.busy = state_.ready = state_.running = false;
    state_.error = message; state_.message = "Operation stopped. Your original disc and saves were not changed.";
}
static std::string file_sha256(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Missing required game file: " + path.filename().string());
    xr_sha256 hash; xr_sha256_init(&hash);
    std::array<char, 65536> buffer;
    while (input) { input.read(buffer.data(), buffer.size()); xr_sha256_update(&hash, buffer.data(), size_t(input.gcount())); }
    if (input.bad()) throw std::runtime_error("Cannot read required game file.");
    uint8_t digest[32]; char hex[65]; xr_sha256_final(&hash, digest); xr_sha256_hex(digest, hex);
    return hex;
}
void Library::validate_files() {
    xr_install_marker marker{};
    if (!xr_install_marker_read(path_text(game_directory()).c_str(), &marker) ||
        std::string(marker.manifest) != bf_disc_manifest.id || marker.files != bf_disc_manifest.count)
        throw std::runtime_error("Disc import is incomplete. Select the ISO again to resume and verify it.");
    for (unsigned i = 0; i < bf_disc_manifest.count; ++i) {
        const auto &file = bf_disc_manifest.files[i];
        const std::string name(file.path);
        if (name != "default.xbe" && name != "data/common.tgz" && name != "data/tutorial.tgz") continue;
        const auto path = game_directory() / text_path(name);
        if (std::filesystem::file_size(path) != file.size || file_sha256(path) != file.sha256)
            throw std::runtime_error("Required file is damaged: " + name + ". Import your ISO again.");
    }
}
void Library::import_disc(std::filesystem::path iso) {
    if (!begin("importing", "Reading your disc. Each imported file is checked against the supported build.")) return;
    worker_ = std::thread([this, iso = std::move(iso)] {
        try {
            const auto source = path_text(std::filesystem::absolute(iso)), destination = path_text(game_directory());
            xr_install_opts options{};
            options.source = source.c_str(); options.source_fd = -1; options.dest = destination.c_str();
            options.manifest = &bf_disc_manifest; options.version = "0.1-native-launcher";
            options.free_needed = xr_manifest_bytes(&bf_disc_manifest); options.ctx = this;
            options.progress = [](void *context, double fraction, const char *text) {
                auto &self = *static_cast<Library *>(context); std::lock_guard guard(self.mutex_);
                self.state_.fraction = float(std::clamp(fraction, 0.0, 1.0));
                self.state_.file = text ? text : "";
            };
            options.cancelled = [](void *context) { return static_cast<Library *>(context)->cancelled_.load() ? 1 : 0; };
            xr_install_result result{};
            if (xr_install(&options, &result) != XR_INSTALL_OK) throw std::runtime_error(result.message);
            validate_files();
            std::lock_guard guard(mutex_);
            state_.phase = "ready"; state_.busy = false; state_.ready = true; state_.fraction = 1;
            state_.file.clear(); state_.message = std::to_string(bf_disc_manifest.count) + " game files imported and verified. Demo products were excluded.";
        } catch (const std::exception &error) { fail(error.what()); }
    });
}
void Library::verify() {
    if (!begin("verifying", "Checking your imported disc and required game files.")) return;
    worker_ = std::thread([this] {
        try {
            validate_files();
            std::lock_guard guard(mutex_);
            state_.phase = "ready"; state_.busy = false; state_.ready = true; state_.fraction = 1;
            state_.message = "Local game files verified.";
        } catch (const std::exception &error) { fail(error.what()); }
    });
}
void Library::check_host(const std::filesystem::path &exe) {
    const auto result = run_process(exe, {"--validate-only", path_text(game_directory())}, root_, root_ / "validate.log");
    if (result.code) throw std::runtime_error("Native file validation failed: " + result.detail);
}
void Library::launch(bool keyboard) {
    if (!state().ready || !begin("launching", "Validating your native build before deployment.")) return;
    const auto exe = executable();
    const auto options = graphics();
    worker_ = std::thread([this, exe, keyboard, options] {
        try {
            validate_files(); check_host(exe);
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            const auto session = root_ / "sessions" / std::to_string(stamp), saves = root_ / "player-data";
            std::filesystem::create_directories(saves);
            if(options.scene!="cpu") {
                const auto capability=run_process(exe,{"--capabilities"},session,session/"capabilities.log");
                if(capability.code || capability.detail.find("nv2a_"+options.scene+"=1")==std::string::npos)
                    throw std::runtime_error("This native build lacks the selected GPU scene backend. Select CPU reference or locate a build with that GPU scene backend enabled.");
                if(options.frame_limit==60 && capability.detail.find("host_fps60=1")==std::string::npos)
                    throw std::runtime_error("This game application predates the 60 FPS option. Locate the updated native game or select Original (30 FPS).");
            }
            const auto result = run_process(exe, {path_text(game_directory()), path_text(saves)}, session, session / "game.log", saves, keyboard, options,root_/"graphics.settings",[this,session] {
                std::lock_guard guard(mutex_);state_.running=true;state_.phase="running";
                state_.message="Native game running. Close its window to return to your squad.";state_.log=path_text(session/"game.log");
            });
            {std::lock_guard guard(mutex_);graphics_=load_graphics(root_/"graphics.settings",graphics_);}
            if (result.code || result.detail.find("[EXIT] HalReturnToFirmware") != std::string::npos)
                throw std::runtime_error("Native host stopped (code " + std::to_string(result.code) + ").\n" + result.detail);
            std::lock_guard guard(mutex_); state_.running = state_.busy = false; state_.phase = "ready";
            state_.message = "Game closed. Ready for another deployment.";
        } catch (const std::exception &error) { fail(error.what()); }
    });
}
std::filesystem::path find_game_executable(const std::filesystem::path &application) {
    auto folder = application;
    // SDL's application path ends in a separator. That adds an empty final
    // component, and a fixed six-step limit stopped before the project root.
    // Walk the finite ancestor chain instead, checking only known filenames.
    for (;;) {
        for (const char *relative : {"bruteforce_recomp.exe", "bruteforce_recomp", "work/diagnostic-build/Release/bruteforce_recomp.exe", ".local/game-build/Release/bruteforce_recomp.exe", ".local/game-build/bruteforce_recomp", "work/diagnostic-build/Debug/bruteforce_recomp.exe"}) {
            const auto candidate = folder / relative;
            if (std::filesystem::is_regular_file(candidate)) return std::filesystem::absolute(candidate);
        }
        const auto parent = folder.parent_path(); if (parent == folder) break; folder = parent;
    }
    return {};
}
}
