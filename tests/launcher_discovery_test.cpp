#include "../native_launcher/library.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

int main() try {
    namespace fs=std::filesystem;
    const auto temporary=fs::canonical(fs::temp_directory_path());
    const auto root=temporary/("bf-discovery-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"project",application=project/"work/native-launcher/build/native_launcher/Release";
#ifdef _WIN32
    const char *name="bruteforce_recomp.exe";
#else
    const char *name="bruteforce_recomp";
#endif
    const auto player=project/
#ifdef _WIN32
        "work/diagnostic-build/Release/bruteforce_recomp.exe";
#else
        ".local/game-build/bruteforce_recomp";
#endif
    fs::create_directories(application);fs::create_directories(player.parent_path());
    {std::ofstream file(player);file<<"Synthetic discovery fixture; never executed.\n";}
    if(bf::find_game_executable(application)!=player)throw std::runtime_error("Player not discovered from native build folder");
    // SDL_GetBasePath always appends a separator. This must not consume the
    // ancestor search limit and prevent reaching the actual project root.
    auto trailing=application;trailing/=fs::path{};
    if(bf::find_game_executable(trailing)!=player)throw std::runtime_error("Trailing separator hides the native player");
    {std::ofstream file(application/name);file<<"Colocated synthetic player.\n";}
    if(bf::find_game_executable(trailing)!=application/name)throw std::runtime_error("Colocated player should take priority");
    fs::remove(application/name);fs::remove(player);
    if(!bf::find_game_executable(trailing).empty())throw std::runtime_error("Missing player was incorrectly reported present");
    // Only this freshly created, uniquely named child of the OS temp folder
    // is eligible for recursive cleanup; no library or save path is involved.
    if(fs::weakly_canonical(root).parent_path()!=temporary)throw std::runtime_error("Unexpected fixture cleanup root");
    fs::remove_all(root);
    std::puts("Native player discovery handles SDL trailing separators, package priority and missing files.");
    return 0;
} catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
