#include "file_picker.h"
#include "process.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace bf {
static std::string lower(std::string value) { for(auto &c:value)c=char(std::tolower(static_cast<unsigned char>(c)));return value; }
void FilePicker::scan() {
    entries_.clear(); error_.clear();
    std::error_code error;
    for(std::filesystem::directory_iterator it(directory_,error),end;it!=end && !error;it.increment(error)) {
        std::error_code item_error;
        const bool folder=it->is_directory(item_error);
        const auto extension=lower(path_text(it->path().extension()));
        if(folder || !iso_ || extension==".iso" || extension==".xiso")entries_.push_back(*it);
        if(entries_.size()>=5000) { error_="This folder has many files. Type a full path to narrow the selection.";break; }
    }
    if(error)error_=error.message();
    std::sort(entries_.begin(),entries_.end(),[](const auto &a,const auto &b){
        std::error_code error_a,error_b;const bool ad=a.is_directory(error_a),bd=b.is_directory(error_b);
        return ad!=bd?ad:lower(path_text(a.path().filename()))<lower(path_text(b.path().filename()));
    });
    directory_text_=path_text(directory_);
    std::snprintf(path_,sizeof(path_),"%s",directory_text_.c_str());
}
void FilePicker::open(const char *title,bool iso,const std::string &current) {
    title_=title;iso_=iso;selected_.clear();requested_=true;visible_=true;
    std::error_code error;
    directory_=current.empty()?std::filesystem::path{}:text_path(current).parent_path();
    if(directory_.empty() || !std::filesystem::is_directory(directory_,error)) {
#ifdef _WIN32
        const char *home=std::getenv("USERPROFILE");
#else
        const char *home=std::getenv("HOME");
#endif
        directory_=home?text_path(home):std::filesystem::current_path();
        if(std::filesystem::is_directory(directory_/"Downloads",error))directory_/="Downloads";
    }
    scan();
}
bool FilePicker::draw(std::string &result) {
    if(requested_){ImGui::OpenPopup(title_.c_str());requested_=false;}
    ImGui::SetNextWindowSize({800,560},ImGuiCond_Appearing);
    bool chosen=false;
    if(ImGui::BeginPopupModal(title_.c_str(),&visible_,ImGuiWindowFlags_NoSavedSettings)) {
        if(ImGui::Button("Up")){directory_=directory_.parent_path();scan();}
        ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
        if(ImGui::InputText("##folder",path_,sizeof(path_),ImGuiInputTextFlags_EnterReturnsTrue)){
            const auto candidate=text_path(path_);std::error_code error;
            if(std::filesystem::is_directory(candidate,error)){directory_=candidate;scan();}
            else if(std::filesystem::is_regular_file(candidate,error)){selected_=path_text(candidate);}
            else error_="That path could not be opened.";
        }
#ifdef _WIN32
        for(char drive='C';drive<='G';++drive){if(drive!='C')ImGui::SameLine();std::string text{drive,':'};
            if(ImGui::SmallButton(text.c_str())){directory_=text_path(text+"/");scan();}}
#endif
        ImGui::BeginChild("files",{0,-117},ImGuiChildFlags_Borders);
        std::filesystem::path enter;
        for(const auto &entry:entries_) {
            std::error_code error;const bool folder=entry.is_directory(error);
            const auto path=path_text(entry.path()),name=(folder?"[+]  ":"      ")+path_text(entry.path().filename());
            if(ImGui::Selectable(name.c_str(),selected_==path,ImGuiSelectableFlags_AllowDoubleClick)) {
                if(folder && ImGui::IsMouseDoubleClicked(0))enter=entry.path();
                else if(!folder){selected_=path;if(ImGui::IsMouseDoubleClicked(0))chosen=true;}
            }
        }
        ImGui::EndChild();
        if(!enter.empty()){directory_=enter;scan();}
        if(!selected_.empty())ImGui::TextWrapped("%s",selected_.c_str());
        if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
        ImGui::BeginDisabled(selected_.empty());if(ImGui::Button("Select file",{160,40}))chosen=true;ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button("Cancel",{110,40})){visible_=false;ImGui::CloseCurrentPopup();}
        if(chosen){result=selected_;visible_=false;ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    return chosen;
}
}
