#include "app.h"
#include "process.h"
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>

int main(int argc,char **argv) {
    try {
        const char *base=SDL_GetBasePath();
        const auto application=base?bf::text_path(base):std::filesystem::current_path();
        if(base)SDL_free(const_cast<char *>(base));
        std::filesystem::path workspace,exe,import;
        bool verify_only=false;
        for(int i=1;i<argc;++i){const std::string_view arg(argv[i]);
            if((arg=="--workspace" || arg=="--game-exe" || arg=="--import-only") && i+1<argc){
                const auto value=std::filesystem::absolute(bf::text_path(argv[++i]));
                if(arg=="--workspace")workspace=value;else if(arg=="--game-exe")exe=value;else import=value;
            }else if(arg=="--verify-only")verify_only=true;
            else throw std::runtime_error("Usage: BruteForceLauncher [--workspace FOLDER] [--game-exe FILE] [--import-only ISO] [--verify-only]");
        }
        if(workspace.empty()){
            char *path=SDL_GetPrefPath("Firebird","BruteForceRecomp");
            if(!path)throw std::runtime_error("Cannot locate the application's local data directory.");
            workspace=bf::text_path(path);SDL_free(path);
        }
        bf::Library library(workspace);
        library.join();
        if(exe.empty() && library.executable().empty())exe=bf::find_game_executable(application);
        if(!exe.empty())library.set_executable(exe);
        if(!import.empty()){library.import_disc(import);library.join();const auto state=library.state();std::fprintf(stderr,"%s\n%s\n",state.message.c_str(),state.error.c_str());return state.ready?0:1;}
        if(verify_only){library.verify();library.join();const auto state=library.state();std::fprintf(stderr,"%s\n%s\n",state.message.c_str(),state.error.c_str());return state.ready?0:1;}
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER)!=0)throw std::runtime_error(SDL_GetError());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
        auto *window=SDL_CreateWindow("Brute Force Recomp - Command Center",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1240,820,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(!window)throw std::runtime_error(SDL_GetError());
        if(auto *icon=SDL_LoadBMP(bf::path_text(application/"assets"/"title.bmp").c_str())){SDL_SetWindowIcon(window,icon);SDL_FreeSurface(icon);}
        SDL_SetWindowMinimumSize(window,970,730);
        auto *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
        if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer)throw std::runtime_error(SDL_GetError());
        IMGUI_CHECKVERSION();ImGui::CreateContext();
        auto &io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.IniFilename=nullptr;
        ImGui_ImplSDL2_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer2_Init(renderer);
        {
            bf::Theme theme;
            if(!theme.load(renderer,bf::path_text(application/"assets").c_str()))throw std::runtime_error("Launcher fonts/textures could not be loaded. Keep the assets folder beside the application.");
            bf::App app(library,theme);bool running=true;unsigned frame=0;
            while(running){SDL_Event event;
                while(SDL_PollEvent(&event)){
                    ImGui_ImplSDL2_ProcessEvent(&event);
                    if(event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE && event.window.windowID==SDL_GetWindowID(window)))running=!app.can_close();
                    if(event.type==SDL_DROPFILE)SDL_free(event.drop.file);
                }
                ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();app.draw();ImGui::Render();
                if(std::getenv("BF_LAUNCHER_TRACE") && ++frame==120){
                    std::ofstream trace(workspace/"render-diagnostic.txt");
                    auto *data=ImGui::GetDrawData();
                    trace<<"Display "<<data->DisplaySize.x<<' '<<data->DisplaySize.y<<" lists "<<data->CmdListsCount<<'\n';
                    for(int i=0;i<data->CmdListsCount;++i){const auto *list=data->CmdLists[i];
                        trace<<list->_OwnerName<<" vertices "<<list->VtxBuffer.Size<<'\n';
                        for(const auto &cmd:list->CmdBuffer)trace<<cmd.ElemCount<<" clip "<<cmd.ClipRect.x<<' '<<cmd.ClipRect.y<<' '<<cmd.ClipRect.z<<' '<<cmd.ClipRect.w<<'\n';
                    }
                }
                SDL_SetRenderDrawColor(renderer,9,14,20,255);SDL_RenderClear(renderer);
                SDL_ClearError();
                ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer);
                if(std::getenv("BF_LAUNCHER_TRACE") && frame==120){std::ofstream trace(workspace/"render-diagnostic.txt",std::ios::app);trace<<"SDL renderer: "<<SDL_GetError()<<'\n';}
                SDL_RenderPresent(renderer);
                if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED)SDL_Delay(50);
            }
        }
        ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }catch(const std::exception &error){
        std::fprintf(stderr,"Native launcher: %s\n",error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Brute Force launcher",error.what(),nullptr);SDL_Quit();return 1;
    }
}
