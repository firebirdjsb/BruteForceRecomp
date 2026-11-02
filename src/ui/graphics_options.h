#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace bf {
// Shared user preferences, never game data. Scene/device changes apply at boot.
struct GraphicsOptions {
    std::string backend = "auto", scene = "cpu";
    bool fsr1 = false, gpu_programs = true;
    unsigned scale = 0, height = 0;
    bool mouse_menus = true, prompts = true, muted = false;
    int volume = 100;
};
inline bool valid_graphics(const GraphicsOptions &o) {
    return (o.backend=="auto"||o.backend=="dx12"||o.backend=="dx11"||o.backend=="gdi") &&
        (o.scene=="cpu"||o.scene=="d3d11") &&
        (o.scale==0||o.scale==2||o.scale==4||o.scale==8) &&
        (o.height==0||o.height==720||o.height==1080||o.height==1440||o.height==2160) &&
        o.volume>=0 && o.volume<=100;
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
    return result;
}
inline void save_graphics(const std::filesystem::path &path,const GraphicsOptions &o) {
    if(!valid_graphics(o))throw std::runtime_error("Invalid graphics settings");
    auto temporary=path;temporary+=".new";
    std::ofstream output(temporary,std::ios::trunc);
    output<<"BF_GRAPHICS 1\n"<<o.backend<<' '<<o.scene<<' '<<o.fsr1<<' '<<o.gpu_programs<<' '
          <<o.scale<<' '<<o.height<<' '<<o.mouse_menus<<' '<<o.prompts<<' '<<o.muted<<' '<<o.volume<<'\n';
    output.close();
    if(!output)throw std::runtime_error("Cannot write graphics settings");
    // Windows rename cannot replace a file. Remove only this preference file,
    // after its complete replacement has been written successfully.
    std::error_code error;
    std::filesystem::remove(path,error);
    std::filesystem::rename(temporary,path);
}
}
