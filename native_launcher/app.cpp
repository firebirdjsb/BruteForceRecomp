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
static void status_label(const LibraryState &state,ImFont *font) {
    const char *text=state.running?"SQUAD DEPLOYED":state.busy?"OPERATION IN PROGRESS":state.ready?"READY TO DEPLOY":"AWAITING DISC";
    ImGui::PushFont(font);ImGui::TextColored(gold,"%s",text);ImGui::PopFont();
}
void App::command() {
    const auto state=library_.state();
    label("YOUR NEXT MISSION",theme_.small);
    ImGui::PushFont(theme_.heading);ImGui::TextUnformatted("Prepare for deployment.");ImGui::PopFont();
    status_label(state,theme_.small);
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
    if(!state.error.empty() || !ui_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{.95f,.66f,.51f,1});
        ImGui::PushFont(theme_.small);ImGui::TextWrapped("%s",ui_error_.empty()?state.error.c_str():ui_error_.c_str());ImGui::PopFont();ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(state.busy);ImGui::Checkbox("Enable keyboard test mapping",&keyboard_);ImGui::EndDisabled();
    label("Z = confirm     X = back     Enter = Start",theme_.small);
    ImGui::Spacing();
#ifdef _WIN32
    if(ImGui::CollapsingHeader("Graphics")) {
        ImGui::BeginDisabled(state.busy);
        auto options=library_.graphics();
        static const char *backends[]={"auto","dx12","dx11","gdi"};
        int selected=0;for(int i=0;i<4;++i)if(options.backend==backends[i])selected=i;
        if(ImGui::Combo("Renderer",&selected,"Automatic\0DirectX 12\0DirectX 11\0GDI fallback\0")){
            options.backend=backends[selected];library_.set_graphics(options);
        }
        ImGui::BeginDisabled(selected==2 || selected==3);
        if(ImGui::Checkbox("AMD FSR 1 upscaling",&options.fsr1))library_.set_graphics(options);
        ImGui::EndDisabled();
        ImGui::PushFont(theme_.small);ImGui::TextWrapped("Preserves the original picture shape. FSR sharpens and scales the image when the game window is enlarged.");ImGui::PopFont();
        ImGui::EndDisabled();
    }
#endif
    ImGui::BeginDisabled(state.busy || !state.ready || !std::filesystem::is_regular_file(library_.executable()));
    ImGui::PushStyleColor(ImGuiCol_Button,gold);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4{.90f,.82f,.56f,1});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4{.68f,.60f,.37f,1});
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{.07f,.12f,.16f,1});
    ImGui::PushFont(theme_.heading);
    if(ImGui::Button("START GAME",{ImGui::GetContentRegionAvail().x,63})){ui_error_.clear();library_.launch(keyboard_);}
    ImGui::PopFont();ImGui::PopStyleColor(4);ImGui::EndDisabled();
    ImGui::Spacing();
    if(ImGui::CollapsingHeader("Local files & native build")) {
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
