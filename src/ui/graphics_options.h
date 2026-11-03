#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cmath>

namespace bf {
// Shared user preferences, never game data. Scene/device changes apply at boot.
struct GraphicsOptions {
    std::string backend = "auto", scene = "cpu";
    bool fsr1 = false, gpu_programs = true;
    unsigned scale = 0, height = 0;
    bool mouse_menus = true, prompts = true, muted = false;
    int volume = 100;
    float mouse_sensitivity = 1.0f;
    unsigned frame_limit = 30;
};
inline bool valid_graphics(const GraphicsOptions &o) {
    return (o.backend=="auto"||o.backend=="dx12"||o.backend=="dx11"||o.backend=="gdi") &&
        (o.scene=="cpu"||o.scene=="d3d11"||o.scene=="d3d12") &&
        (o.scale==0||o.scale==2||o.scale==4||o.scale==8) &&
        (o.height==0||o.height==720||o.height==1080||o.height==1440||o.height==2160) &&
        o.volume>=0 && o.volume<=100 && std::isfinite(o.mouse_sensitivity) &&
        o.mouse_sensitivity>=0.1f && o.mouse_sensitivity<=10.0f &&
        (o.frame_limit==30 || o.frame_limit==60);
}
inline GraphicsOptions load_graphics(const std::filesystem::path &path, GraphicsOptions fallback={}) {
    std::ifstream input(path);
    if(!input)return fallback;
    GraphicsOptions result;
    std::string marker;
    int version{};
    if(!(input>>marker>>version) || marker!="BF_GRAPHICS" || version!=1 ||
       !(input>>result.backend>>result.scene>>result.fsr1>>result.gpu_programs>>result.scale>>result.height
         >>result.mouse_menus>>result.prompts>>result.muted>>result.volume) || !valid_graphics(result))
        return fallback;
    // Older version-1 files end after volume. Preserve those preferences.
    input >> std::ws;
    if(input.peek()!=std::char_traits<char>::eof() &&
       (!(input>>result.mouse_sensitivity)||!valid_graphics(result)))return fallback;
    // Existing files end after volume or sensitivity. Higher FPS is opt-in.
    input >> std::ws;
    if(input.peek()!=std::char_traits<char>::eof() &&
       (!(input>>result.frame_limit)||!valid_graphics(result)))return fallback;
    return result;
}
inline void save_graphics(const std::filesystem::path &path,const GraphicsOptions &o) {
    if(!valid_graphics(o))throw std::runtime_error("Invalid graphics settings");
    auto temporary=path;temporary+=".new";
    std::ofstream output(temporary,std::ios::trunc);
    output<<"BF_GRAPHICS 1\n"<<o.backend<<' '<<o.scene<<' '<<o.fsr1<<' '<<o.gpu_programs<<' '
          <<o.scale<<' '<<o.height<<' '<<o.mouse_menus<<' '<<o.prompts<<' '<<o.muted<<' '<<o.volume
          <<' '<<o.mouse_sensitivity<<' '<<o.frame_limit<<'\n';
    output.close();
    if(!output)throw std::runtime_error("Cannot write graphics settings");
    // Windows rename cannot replace a file. Remove only this preference file,
    // after its complete replacement has been written successfully.
    std::error_code error;
    std::filesystem::remove(path,error);
    std::filesystem::rename(temporary,path);
}
}
