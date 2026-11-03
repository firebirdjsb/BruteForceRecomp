#include "app.h"
#include "process.h"
#include <algorithm>
#include <cstdio>

namespace bf {
App::App(Library &library,Theme &theme):library_(library),theme_(theme){
    std::snprintf(host_,sizeof(host_),"%s",path_text(library_.executable()).c_str());
}
bool App::can_close() {
    if(library_.state().running){show_exit_=true;return false;}
    library_.cancel();return true;
}
static void status_label(const LibraryState &state,ImFont *font,bool has_player) {
    const char *text=state.running?"SQUAD DEPLOYED":state.busy?"OPERATION IN PROGRESS":!state.ready?"AWAITING DISC":has_player?"READY TO DEPLOY":"GAME APPLICATION MISSING";
    ImGui::PushFont(font);ImGui::TextColored(gold,"%s",text);ImGui::PopFont();
}
void App::command() {
    const auto state=library_.state();
    std::error_code host_error;
    const bool has_player=std::filesystem::is_regular_file(library_.executable(),host_error);
    label("YOUR NEXT MISSION",theme_.small);
    ImGui::PushFont(theme_.heading);ImGui::TextUnformatted("Prepare for deployment.");ImGui::PopFont();
    status_label(state,theme_.small,has_player);
    ImGui::Spacing(); ImGui::Separator();ImGui::Spacing();
    label("01  SELECT DISC     /     02  IMPORT & VERIFY     /     03  DEPLOY",theme_.small);
    ImGui::Spacing();label("BRUTE FORCE DISC IMAGE",theme_.small);
    ImGui::BeginDisabled(state.busy);
    ImGui::SetNextItemWidth(std::max(100.f,ImGui::GetContentRegionAvail().x-115));
    ImGui::InputTextWithHint("##iso","Choose your .iso or .xiso file",iso_,sizeof(iso_));
    ImGui::SameLine();if(ImGui::Button("Browse",{105,43})){picking_host_=false;picker_.open("Select your Brute Force disc",true,iso_);}
    ImGui::PushFont(theme_.small);ImGui::TextWrapped("Your original disc is read locally. Imported game files stay on this device.");ImGui::PopFont();
    ImGui::BeginDisabled(iso_[0]==0);
    if(ImGui::Button("Import disc",{155,42})){ui_error_.clear();library_.import_disc(text_path(iso_));}
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Verify files",{145,42})){ui_error_.clear();library_.verify();}
    ImGui::EndDisabled();
    if(state.busy && state.phase=="importing"){ImGui::SameLine();if(ImGui::Button("Cancel"))library_.cancel();}
    ImGui::Spacing();
    ImGui::BeginChild("telemetry",{0,110},ImGuiChildFlags_Borders);
    label(state.phase=="importing"?"EXTRACTING & CHECKING GAME DATA":"MISSION STATUS",theme_.small);
    ImGui::ProgressBar(state.fraction,{ImGui::GetContentRegionAvail().x,4},"");
    ImGui::PushFont(theme_.small);ImGui::TextWrapped("%s",state.message.c_str());
    if(!state.file.empty())ImGui::TextWrapped("%s",state.file.c_str());ImGui::PopFont();
    ImGui::EndChild();
    if(!state.dlc.empty()){ImGui::PushFont(theme_.small);ImGui::TextWrapped("%s",state.dlc.c_str());ImGui::PopFont();}
    if(!state.error.empty() || !ui_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{.95f,.66f,.51f,1});
        ImGui::PushFont(theme_.small);ImGui::TextWrapped("%s",ui_error_.empty()?state.error.c_str():ui_error_.c_str());ImGui::PopFont();ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(state.busy);ImGui::Checkbox("Mouse and keyboard controls",&keyboard_);ImGui::EndDisabled();
    label("WASD = move / mouse = aim / F1 = settings and controls",theme_.small);
    ImGui::Spacing();
    if(ImGui::CollapsingHeader("Graphics")) {
        ImGui::BeginDisabled(state.busy);
        auto options=library_.graphics();
        bool changed=false;
#ifdef _WIN32
        const char *scenes[]={"cpu","d3d11","d3d12"};int scene=0;
        for(int i=0;i<3;++i)if(options.scene==scenes[i])scene=i;
        if(ImGui::Combo("Scene renderer",&scene,"CPU reference\0D3D11 GPU (experimental)\0D3D12 GPU via D3D11On12 (experimental)\0")){options.scene=scenes[scene];changed=true;}
        ImGui::BeginDisabled(scene==0);
        changed|=ImGui::Checkbox("GPU vertex programs",&options.gpu_programs);
        ImGui::EndDisabled();
        static const char *backends[]={"auto","dx12","dx11","gdi"};
        int selected=0;for(int i=0;i<4;++i)if(options.backend==backends[i])selected=i;
        if(ImGui::Combo("Presentation",&selected,"Automatic\0DirectX 12\0DirectX 11\0GDI fallback\0")){
            options.backend=backends[selected];changed=true;
        }
        ImGui::BeginDisabled(selected==2 || selected==3);
        int interpolation=options.interpolation==120?2:options.interpolation==60?1:0;
        if(ImGui::Combo("Visual interpolation",&interpolation,"Off (original 30 FPS)\0 60 FPS visuals (experimental)\0 120 FPS visuals (experimental)\0")) {
            const unsigned rates[]={0,60,120};options.interpolation=rates[interpolation];changed=true;
        }
        changed|=ImGui::Checkbox("AMD FSR 1 upscaling",&options.fsr1);
        const unsigned factors[]={0,2,4,8};int factor=0;
        for(int i=0;i<4;++i)if(options.scale==factors[i])factor=i;
        if(ImGui::Combo("FSR output scale",&factor,"Match window\0 2x\0 4x\0 8x\0")){options.scale=factors[factor];changed=true;}
        ImGui::EndDisabled();
#else
        ImGui::TextUnformatted("CPU scene renderer / native SDL presentation");
#endif
        ImGui::BeginDisabled(options.scene=="cpu");
        int texture=options.texture_scale==4?2:options.texture_scale==2?1:0;
        if(ImGui::Combo("Texture reconstruction",&texture,"Off (original assets)\0 2x (experimental)\0 4x (experimental)\0")) {
            const unsigned values[]={1,2,4};options.texture_scale=values[texture];changed=true;
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("Reconstructs sampled color textures, including UI and cube faces, with original mip levels preserved. Uses up to 256 MiB extra VRAM; depth, movies and render targets retain their original path. This does not create an HD texture pack.");
        changed|=ImGui::Checkbox("Anti-aliasing (Edge AA)",&options.antialiasing);
        ImGui::TextWrapped("Smooths high-contrast edges before upscaling on every presenter. Spatial filtering can soften fine detail; it does not change game timing.");
        const unsigned heights[]={0,720,1080,1440,2160};int resolution=0;
        for(int i=0;i<5;++i)if(options.height==heights[i])resolution=i;
        if(ImGui::Combo("Window resolution",&resolution,"Automatic\0 720p\0 1080p\0 1440p\0 2160p (4K)\0")){options.height=heights[resolution];changed=true;}
        if(changed)library_.set_graphics(options);
        ImGui::PushFont(theme_.small);
        ImGui::TextWrapped("The game keeps its original 30 FPS timing. Visual interpolation generates extra presentation frames on DX12; it adds one source frame of latency. Device changes apply on the next launch.");
        ImGui::TextWrapped("The D3D12 scene option uses Microsoft's D3D11On12 bridge. Presentation is a separate choice below the scene renderer.");
        ImGui::TextWrapped("The original 4:3 picture is preserved. Window size and FSR output scale do not enable native widescreen or increase scene resolution.");
        ImGui::TextWrapped("Interpolation can produce artifacts around occlusion and fast motion. Menus use original frames. Actual output depends on display refresh. This is motion interpolation, separate from temporal FSR/DLSS, which remain unfinished.");
        ImGui::PopFont();
        ImGui::EndDisabled();
    }
    if(ImGui::CollapsingHeader("Input & audio")) {
        ImGui::BeginDisabled(state.busy);auto options=library_.graphics();bool changed=false;
        changed|=ImGui::SliderFloat("Mouse sensitivity",&options.mouse_sensitivity,0.1f,10.0f,"%.2fx");
        ImGui::TextWrapped("F1 > Controls has the live mouse sensitivity slider and keyboard/mouse guide.");
        changed|=ImGui::Checkbox("Mouse menu selection",&options.mouse_menus);
        changed|=ImGui::Checkbox("Keyboard / mouse button prompts",&options.prompts);
        changed|=ImGui::Checkbox("Mute audio",&options.muted);
        changed|=ImGui::SliderInt("Master volume",&options.volume,0,100,"%d%%");
        if(changed)library_.set_graphics(options);ImGui::EndDisabled();
    }
    if(state.ready&&!state.busy&&!has_player) {
        ImGui::PushFont(theme_.small);
        ImGui::TextColored(gold,"Game files verified. The game application is missing.");
        ImGui::TextWrapped("Choose bruteforce_recomp.exe using Locate below to enable Start.");
        ImGui::PopFont();
        if(ImGui::Button("Locate game application")){picking_host_=true;picker_.open("Select the game application",false,host_);}
    }
    ImGui::BeginDisabled(state.busy || !state.ready || !has_player);
    ImGui::PushStyleColor(ImGuiCol_Button,gold);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4{.90f,.82f,.56f,1});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4{.68f,.60f,.37f,1});
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{.07f,.12f,.16f,1});
    ImGui::PushFont(theme_.heading);
    if(ImGui::Button("START GAME",{ImGui::GetContentRegionAvail().x,63})){ui_error_.clear();library_.launch(keyboard_);}
    ImGui::PopFont();ImGui::PopStyleColor(4);ImGui::EndDisabled();
    ImGui::Spacing();
    if(!has_player)ImGui::SetNextItemOpen(true,ImGuiCond_Always);
    if(ImGui::CollapsingHeader("Game application & local files")) {
        ImGui::PushFont(theme_.small);ImGui::TextWrapped("Library: %s",path_text(library_.game_directory()).c_str());
        ImGui::TextWrapped("Session log: %s",state.log.empty()?"No game session yet":state.log.c_str());ImGui::PopFont();
        ImGui::BeginDisabled(state.busy);
        ImGui::SetNextItemWidth(std::max(100.f,ImGui::GetContentRegionAvail().x-115));
        if(ImGui::InputTextWithHint("##host","Native game executable",host_,sizeof(host_),ImGuiInputTextFlags_EnterReturnsTrue))library_.set_executable(text_path(host_));
        ImGui::SameLine();if(ImGui::Button("Locate",{105,43})){picking_host_=true;picker_.open("Select the compiled native game",false,host_);}
        ImGui::EndDisabled();
    }
}
void App::draw() {
    const auto &io=ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Brute Force Command Center",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(theme_.small);ImGui::TextUnformatted("CONFEDERATION  /  SPECIAL OPERATIONS");
    ImGui::SameLine(std::max(450.f,io.DisplaySize.x-375));ImGui::TextColored(gold,"NATIVE RECOMP  /  DEVELOPMENT BUILD");ImGui::PopFont();
    ImGui::Separator();ImGui::Dummy({0,12});
    const auto start=ImGui::GetCursorScreenPos();
    const float available=ImGui::GetContentRegionAvail().x;
    const float hero_width=std::clamp(available*.41f,315.f,475.f),height=ImGui::GetContentRegionAvail().y-64;
    theme_.hero(start,{hero_width,height});
    ImGui::SetCursorScreenPos({start.x+hero_width+20,start.y});
    const float command_width=available-hero_width-20;
    theme_.panel(ImGui::GetWindowDrawList(),{start.x+hero_width+20,start.y},{start.x+available,start.y+height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2{24,23});
    ImGui::BeginChild("command",{command_width,height},ImGuiChildFlags_AlwaysUseWindowPadding);
    try{command();}catch(const std::exception &error){ui_error_=error.what();}
    ImGui::EndChild();ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos({start.x,start.y+height+15});
    ImGui::PushFont(theme_.small);
    ImGui::TextWrapped("EXPERIMENTAL BUILD  /  Windows menus verified. Gameplay and full input, audio and multiplayer are still in development.");
    ImGui::TextColored(muted,"WINDOWS  /  LINUX  /  NATIVE ANDROID TARGET     -     ORIGINAL DISC REQUIRED");ImGui::PopFont();
    std::string selected;
    if(picker_.draw(selected)) {
        if(picking_host_){std::snprintf(host_,sizeof(host_),"%s",selected.c_str());try{library_.set_executable(text_path(selected));}catch(const std::exception &error){ui_error_=error.what();}}
        else std::snprintf(iso_,sizeof(iso_),"%s",selected.c_str());
    }
    if(show_exit_){ImGui::OpenPopup("Native game is running");show_exit_=false;}
    if(ImGui::BeginPopupModal("Native game is running",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Close the game window before exiting this launcher.");
        ImGui::TextUnformatted("This keeps the current session and its saves safe.");
        if(ImGui::Button("Back to launcher"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::End();
}
}
