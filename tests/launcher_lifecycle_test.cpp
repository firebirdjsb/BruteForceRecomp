#include "../native_launcher/process.h"
#include "../native_launcher/window_lifecycle.h"
#include "../native_launcher/library.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif

static bool window_visible(SDL_Window *window) {
#ifdef _WIN32
    if(std::string(SDL_GetCurrentVideoDriver())=="windows") {
        SDL_SysWMinfo info{};SDL_VERSION(&info.version);
        if(!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error(SDL_GetError());
        return IsWindowVisible(info.info.win.window)!=FALSE;
    }
#endif
    return (SDL_GetWindowFlags(window)&SDL_WINDOW_SHOWN)!=0;
}

int main(int argc,char **argv) try {
    if(argc==3&&std::string(argv[1])=="--child") {
        for(const char *name:{"RECOMP_GPU","RECOMP_GPU_VSH","RECOMP_FLIP_PACING","BF_RENDER_BACKEND","BF_UPSCALER","BF_UI_SETTINGS","RECOMP_KEYBOARD","RECOMP_MKB","RECOMP_ASYNC_IO"})
            std::cout<<name<<'='<<(std::getenv(name)?std::getenv(name):"")<<'\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(250));return std::atoi(argv[2]);
    }
    const auto root=std::filesystem::temp_directory_path()/
        ("bf-launch-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    bf::GraphicsOptions options;options.scene="d3d11";options.backend="dx12";options.fsr1=true;
    options.gpu_programs=false;options.scale=4;options.height=1440;options.volume=70;options.interpolation=120;
    const auto settings=root/"graphics.settings";
    bf::save_graphics(settings,options);const auto loaded=bf::load_graphics(settings);
    if(loaded.scene!="d3d11"||loaded.gpu_programs||loaded.scale!=4||loaded.height!=1440||loaded.volume!=70||loaded.frame_limit!=30||loaded.interpolation!=120)return 1;
    {std::ofstream invalid(settings);invalid<<"BF_GRAPHICS 1\ninvalid data\n";}
    if(bf::load_graphics(settings).scene!="cpu")return 2;
    bf::save_graphics(settings,options);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
    SDL_Window *window=SDL_CreateWindow("Launcher lifecycle regression",0,0,320,240,SDL_WINDOW_HIDDEN);
    if(!window)throw std::runtime_error(SDL_GetError());
    // Hide at Start, while validation still runs, and restore after a failure
    // that happens before a child process exists. Other busy operations stay visible.
    SDL_ShowWindow(window);bool preflight_hidden=false;
    bf::LibraryState preflight;preflight.busy=true;preflight.phase="launching";
    bf::sync_game_window(window,preflight.game_session_active(),preflight_hidden);
    if(!preflight_hidden||window_visible(window))return 8;
    preflight.phase="error";preflight.busy=false;
    bf::sync_game_window(window,preflight.game_session_active(),preflight_hidden);
    if(preflight_hidden||!window_visible(window))return 9;
    preflight.phase="verifying";preflight.busy=true;
    if(preflight.game_session_active())return 11;
    preflight.phase="importing";if(preflight.game_session_active())return 12;
    const auto exe=std::filesystem::absolute(bf::text_path(argv[0]));
    for(const int exit_code:{0,7}) {
        options.scene=exit_code==0?"d3d11":"d3d12";
        bf::save_graphics(settings,options);
        if(bf::load_graphics(settings).scene!=options.scene)return 7;
        SDL_ShowWindow(window);bool hidden=false,saw_hidden=false;
        std::atomic_bool running=false,finished=false;bf::ProcessResult result{};std::exception_ptr failure;
        std::thread child([&] {
            try {result=bf::run_process(exe,{"--child",std::to_string(exit_code)},root,root/"child.log",root/"saves",true,options,settings,[&]{running=true;});}
            catch(...){failure=std::current_exception();}
            running=false;finished=true;
        });
        while(!finished) {
            SDL_PumpEvents();bf::sync_game_window(window,running,hidden);
            if(hidden&&!window_visible(window))saw_hidden=true;
            SDL_Delay(5);
        }
        child.join();bf::sync_game_window(window,false,hidden);
        if(failure)std::rethrow_exception(failure);
        if(result.code!=exit_code||!saw_hidden||hidden||!window_visible(window))return 3;
        for(const std::string &text:std::vector<std::string>{"RECOMP_GPU="+options.scene,"RECOMP_GPU_VSH=0","RECOMP_FLIP_PACING=edge","BF_RENDER_BACKEND=dx12","BF_UPSCALER=fsr1","BF_UI_SETTINGS="+bf::path_text(settings),"RECOMP_KEYBOARD=1","RECOMP_MKB=1","RECOMP_ASYNC_IO=1"})
            if(result.detail.find(text)==std::string::npos)return 4;
    }
    bool started=false;
    try {bf::run_process(root/"absent-native-game.exe",{},root,root/"absent.log",{},false,{}, {},[&]{started=true;});return 5;}
    catch(const std::runtime_error &) {if(started)return 6;}
    std::printf("Window lifecycle verified with SDL driver: %s\n",SDL_GetCurrentVideoDriver());
    SDL_DestroyWindow(window);SDL_Quit();
    // Only the files/directories created above are removed, never player data.
    std::filesystem::remove(root/"child.log");std::filesystem::remove(settings);std::filesystem::remove(root);
    std::puts("Settings roundtrip, child environment, failed start, and launcher hide/restore after normal and failed exits passed.");
    return 0;
} catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 10;}
